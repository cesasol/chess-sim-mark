// SPDX-License-Identifier: Apache-2.0
#include "Characters/OpponentCharacter.h"

#include "ChessSimMark.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/MorphTarget.h"
#include "Materials/MaterialInstanceDynamic.h"

AOpponentCharacter::AOpponentCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Body->SetAnimInstanceClass(UProceduralAnimInstance::StaticClass());
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

	Face = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Face"));
	Face->SetupAttachment(Body);
	Face->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Hair = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Hair"));
	Hair->SetupAttachment(Body);
	Hair->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	StandInBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandInBody"));
	StandInBody->SetupAttachment(Root);
	StandInBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StandInHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StandInHead"));
	StandInHead->SetupAttachment(StandInBody);
	StandInHead->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StandInFace = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StandInFace"));
	StandInFace->SetupAttachment(Root);
	StandInFace->SetHorizontalAlignment(EHTA_Center);
	StandInFace->SetVerticalAlignment(EVRTA_TextCenter);
	StandInFace->SetWorldSize(9.f);
	StandInFace->SetTextRenderColor(FColor(40, 20, 30));
}

bool AOpponentCharacter::HasModel() const { return Body && Body->GetSkeletalMeshAsset() != nullptr; }

void AOpponentCharacter::ApplyCharacter(const FCharacterDef& Def)
{
	Accent = Def.Accent;
	USkeletalMesh* BodyMesh = nullptr;
	if (!Def.Model.IsEmpty())
	{
		auto Load = [&](const TCHAR* Part) {
			return LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Def.Model, Part, Part));
		};
		BodyMesh = Load(TEXT("Body"));
		Body->SetSkeletalMesh(BodyMesh);
		Face->SetSkeletalMesh(Load(TEXT("Face")));
		Hair->SetSkeletalMesh(Load(TEXT("Hair")));
		if (!BodyMesh) UE_LOG(LogChessSim, Warning, TEXT("Character %s: model '%s' not found; using stand-in"), *Def.Id, *Def.Model);
	}
	else
	{
		Body->SetSkeletalMesh(nullptr);
		Face->SetSkeletalMesh(nullptr);
		Hair->SetSkeletalMesh(nullptr);
	}

	const bool bModel = BodyMesh != nullptr;
	Body->SetVisibility(bModel, true);
	StandInBody->SetVisibility(!bModel, true);
	StandInFace->SetVisibility(!bModel);
	AvailableMorphs.Reset();
	MorphCurrent.Reset();

	if (bModel)
	{
		Body->SetRelativeScale3D(FVector(Def.ModelScale));
		Face->SetLeaderPoseComponent(Body);
		Hair->SetLeaderPoseComponent(Body);
		Body->InitAnim(true);
		if (USkeletalMesh* FaceMesh = Face->GetSkeletalMeshAsset())
		{
			for (const UMorphTarget* M : FaceMesh->GetMorphTargets())
				if (M) AvailableMorphs.Add(M->GetFName());
		}
		// Rotate the mesh so its detected facing points along actor +X.
		if (UProceduralAnimInstance* Anim = Cast<UProceduralAnimInstance>(Body->GetAnimInstance()))
		{
			const FVector Fwd = Anim->GetMeshForward();
			Body->SetRelativeRotation(FRotator(0, -FMath::RadiansToDegrees(FMath::Atan2(Fwd.Y, Fwd.X)), 0));
			FaceHeight = Anim->GetHeadRefLocation().Z * Def.ModelScale;
		}
		UE_LOG(LogChessSim, Log, TEXT("Character %s: model %s, %d face morphs"), *Def.Id, *Def.Model, AvailableMorphs.Num());
	}
	else
	{
		// Primitive stand-in: capsule-ish body, sphere head, emoticon face.
		UStaticMesh* Cyl = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		UStaticMesh* Sph = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		StandInBody->SetStaticMesh(Cyl);
		StandInBody->SetRelativeScale3D(FVector(0.38f, 0.32f, 1.3f));
		StandInBody->SetRelativeLocation(FVector(0, 0, 65.f));
		StandInHead->SetStaticMesh(Sph);
		StandInHead->SetRelativeScale3D(FVector(0.24f / 0.38f, 0.24f / 0.32f, 0.26f / 1.3f));
		StandInHead->SetRelativeLocation(FVector(0, 0, 70.f / 1.3f));
		if (UMaterialInstanceDynamic* M = StandInBody->CreateDynamicMaterialInstance(0, Mat)) M->SetVectorParameterValue(TEXT("Color"), Accent);
		if (UMaterialInstanceDynamic* M = StandInHead->CreateDynamicMaterialInstance(0, Mat)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.85f, 0.75f));
		StandInFace->SetRelativeLocation(FVector(13.f, 0, 157.f));
		FaceHeight = 157.f;
	}
	SetEmotion(EEmotion::Neutral);
}

