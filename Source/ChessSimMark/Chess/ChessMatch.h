// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Chess/ChessCore.h"
#include "Chess/ChessTypes.h"
#include "Tickable.h"
#include "UObject/Object.h"
#include "ChessMatch.generated.h"

class AChessBoardActor;
class UChessEngineSubsystem;
struct FChessAnalysis;

UENUM()
enum class EMatchPhase : uint8
{
	Idle,
	PlayerTurn,
	Animating,
	Analyzing,
	OpponentThinking,
	Over,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnMatchReaction, const FString& /*Trigger*/);
DECLARE_MULTICAST_DELEGATE(FOnMatchChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnMatchOver, EChessResult, chesscore::GameStatus);

/**
 * One chess game against the current opponent. Owns the rules state, drives
 * the board actor, asks the engine subsystem for opponent moves and analysis,
 * and turns evaluation swings into reaction triggers (Reactions.csv).
 */
UCLASS()
class CHESSSIMMARK_API UChessMatch : public UObject, public FTickableGameObject
{
	GENERATED_BODY()

public:
	void Begin(UChessEngineSubsystem* InEngine, AChessBoardActor* InBoard, chesscore::Color InPlayerColor, EChessDifficulty InDifficulty);
	void Abort();

	/** Try a player move; returns false if it is not the player's turn or the move is illegal. */
	bool TryPlayerMove(const chesscore::ChessMove& Move);
	/** True if From->To is a pawn promotion needing a piece choice. */
	bool NeedsPromotion(chesscore::Square From, chesscore::Square To) const;
	void RequestHint();
	void Resign();

	const chesscore::Game& GetGame() const { return Game; }
	EMatchPhase GetPhase() const { return Phase; }
	chesscore::Color GetPlayerColor() const { return PlayerColor; }
	bool IsPlayerTurn() const { return Phase == EMatchPhase::PlayerTurn; }
	/** Latest evaluation from the player's point of view, centipawns. */
	int32 GetPlayerEval() const { return PlayerPov(WhiteEval); }
	EChessResult GetResult() const { return Result; }
	chesscore::GameStatus GetEndStatus() const { return EndStatus; }
	int32 GetHintsUsed() const { return HintsUsed; }

	FOnMatchReaction OnReaction;
	FOnMatchChanged OnChanged;
	FOnMatchOver OnOver;
	TFunction<void(const chesscore::ChessMove&)> OnHint;

	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override { return Phase == EMatchPhase::OpponentThinking; }
	virtual ETickableTickType GetTickableTickType() const override { return IsTemplate() ? ETickableTickType::Never : ETickableTickType::Conditional; }

	/** Thresholds (centipawns) used to classify the player's move quality. */
	static constexpr int32 BlunderCp = 250;
	static constexpr int32 MistakeCp = 100;
	static constexpr int32 GoodMoveCp = 100;
	static constexpr int32 WinningCp = 300;

	/** Pure classification, exposed for tests: picks the single most salient trigger for a player move. */
	static FString ClassifyPlayerMove(int32 PlayerEvalBefore, int32 PlayerEvalAfter, bool bCapture, bool bCapturedQueen,
		bool bCheck, bool bCastle, bool bPromote);

private:
	void OnPlayerMoveAnimated(const chesscore::ChessMove& Move);
	void OnPlayerMoveAnalyzed(const FChessAnalysis& A, FString MoveTrigger);
	void StartOpponentTurn();
	void ApplyOpponentMove(const chesscore::ChessMove& Move);
	void OnOpponentMoveAnimated(const chesscore::ChessMove& Move);
	void RefreshBoardHighlights();
	bool CheckGameOver();
	void Finish(EChessResult InResult, chesscore::GameStatus Status);
	void Emit(const FString& Trigger);
	int32 PlayerPov(int32 White) const { return PlayerColor == chesscore::Color::White ? White : -White; }

	chesscore::Game Game;
	TWeakObjectPtr<UChessEngineSubsystem> Engine;
	TWeakObjectPtr<AChessBoardActor> Board;
	chesscore::Color PlayerColor = chesscore::Color::White;
	EChessDifficulty Difficulty = EChessDifficulty::Club;
	EMatchPhase Phase = EMatchPhase::Idle;
	EChessResult Result = EChessResult::None;
	chesscore::GameStatus EndStatus = chesscore::GameStatus::Ongoing;

	int32 WhiteEval = 0;
	bool bPlayerWinningAnnounced = false;
	bool bOpponentWinningAnnounced = false;
	int32 HintsUsed = 0;

	TOptional<chesscore::ChessMove> PendingOpponentMove;
	double ThinkStarted = 0.0;
	float MinThinkSeconds = 1.0f;
	uint32 Epoch = 0;
	FRandomStream Random;
};
