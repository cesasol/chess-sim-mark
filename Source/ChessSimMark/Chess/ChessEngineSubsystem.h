// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Chess/ChessCore.h"
#include "Chess/ChessTypes.h"
#include "Chess/UciEngine.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "ChessEngineSubsystem.generated.h"

/** Engine analysis of a position. Scores are always from White's point of view. */
struct FChessAnalysis
{
	bool bValid = false;
	int32 WhiteCp = 0;
	bool bMate = false;
	int32 MateIn = 0; // from side to move, as reported by the engine
	chesscore::ChessMove BestMove;
	bool bFromStockfish = false;
};

/**
 * Owns two Stockfish processes: the "opponent" (strength-limited, picks the
 * character's moves) and the "analyst" (full strength, evaluates positions to
 * drive live reactions and hints). Falls back to the built-in search in
 * chesscore when Stockfish cannot be found or dies.
 */
UCLASS()
class CHESSSIMMARK_API UChessEngineSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return !IsTemplate(); }
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual ETickableTickType GetTickableTickType() const override { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Always; }

	/** True when Stockfish was found and completed the UCI handshake (or is still starting). */
	bool IsStockfishAvailable() const;
	FString GetEngineDescription() const;
	const FString& GetStockfishPath() const { return StockfishPath; }

	/** Locate the Stockfish binary. Order: env CHESSSIM_STOCKFISH, settings, staged ThirdParty copy, PATH. */
	static FString FindStockfish();

	using FMoveCallback = TFunction<void(const chesscore::ChessMove&)>;
	using FAnalysisCallback = TFunction<void(const FChessAnalysis&)>;

	/** Ask the opponent engine for a move in the game's current position. */
	void RequestOpponentMove(const chesscore::Game& Game, EChessDifficulty Difficulty, FMoveCallback OnMove);
	/** Analyse the game's current position at full strength. */
	void RequestAnalysis(const chesscore::Game& Game, int32 Depth, FAnalysisCallback OnDone);
	/** Drop every pending request; callbacks will not fire. */
	void CancelAll();

private:
	void StartEngines();
	FEngineSearchParams MakeParams(const chesscore::Game& Game) const;

	TUniquePtr<FUciEngine> Opponent;
	TUniquePtr<FUciEngine> Analyst;
	FString StockfishPath;
	FRandomStream Random;

	/** Deferred fallback work, run on the next Tick to keep callbacks asynchronous. */
	TArray<TFunction<void()>> Deferred;
	uint32 Generation = 0;
};
