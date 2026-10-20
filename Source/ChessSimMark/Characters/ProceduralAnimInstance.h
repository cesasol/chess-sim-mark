// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "ProceduralAnimInstance.generated.h"

/** Pose parameters driven by AOpponentCharacter; degrees. Mesh-space semantics are rig agnostic. */
struct FProceduralPoseParams
{
	float HeadPitch = 0.f;   // + looks down
	float HeadYaw = 0.f;     // + turns toward the character's right
	float HeadRoll = 0.f;    // + tilts
	float SpinePitch = 0.f;  // + leans forward
	float SpineRoll = 0.f;
	float Breath = 0.f;      // -1..1
	float ArmDown = 70.f;    // angle below the T-pose horizontal
	float ArmForward = 12.f; // brings hands in front of the body
	float ElbowBend = 25.f;
	/** Optional absolute look direction in mesh space (overrides yaw/pitch from params when set). */
	TOptional<FVector> LookDirection;
};

class FProceduralAnimProxy : public FAnimInstanceProxy
{
public:
	FProceduralAnimProxy() = default;
	explicit FProceduralAnimProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	/** Mesh bone index -> final local rotation. */
	TArray<TPair<int32, FQuat>> Overrides;
};

/**
 * Native (asset-free) anim instance: reference pose plus procedural bone
 * rotations for idle breathing, look-at, posture and reactions. Lets the
 * imported T-pose VRoid/MMD rigs stand naturally without animation assets.
 */
UCLASS(Transient, NotBlueprintable)
class CHESSSIMMARK_API UProceduralAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;

	void SetParams(const FProceduralPoseParams& InParams) { Params = InParams; }
	bool HasRig() const { return Rig.Head != INDEX_NONE; }
	/** Head bone location in component space (ref pose), for look-at maths. */
	FVector GetHeadRefLocation() const { return HeadRefLocation; }
	FVector GetMeshForward() const { return Forward; }

	/** Computes Overrides for the current Params (game thread). */
	void BuildOverrides(TArray<TPair<int32, FQuat>>& Out) const;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FProceduralAnimProxy(this); }

private:
	struct FRig
	{
		int32 Hips = INDEX_NONE, Spine = INDEX_NONE, Chest = INDEX_NONE, Neck = INDEX_NONE, Head = INDEX_NONE;
		int32 UpperArm[2] = {INDEX_NONE, INDEX_NONE};
		int32 LowerArm[2] = {INDEX_NONE, INDEX_NONE};
		int32 Hand[2] = {INDEX_NONE, INDEX_NONE};
	};

	FRig Rig;
	TArray<FTransform> RefCS;    // component-space ref pose per mesh bone
	TArray<FTransform> RefLocal; // local ref pose per mesh bone
	TArray<int32> Parents;
	FVector Forward = FVector(0, 1, 0);
	FVector Up = FVector(0, 0, 1);
	FVector Right = FVector(1, 0, 0);
	FVector HeadRefLocation = FVector::ZeroVector;
	FProceduralPoseParams Params;

	FQuat ToLocal(int32 Bone, const FQuat& ComponentDelta) const;
};
