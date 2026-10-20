// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Characters/ProceduralAnimInstance.h"
#include "Data/GameDataTypes.h"
#include "GameFramework/Actor.h"
#include "OpponentCharacter.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * The romanceable opponent across the board. Live reactions combine VRoid
 * facial morph targets (Fcl_*), procedural posture (UProceduralAnimInstance),
 * blinking, lip flap while talking, gaze and short reaction "impulses".
 * Without an imported model it shows a primitive stand-in with an emoticon face.
 * The actor faces its local +X.
 */
UCLASS()
class CHESSSIMMARK_API AOpponentCharacter : public AActor
{
	GENERATED_BODY()

public:
	AOpponentCharacter();

	virtual void Tick(float DeltaSeconds) override;

	/** Load the model for a character (Characters.csv `Model` folder containing Body/Face/Hair meshes). */
	void ApplyCharacter(const FCharacterDef& Def);

	void SetEmotion(EEmotion InEmotion);
	EEmotion GetEmotion() const { return Emotion; }
	/** Emotion plus a short body-language beat (bounce, head shake, recoil...). */
	void React(EEmotion InEmotion);
	void SetTalking(bool bInTalking) { bTalking = bInTalking; }
	/** World point to look at; unset = look straight ahead. */
	void SetLookTarget(TOptional<FVector> World) { LookTarget = World; }

	/** Approximate world location just above the head, for speech bubbles. */
	FVector GetHeadWorldLocation() const;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TObjectPtr<USkeletalMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TObjectPtr<USkeletalMeshComponent> Face;
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TObjectPtr<USkeletalMeshComponent> Hair;

	// Stand-in pieces used when no model is available.
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TObjectPtr<UStaticMeshComponent> StandInBody;
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TObjectPtr<UStaticMeshComponent> StandInHead;
	UPROPERTY(VisibleAnywhere, Category = "Character")
	TObjectPtr<UTextRenderComponent> StandInFace;

private:
	struct FMorphWeight
	{
		FName Name;
		float Weight;
	};
	struct FEmotionLook
	{
		TArray<FMorphWeight> Morphs;
		float HeadPitch = 0, HeadYaw = 0, HeadRoll = 0, SpinePitch = 0;
		const TCHAR* Emoticon = TEXT("- _ -");
	};
	static const FEmotionLook& LookFor(EEmotion E);

	void UpdateMorphs(float Dt);
	void UpdatePose(float Dt);
	bool HasModel() const;

	EEmotion Emotion = EEmotion::Neutral;
	bool bTalking = false;
	TOptional<FVector> LookTarget;

	/** Current smoothed morph weights by name. */
	TMap<FName, float> MorphCurrent;
	TSet<FName> AvailableMorphs;

	FProceduralPoseParams Pose;
	float Time = 0.f;
	float NextBlink = 2.f;
	float BlinkT = -1.f;

	/** Decaying reaction impulse. */
	EEmotion ImpulseKind = EEmotion::Neutral;
	float ImpulseT = -1.f;
	float FaceHeight = 160.f;
	FLinearColor Accent = FLinearColor::White;
};
