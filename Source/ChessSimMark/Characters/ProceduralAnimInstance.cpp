// SPDX-License-Identifier: Apache-2.0
#include "Characters/ProceduralAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "ChessSimMark.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

namespace
{
/** Bone names lower-cased with separators removed, so "Left arm", "Left_arm" and "Left-arm" all match. */
FString NormalizeBoneName(const FString& In)
{
	FString Out;
	for (TCHAR C : In)
		if (FChar::IsAlnum(C)) Out.AppendChar(FChar::ToLower(C));
	return Out;
}

int32 FindBone(const FReferenceSkeleton& Ref, std::initializer_list<const TCHAR*> Candidates)
{
	for (const TCHAR* C : Candidates)
	{
		const int32 I = Ref.FindBoneIndex(FName(C));
		if (I != INDEX_NONE) return I;
	}
	for (const TCHAR* C : Candidates)
	{
		const FString Want = NormalizeBoneName(C);
		for (int32 I = 0; I < Ref.GetNum(); ++I)
			if (NormalizeBoneName(Ref.GetBoneName(I).ToString()) == Want) return I;
	}
	return INDEX_NONE;
}

/** Rotation that turns unit vector A toward B by Fraction (0..1). */
FQuat Toward(const FVector& A, const FVector& B, float Fraction = 1.f)
{
	return FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(A.GetSafeNormal(), B.GetSafeNormal()), Fraction);
}

FQuat PitchQ(const FVector& Fwd, const FVector& Up, float Deg)
{
	const float R = FMath::DegreesToRadians(Deg);
	return Toward(Fwd, Fwd * FMath::Cos(R) - Up * FMath::Sin(R));
}

FQuat YawQ(const FVector& Fwd, const FVector& Right, float Deg)
{
	const float R = FMath::DegreesToRadians(Deg);
	return Toward(Fwd, Fwd * FMath::Cos(R) + Right * FMath::Sin(R));
}
} // namespace

void FProceduralAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
	if (const UProceduralAnimInstance* I = Cast<UProceduralAnimInstance>(InAnimInstance)) I->BuildOverrides(Overrides);
}

bool FProceduralAnimProxy::Evaluate(FPoseContext& Output)
{
	Output.ResetToRefPose();
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (const TPair<int32, FQuat>& O : Overrides)
	{
		const FCompactPoseBoneIndex CI = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(O.Key));
		if (CI.IsValid()) Output.Pose[CI].SetRotation(O.Value);
	}
	return true;
}

void UProceduralAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Rig = FRig();
	const USkeletalMeshComponent* Comp = GetSkelMeshComponent();
	const USkeletalMesh* Mesh = Comp ? Comp->GetSkeletalMeshAsset() : nullptr;
	if (!Mesh) return;
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();

	// Candidate names cover VRoid (J_Bip_*), MMD-style exports, Mixamo and UE mannequins.
	Rig.Hips = FindBone(Ref, {TEXT("J_Bip_C_Hips"), TEXT("Hips"), TEXT("mixamorig:Hips"), TEXT("pelvis")});
	Rig.Spine = FindBone(Ref, {TEXT("J_Bip_C_Spine"), TEXT("Spine"), TEXT("mixamorig:Spine"), TEXT("spine_01")});
	Rig.Chest = FindBone(Ref, {TEXT("J_Bip_C_UpperChest"), TEXT("J_Bip_C_Chest"), TEXT("Chest"), TEXT("mixamorig:Spine2"), TEXT("spine_03")});
	Rig.Neck = FindBone(Ref, {TEXT("J_Bip_C_Neck"), TEXT("Neck"), TEXT("mixamorig:Neck"), TEXT("neck_01")});
	Rig.Head = FindBone(Ref, {TEXT("J_Bip_C_Head"), TEXT("Head"), TEXT("mixamorig:Head"), TEXT("head")});
	Rig.UpperArm[0] = FindBone(Ref, {TEXT("J_Bip_L_UpperArm"), TEXT("Left arm"), TEXT("Left_arm"), TEXT("mixamorig:LeftArm"), TEXT("upperarm_l")});
	Rig.UpperArm[1] = FindBone(Ref, {TEXT("J_Bip_R_UpperArm"), TEXT("Right arm"), TEXT("Right_arm"), TEXT("mixamorig:RightArm"), TEXT("upperarm_r")});
	Rig.LowerArm[0] = FindBone(Ref, {TEXT("J_Bip_L_LowerArm"), TEXT("Left elbow"), TEXT("Left_elbow"), TEXT("mixamorig:LeftForeArm"), TEXT("lowerarm_l")});
	Rig.LowerArm[1] = FindBone(Ref, {TEXT("J_Bip_R_LowerArm"), TEXT("Right elbow"), TEXT("Right_elbow"), TEXT("mixamorig:RightForeArm"), TEXT("lowerarm_r")});
	Rig.Hand[0] = FindBone(Ref, {TEXT("J_Bip_L_Hand"), TEXT("Left wrist"), TEXT("Left_wrist"), TEXT("mixamorig:LeftHand"), TEXT("hand_l")});
	Rig.Hand[1] = FindBone(Ref, {TEXT("J_Bip_R_Hand"), TEXT("Right wrist"), TEXT("Right_wrist"), TEXT("mixamorig:RightHand"), TEXT("hand_r")});

	if (Rig.UpperArm[0] == INDEX_NONE || Rig.UpperArm[1] == INDEX_NONE)
	{
		FString Names;
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			const FString B = Ref.GetBoneName(I).ToString();
			if (B.Contains(TEXT("arm")) || B.Contains(TEXT("Arm")) || B.Contains(TEXT("elbow"))) Names += B + TEXT(", ");
		}
		UE_LOG(LogChessSim, Warning, TEXT("%s: arm bones not recognised; candidates: %s"), *Mesh->GetName(), *Names);
	}

	const int32 N = Ref.GetNum();
	RefLocal = Ref.GetRefBonePose();
	RefCS.SetNum(N);
	Parents.SetNum(N);
	for (int32 I = 0; I < N; ++I)
	{
		Parents[I] = Ref.GetParentIndex(I);
		RefCS[I] = Parents[I] == INDEX_NONE ? RefLocal[I] : RefLocal[I] * RefCS[Parents[I]];
	}

	if (Rig.Head == INDEX_NONE)
	{
		UE_LOG(LogChessSim, Warning, TEXT("%s: no head bone found; procedural animation disabled"), *Mesh->GetName());
		return;
	}
	HeadRefLocation = RefCS[Rig.Head].GetLocation();
	UE_LOG(LogChessSim, Log, TEXT("ProceduralAnim rig on %s: head=%d neck=%d spine=%d chest=%d arms=%d/%d elbows=%d/%d"), *Mesh->GetName(),
		Rig.Head, Rig.Neck, Rig.Spine, Rig.Chest, Rig.UpperArm[0], Rig.UpperArm[1], Rig.LowerArm[0], Rig.LowerArm[1]);
	// Forward: face front is where the eyes sit relative to the head; default +Y (glTF import).
	const int32 EyeL = FindBone(Ref, {TEXT("J_Adj_L_FaceEye"), TEXT("Eye_L"), TEXT("mixamorig:LeftEye")});
	const int32 EyeR = FindBone(Ref, {TEXT("J_Adj_R_FaceEye"), TEXT("Eye_R"), TEXT("mixamorig:RightEye")});
	if (EyeL != INDEX_NONE && EyeR != INDEX_NONE)
	{
		// The midpoint between the eyes cancels the lateral offset and leaves the facing direction.
		FVector D = (RefCS[EyeL].GetLocation() + RefCS[EyeR].GetLocation()) * 0.5f - HeadRefLocation;
		D.Z = 0;
		if (D.SizeSquared() > 0.01f) Forward = D.GetSafeNormal();
	}
	Right = FVector::CrossProduct(Up, Forward).GetSafeNormal();
	// Make "Right" the character's right using the arm layout: right upper arm sits on that side.
	if (Rig.UpperArm[1] != INDEX_NONE && FVector::DotProduct(RefCS[Rig.UpperArm[1]].GetLocation() - HeadRefLocation, Right) < 0)
		Right = -Right;
}