const AOpponentCharacter::FEmotionLook& AOpponentCharacter::LookFor(EEmotion E)
{
	// VRoid "Fcl_*" morph names; missing morphs are skipped per model.
	static const TMap<EEmotion, FEmotionLook> Table = [] {
		TMap<EEmotion, FEmotionLook> T;
		T.Add(EEmotion::Neutral, {{}, 0, 0, 0, 0, TEXT("- _ -")});
		T.Add(EEmotion::Happy, {{{TEXT("Fcl_ALL_Joy"), 0.9f}}, -4, 0, 5, 0, TEXT("^ v ^")});
		T.Add(EEmotion::Smug, {{{TEXT("Fcl_ALL_Fun"), 0.7f}, {TEXT("Fcl_BRW_Angry"), 0.3f}}, -9, -8, -6, -4, TEXT("~ v ~")});
		T.Add(EEmotion::Annoyed, {{{TEXT("Fcl_ALL_Angry"), 0.8f}}, 3, 18, 0, 2, TEXT("> _ <")});
		T.Add(EEmotion::Flustered,
			{{{TEXT("Fcl_ALL_Joy"), 0.3f}, {TEXT("Fcl_EYE_Surprised"), 0.3f}, {TEXT("Fcl_BRW_Sorrow"), 0.6f}, {TEXT("Fcl_MTH_Small"), 0.6f}},
				12, -25, 8, 2, TEXT("o///o")});
		T.Add(EEmotion::Surprised, {{{TEXT("Fcl_ALL_Surprised"), 1.f}}, -8, 0, 0, -8, TEXT("O _ O")});
		T.Add(EEmotion::Sad, {{{TEXT("Fcl_ALL_Sorrow"), 0.9f}}, 16, 0, 4, 6, TEXT("; _ ;")});
		T.Add(EEmotion::Thinking, {{{TEXT("Fcl_BRW_Sorrow"), 0.35f}, {TEXT("Fcl_EYE_Close_R"), 0.3f}, {TEXT("Fcl_MTH_Small"), 0.4f}}, 8, 0, 10, 6, TEXT("- . o")});
		T.Add(EEmotion::Determined, {{{TEXT("Fcl_BRW_Angry"), 0.6f}, {TEXT("Fcl_EYE_Angry"), 0.4f}, {TEXT("Fcl_MTH_Close"), 0.4f}}, 4, 0, 0, 9, TEXT("` ^ '")});
		T.Add(EEmotion::Dark,
			{{{TEXT("Fcl_ALL_Fun"), 0.55f}, {TEXT("Fcl_EYE_Highlight_Hide"), 1.f}, {TEXT("Fcl_BRW_Angry"), 0.2f}}, 9, 0, 14, 4, TEXT("@ w @")});
		return T;
	}();
	const FEmotionLook* L = Table.Find(E);
	return L ? *L : Table[EEmotion::Neutral];
}

void AOpponentCharacter::SetEmotion(EEmotion InEmotion)
{
	Emotion = InEmotion;
	if (StandInFace) StandInFace->SetText(FText::FromString(LookFor(Emotion).Emoticon));
}

void AOpponentCharacter::React(EEmotion InEmotion)
{
	SetEmotion(InEmotion);
	ImpulseKind = InEmotion;
	ImpulseT = 0.f;
}

FVector AOpponentCharacter::GetHeadWorldLocation() const
{
	return GetActorTransform().TransformPosition(FVector(0, 0, FaceHeight + 28.f));
}

void AOpponentCharacter::UpdateMorphs(float Dt)
{
	if (!HasModel() || !Face->GetSkeletalMeshAsset()) return;
	TMap<FName, float> Target;
	for (const FMorphWeight& M : LookFor(Emotion).Morphs) Target.Add(M.Name, M.Weight);

	// Blink (suppressed for expressions that already close the eyes).
	if (BlinkT >= 0.f)
	{
		BlinkT += Dt;
		const float B = FMath::Clamp(1.f - FMath::Abs(BlinkT - 0.07f) / 0.07f, 0.f, 1.f);
		if (Emotion != EEmotion::Happy && Emotion != EEmotion::Smug) Target.FindOrAdd(TEXT("Fcl_EYE_Close")) = B;
		if (BlinkT > 0.14f) BlinkT = -1.f;
	}
	else if ((NextBlink -= Dt) <= 0.f)
	{
		BlinkT = 0.f;
		NextBlink = FMath::FRandRange(2.2f, 5.5f);
	}

	// Lip flap while a line is typing.
	if (bTalking)
	{
		const float Open = 0.25f + 0.35f * (0.5f + 0.5f * FMath::Sin(Time * 17.f)) * (0.6f + 0.4f * FMath::Sin(Time * 5.3f));
		Target.FindOrAdd(TEXT("Fcl_MTH_A")) = Open;
	}

	// Ease every known morph toward its target.
	TSet<FName> Names;
	for (const auto& P : MorphCurrent) Names.Add(P.Key);
	for (const auto& P : Target) Names.Add(P.Key);
	for (const FName& N : Names)
	{
		if (!AvailableMorphs.Contains(N)) continue;
		const float Goal = Target.Contains(N) ? Target[N] : 0.f;
		const bool bFast = N == TEXT("Fcl_EYE_Close") || N == TEXT("Fcl_MTH_A");
		float& Cur = MorphCurrent.FindOrAdd(N);
		Cur = bFast ? Goal : FMath::FInterpTo(Cur, Goal, Dt, 8.f);
		Face->SetMorphTarget(N, Cur);
	}
}

