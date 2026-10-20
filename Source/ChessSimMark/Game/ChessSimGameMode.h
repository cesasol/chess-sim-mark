// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Chess/ChessCore.h"
#include "Chess/ChessTypes.h"
#include "GameFramework/GameModeBase.h"
#include "Narrative/ScriptRunner.h"
#include "ChessSimGameMode.generated.h"

class ACameraActor;
class AChessBoardActor;
class AOpponentCharacter;
class SChessSimHud;
class UChessEngineSubsystem;
class UChessMatch;
class UGameDataSubsystem;

UENUM()
enum class EGameFlow : uint8
{
	Title,
	Story,
	Chess,
	Ending,
};

/**
 * Orchestrates the MVP loop: title/select → scripted conversation →
 * full chess game with live reactions → relationship outcome. Finds (or
 * spawns) the board, opponent and camera shots in the level, owns the HUD
 * and routes player intents from AChessSimPlayerController.
 */
UCLASS()
class CHESSSIMMARK_API AChessSimGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AChessSimGameMode();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

	// Player intents (from the controller / HUD) ---------------------------
	void Advance();
	void Choose(int32 Index);
	void ClickWorld(const FVector& WorldLocation, bool bHit);
	void Hint();
	void Resign();
	void Promote(chesscore::PieceType Type);
	void BackToTitle();

	EGameFlow GetFlow() const { return Flow; }
	UChessMatch* GetMatch() const { return Match; }

	/** For automated smoke tests: start the story with a given opponent. */
	void StartGame(const FString& OpponentId, EChessDifficulty Difficulty, const FString& PlayerName);

private:
	void SetupScene();
	void CreateHud();
	void ShowTitle();
	void RunScript();
	void HandleLine(const FScriptEvent& E);
	void BlendTo(const FString& Shot, float Seconds = -1.f);
	void StartChess(const FString& ColorArg);
	void OnMatchReaction(const FString& Trigger);
	void OnMatchChanged();
	void OnMatchOver(EChessResult Result, chesscore::GameStatus Status);
	void ResumeAfterChess();
	void ShowEnding(const FString& EndingId);
	void ClearSelection();
	FText StatusText() const;
	FLinearColor AccentFor(const FString& CharacterId) const;
	TOptional<FVector2D> BubbleAnchor() const;

	/** -ChessSimAutoPilot[=opponent]: plays the whole loop unattended, saving screenshots to Saved/AutoPilot. */
	void TickAutoPilot();
	/** Requests a screenshot once per name; returns true if one was requested (autopilot then pauses). */
	bool Shot(const FString& Name);

	UGameDataSubsystem* Data() const;
	UChessEngineSubsystem* Engine() const;

	UPROPERTY(Transient)
	TObjectPtr<AChessBoardActor> Board;
	UPROPERTY(Transient)
	TObjectPtr<AOpponentCharacter> Opponent;
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<ACameraActor>> Shots;
	UPROPERTY(Transient)
	TObjectPtr<UChessMatch> Match;

	TSharedPtr<SChessSimHud> Hud;
	FScriptRunner Runner;
	EGameFlow Flow = EGameFlow::Title;
	FString OpponentId;
	EChessDifficulty Difficulty = EChessDifficulty::Club;
	FString CurrentShot;

	chesscore::Square Selected = chesscore::NoSquare;
	TOptional<chesscore::ChessMove> PendingPromotion;
	double ResignArmedUntil = 0.0;
	double ResumeAt = 0.0;
	double LookAtPlayerUntil = 0.0;
	double TalkUntil = 0.0;
	FRandomStream Random;

	bool bAutoPilot = false;
	FString AutoOpponentId;
	double AutoNext = 0.0;
	int32 AutoPlayerMoves = 0;
	int32 AutoLines = 0;
	int32 AutoGames = 0;
	TSet<FString> AutoShotsTaken;
};