FQuat UProceduralAnimInstance::ToLocal(int32 Bone, const FQuat& ComponentDelta) const
{
	const int32 P = Parents[Bone];
	const FQuat ParentRot = P == INDEX_NONE ? FQuat::Identity : RefCS[P].GetRotation();
	return ParentRot.Inverse() * ComponentDelta * ParentRot * RefLocal[Bone].GetRotation();
}

void UProceduralAnimInstance::BuildOverrides(TArray<TPair<int32, FQuat>>& Out) const
{
	Out.Reset();
	if (Rig.Head == INDEX_NONE || RefCS.Num() == 0) return;
	const FProceduralPoseParams& P = Params;

	// Spine posture: lean + breathing.
	const FQuat SpineRollQ(Forward, FMath::DegreesToRadians(P.SpineRoll));
	const FQuat Lean = PitchQ(Forward, Up, P.SpinePitch * 0.5f) * SpineRollQ;
	if (Rig.Spine != INDEX_NONE) Out.Emplace(Rig.Spine, ToLocal(Rig.Spine, Lean));
	const FQuat ChestQ = PitchQ(Forward, Up, P.SpinePitch * 0.5f - P.Breath * 1.2f);
	if (Rig.Chest != INDEX_NONE) Out.Emplace(Rig.Chest, ToLocal(Rig.Chest, ChestQ));

	// Head: either a look direction or explicit angles, split between neck and head.
	FQuat HeadQ;
	if (P.LookDirection.IsSet())
	{
		FVector Dir = P.LookDirection->GetSafeNormal();
		HeadQ = Toward(Forward, Dir) * FQuat(Forward, FMath::DegreesToRadians(P.HeadRoll));
		HeadQ = PitchQ(Forward, Up, P.HeadPitch) * HeadQ;
	}
	else
	{
		HeadQ = YawQ(Forward, Right, P.HeadYaw) * PitchQ(Forward, Up, P.HeadPitch) * FQuat(Forward, FMath::DegreesToRadians(P.HeadRoll));
	}
	// Remove what the spine already contributes so the gaze lands on target.
	HeadQ = (Lean * ChestQ).Inverse() * HeadQ;
	const FQuat NeckQ = FQuat::Slerp(FQuat::Identity, HeadQ, 0.4f);
	const FQuat HeadOnlyQ = NeckQ.Inverse() * HeadQ; // remaining rotation after the neck
	if (Rig.Neck != INDEX_NONE)
	{
		Out.Emplace(Rig.Neck, ToLocal(Rig.Neck, NeckQ));
		Out.Emplace(Rig.Head, ToLocal(Rig.Head, HeadOnlyQ));
	}
	else
	{
		Out.Emplace(Rig.Head, ToLocal(Rig.Head, HeadQ));
	}

	// Arms: rotate out of the T/A-pose to hang down and slightly forward, then bend the elbows.
	for (int32 Side = 0; Side < 2; ++Side)
	{
		const int32 U = Rig.UpperArm[Side], L = Rig.LowerArm[Side], H = Rig.Hand[Side];
		if (U == INDEX_NONE || L == INDEX_NONE) continue;
		const FVector ArmDir = (RefCS[L].GetLocation() - RefCS[U].GetLocation()).GetSafeNormal();
		FVector Lateral = ArmDir;
		Lateral.Z = 0;
		Lateral = Lateral.GetSafeNormal();
		const float Down = FMath::DegreesToRadians(P.ArmDown);
		const float Fwd = FMath::DegreesToRadians(P.ArmForward);
		const FVector Target = (Lateral * FMath::Cos(Down) - Up * FMath::Sin(Down) + Forward * FMath::Sin(Fwd)).GetSafeNormal();
		const FQuat UpperQ = Toward(ArmDir, Target);
		Out.Emplace(U, ToLocal(U, UpperQ));

		if (H != INDEX_NONE)
		{
			// Bend expressed in the already-rotated frame, then mapped back for the child.
			const FVector Fore = UpperQ.RotateVector((RefCS[H].GetLocation() - RefCS[L].GetLocation()).GetSafeNormal());
			const float B = FMath::DegreesToRadians(P.ElbowBend);
			const FQuat Bend = Toward(Fore, Fore * FMath::Cos(B) + Forward * FMath::Sin(B));
			Out.Emplace(L, ToLocal(L, UpperQ.Inverse() * Bend * UpperQ));
		}
	}
}
