// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ChessSimSettings.generated.h"

/** Project settings (Config/DefaultGame.ini, section [/Script/ChessSimMark.ChessSimSettings]). */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Chess Sim"))
class CHESSSIMMARK_API UChessSimSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Optional explicit Stockfish path; relative paths resolve against the project directory. */
	UPROPERTY(Config, EditAnywhere, Category = "Engine")
	FString StockfishPath;

	/** Analyst search depth used for live reactions and hints. */
	UPROPERTY(Config, EditAnywhere, Category = "Engine", meta = (ClampMin = 1, ClampMax = 24))
	int32 AnalysisDepth = 12;

	/** Minimum time the opponent "thinks" before moving, so reactions can be read. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ClampMin = 0))
	float OpponentMinThinkSeconds = 1.0f;

	/** Duration of the linear camera blends between conversation and board. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ClampMin = 0))
	float CameraBlendSeconds = 1.5f;

	/** Seconds a reaction bubble stays on screen. */
	UPROPERTY(Config, EditAnywhere, Category = "Presentation", meta = (ClampMin = 0.5))
	float ReactionSeconds = 3.5f;
};