void AOpponentCharacter::UpdatePose(float Dt)
{
	const FEmotionLook& L = LookFor(Emotion);
	const float K = FMath::Clamp(Dt * 5.f, 0.f, 1.f);
	Pose.HeadPitch = FMath::Lerp(Pose.HeadPitch, L.HeadPitch, K);
	Pose.HeadRoll = FMath::Lerp(Pose.HeadRoll, L.HeadRoll, K);
	Pose.SpinePitch = FMath::Lerp(Pose.SpinePitch, L.SpinePitch, K);
	Pose.HeadYaw = FMath::Lerp(Pose.HeadYaw, L.HeadYaw, K);
	Pose.Breath = FMath::Sin(Time * 1.7f);
	Pose.SpineRoll = FMath::Sin(Time * 0.6f) * 1.2f;

	float ExtraPitch = 0.f, ExtraYaw = 0.f, ExtraLean = 0.f, Bob = 0.f;
	if (ImpulseT >= 0.f)
	{
		ImpulseT += Dt;
		const float Decay = FMath::Exp(-ImpulseT * 3.5f);
		switch (ImpulseKind)
		{
		case EEmotion::Annoyed: ExtraYaw = FMath::Sin(ImpulseT * 22.f) * 10.f * Decay; break;              // head shake
		case EEmotion::Happy: Bob = FMath::Abs(FMath::Sin(ImpulseT * 12.f)) * 2.5f * Decay; break;         // little hop
		case EEmotion::Surprised: ExtraLean = -10.f * Decay; ExtraPitch = -6.f * Decay; break;            // recoil
		case EEmotion::Flustered: ExtraYaw = -12.f * Decay; ExtraPitch = 6.f * Decay; break;             // look away
		case EEmotion::Smug: ExtraPitch = -6.f * Decay; break;                                            // chin up
		case EEmotion::Determined: ExtraLean = 8.f * Decay; break;                                       // lean in
		case EEmotion::Sad: ExtraPitch = 8.f * Decay; break;
		case EEmotion::Dark: ExtraLean = 10.f * Decay; ExtraPitch = 4.f * Decay; break;                // creepy lean
		default: ExtraPitch = FMath::Sin(ImpulseT * 9.f) * 4.f * Decay; break;                        // nod
		}
		if (ImpulseT > 2.f) ImpulseT = -1.f;
	}

	FProceduralPoseParams Out = Pose;
	Out.HeadPitch += ExtraPitch;
	Out.HeadYaw += ExtraYaw;
	Out.SpinePitch += ExtraLean;

	if (HasModel())
	{
		// Gaze: aim the head at the target unless the emotion turns the face away.
		if (LookTarget.IsSet() && FMath::Abs(Out.HeadYaw) < 10.f)
		{
			const FVector HeadWorld = Body->GetComponentTransform().TransformPosition(
				Cast<UProceduralAnimInstance>(Body->GetAnimInstance()) ? Cast<UProceduralAnimInstance>(Body->GetAnimInstance())->GetHeadRefLocation() : FVector::ZeroVector);
			FVector Dir = Body->GetComponentTransform().InverseTransformVectorNoScale(*LookTarget - HeadWorld).GetSafeNormal();
			if (UProceduralAnimInstance* Anim = Cast<UProceduralAnimInstance>(Body->GetAnimInstance()))
			{
				const FVector Fwd = Anim->GetMeshForward();
				// Limit to a 45 degree cone around the facing direction.
				const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Fwd, Dir), -1.f, 1.f));
				if (Angle > FMath::DegreesToRadians(45.f))
					Dir = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Fwd, Dir), FMath::DegreesToRadians(45.f) / Angle).RotateVector(Fwd);
				Out.LookDirection = Dir;
			}
		}
		if (UProceduralAnimInstance* Anim = Cast<UProceduralAnimInstance>(Body->GetAnimInstance())) Anim->SetParams(Out);
		Body->SetRelativeLocation(FVector(0, 0, Bob));
	}
	else
	{
		// Stand-in: tilt the body/face so reactions still read.
		StandInBody->SetRelativeRotation(FRotator(-Out.SpinePitch * 0.6f, Out.HeadYaw * 0.3f, Out.HeadRoll * 0.6f));
		StandInBody->SetRelativeLocation(FVector(0, 0, 65.f + Bob + Out.Breath * 0.3f));
		StandInFace->SetRelativeLocation(FVector(14.f, Out.HeadYaw * 0.15f, 157.f + Bob - Out.HeadPitch * 0.1f));
	}
}

void AOpponentCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	UpdateMorphs(DeltaSeconds);
	UpdatePose(DeltaSeconds);
}
