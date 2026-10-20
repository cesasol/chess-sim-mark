// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Chess/ChessTypes.h"
#include "GameDataTypes.generated.h"

UENUM(BlueprintType)
enum class EEmotion : uint8
{
	Neutral,
	Happy,
	Smug,
	Annoyed,
	Flustered,
	Surprised,
	Sad,
	Thinking,
	Determined,
	Dark,
};

namespace Emotion
{
CHESSSIMMARK_API EEmotion FromString(const FString& S, EEmotion Default = EEmotion::Neutral);
CHESSSIMMARK_API FString ToString(EEmotion E);
}

UENUM(BlueprintType)
enum class EScriptOp : uint8
{
	Line,
	Choice,
	Option,
	Affection,
	Flag,
	If,
	Goto,
	Camera,
	Chess,
	End,
	Invalid,
};

struct FCharacterDef
{
	FString Id;
	bool bFemale = true;
	bool bOpponent = false;
	FString Archetype;
	FString NameKey;
	FString BlurbKey;
	FLinearColor Accent = FLinearColor::White;
	int32 Age = 18;
	EChessDifficulty Difficulty = EChessDifficulty::Club;
	/** Package folder holding Body/Face/Hair skeletal meshes; empty = primitive stand-in. */
	FString Model;
	float ModelScale = 1.f;
};

struct FScriptStep
{
	FString Scene;
	FString Step;
	EScriptOp Op = EScriptOp::Invalid;
	FString Speaker;
	FString TextKey;
	/** Empty = keep current expression. */
	TOptional<EEmotion> Emotion;
	FString Arg;
	FString Next;
};

struct FReactionDef
{
	FString Character;
	FString Trigger;
	FString TextKey;
	EEmotion Emotion = EEmotion::Neutral;
	float Weight = 1.f;
};
