// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "ChessTypes.generated.h"

UENUM(BlueprintType)
enum class EChessDifficulty : uint8
{
	Novice,
	Casual,
	Club,
	Strong,
	Champion,
};

UENUM(BlueprintType)
enum class EChessResult : uint8
{
	None,
	Win,
	Lose,
	Draw,
};

/** Engine settings for one difficulty preset. */
struct FDifficultyPreset
{
	int32 SkillLevel = 20;
	int32 Depth = 0;
	int32 MoveTimeMs = 0;
	/** Chance (0..1) to play a uniformly random legal move instead; makes the lowest level beatable. */
	float RandomMoveChance = 0.f;
	/** Fallback alpha-beta depth when Stockfish is unavailable. */
	int32 FallbackDepth = 1;
};

namespace ChessDifficulty
{
CHESSSIMMARK_API FDifficultyPreset GetPreset(EChessDifficulty D);
CHESSSIMMARK_API FString ToString(EChessDifficulty D);
CHESSSIMMARK_API bool FromString(const FString& S, EChessDifficulty& Out);
}
