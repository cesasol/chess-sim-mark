// SPDX-License-Identifier: Apache-2.0
#include "Chess/ChessMatch.h"

#include "ChessSimMark.h"
#include "ChessSimSettings.h"
#include "Chess/ChessBoardActor.h"
#include "Chess/ChessEngineSubsystem.h"
#include "HAL/PlatformTime.h"

using namespace chesscore;

FString UChessMatch::ClassifyPlayerMove(int32 Before, int32 After, bool bCapture, bool bCapturedQueen, bool bCheck, bool bCastle,
	bool bPromote)
{
	// Clamp mate scores so "mate in 3" vs "mate in 5" is not a 20-pawn swing.
	Before = FMath::Clamp(Before, -2000, 2000);
	After = FMath::Clamp(After, -2000, 2000);
	const int32 Delta = After - Before;
	if (Delta <= -BlunderCp) return TEXT("PlayerBlunder");
	if (bCapturedQueen) return TEXT("PlayerCaptureQueen");
	if (bCheck) return TEXT("PlayerCheck");
	if (Delta <= -MistakeCp) return TEXT("PlayerMistake");
	if (bPromote) return TEXT("PlayerPromote");
	if (Delta >= GoodMoveCp) return TEXT("PlayerGoodMove");
	if (bCapture) return TEXT("PlayerCapture");
	if (bCastle) return TEXT("PlayerCastle");
	return FString();
}

void UChessMatch::Begin(UChessEngineSubsystem* InEngine, AChessBoardActor* InBoard, Color InPlayerColor, EChessDifficulty InDifficulty)
{
	Abort();
	Engine = InEngine;
	Board = InBoard;
	PlayerColor = InPlayerColor;
	Difficulty = InDifficulty;
	Game = chesscore::Game();
	Result = EChessResult::None;
	EndStatus = GameStatus::Ongoing;
	WhiteEval = 30; // typical opening edge for White
	bPlayerWinningAnnounced = bOpponentWinningAnnounced = false;
	HintsUsed = 0;
	MinThinkSeconds = GetDefault<UChessSimSettings>()->OpponentMinThinkSeconds;
	Random.Initialize(static_cast<int32>(FPlatformTime::Cycles()));

	if (AChessBoardActor* B = Board.Get())
	{
		B->SetPlayerColor(PlayerColor);
		B->SyncToPosition(Game.Current());
		B->ClearHighlights(false);
	}
	Emit(TEXT("GameStart"));
	if (PlayerColor == Color::White)
	{
		Phase = EMatchPhase::PlayerTurn;
		OnChanged.Broadcast();
	}
	else
	{
		StartOpponentTurn();
	}
}

void UChessMatch::Abort()
{
	++Epoch;
	if (UChessEngineSubsystem* E = Engine.Get()) E->CancelAll();
	PendingOpponentMove.Reset();
	Phase = EMatchPhase::Idle;
}

bool UChessMatch::NeedsPromotion(Square From, Square To) const
{
	const Position& P = Game.Current();
	const Piece& Pc = P.At(From);
	if (Pc.Type != PieceType::Pawn) return false;
	const int32 LastRank = Pc.Side == Color::White ? 7 : 0;
	return RankOf(To) == LastRank && P.IsLegal(ChessMove{From, To, PieceType::Queen});
}

bool UChessMatch::TryPlayerMove(const ChessMove& Move)
{
	if (Phase != EMatchPhase::PlayerTurn) return false;
	const Position Before = Game.Current();
	if (!Before.IsLegal(Move)) return false;

	Phase = EMatchPhase::Animating;
	OnChanged.Broadcast();
	const uint32 MyEpoch = Epoch;
	TWeakObjectPtr<UChessMatch> Self(this);
	auto Done = [Self, MyEpoch, Move]() {
		if (Self.IsValid() && Self->Epoch == MyEpoch) Self->OnPlayerMoveAnimated(Move);
	};
	if (AChessBoardActor* B = Board.Get())
	{
		B->ClearHighlights(true);
		B->AnimateMove(Before, Move, Done);
	}
	else
	{
		Done();
	}
	return true;
}

void UChessMatch::OnPlayerMoveAnimated(const ChessMove& Move)
{
	const Position Before = Game.Current();
	const Piece Captured = Before.At(Move.To);
	const bool bCapture = Before.IsCapture(Move);
	const bool bCastle = Before.At(Move.From).Type == PieceType::King && FMath::Abs(FileOf(Move.To) - FileOf(Move.From)) == 2;
	const bool bPromote = Move.Promotion != PieceType::None;
	Game.Play(Move);
	const bool bCheck = Game.Current().InCheck();
	RefreshBoardHighlights();
	if (CheckGameOver()) return;

	// Board facts are known now; quality needs the analyst. Pre-classify with no swing.
	const FString BoardTrigger = ClassifyPlayerMove(0, 0, bCapture, Captured.Type == PieceType::Queen, bCheck, bCastle, bPromote);
	Phase = EMatchPhase::Analyzing;
	OnChanged.Broadcast();

	UChessEngineSubsystem* E = Engine.Get();
	if (!E)
	{
		Emit(BoardTrigger);
		StartOpponentTurn();
		return;
	}
	const uint32 MyEpoch = Epoch;
	TWeakObjectPtr<UChessMatch> Self(this);
	const int32 Depth = GetDefault<UChessSimSettings>()->AnalysisDepth;
	const bool bQueen = Captured.Type == PieceType::Queen;
	E->RequestAnalysis(Game, Depth, [Self, MyEpoch, bCapture, bQueen, bCheck, bCastle, bPromote](const FChessAnalysis& A) {
		if (!Self.IsValid() || Self->Epoch != MyEpoch) return;
		const int32 Before = Self->GetPlayerEval();
		const int32 After = A.bValid ? Self->PlayerPov(A.WhiteCp) : Before;
		Self->OnPlayerMoveAnalyzed(A, ClassifyPlayerMove(Before, After, bCapture, bQueen, bCheck, bCastle, bPromote));
	});
}

void UChessMatch::OnPlayerMoveAnalyzed(const FChessAnalysis& A, FString MoveTrigger)
{
	if (A.bValid) WhiteEval = A.WhiteCp;
	const int32 Eval = GetPlayerEval();
	if (MoveTrigger.IsEmpty() && Eval >= WinningCp && !bPlayerWinningAnnounced)
	{
		MoveTrigger = TEXT("PlayerWinning");
		bPlayerWinningAnnounced = true;
	}
	if (Eval < WinningCp / 2) bPlayerWinningAnnounced = false;
	Emit(MoveTrigger);
	StartOpponentTurn();
}

void UChessMatch::StartOpponentTurn()
{
	Phase = EMatchPhase::OpponentThinking;
	PendingOpponentMove.Reset();
	ThinkStarted = FPlatformTime::Seconds();
	if (Random.FRand() < 0.2f) Emit(TEXT("Thinking"));
	OnChanged.Broadcast();

	const uint32 MyEpoch = Epoch;
	TWeakObjectPtr<UChessMatch> Self(this);
	if (UChessEngineSubsystem* E = Engine.Get())
	{
		E->RequestOpponentMove(Game, Difficulty, [Self, MyEpoch](const ChessMove& M) {
			if (Self.IsValid() && Self->Epoch == MyEpoch) Self->PendingOpponentMove = M;
		});
	}
	else
	{
		PendingOpponentMove = PickFallbackMove(Game.Current(), 1, 1);
	}
}

void UChessMatch::Tick(float)
{
	if (Phase != EMatchPhase::OpponentThinking || !PendingOpponentMove.IsSet()) return;
	if (FPlatformTime::Seconds() - ThinkStarted < MinThinkSeconds) return;
	const ChessMove M = *PendingOpponentMove;
	PendingOpponentMove.Reset();
	ApplyOpponentMove(M);
}

void UChessMatch::ApplyOpponentMove(const ChessMove& Move)
{
	const Position Before = Game.Current();
	if (!Move.IsValid() || !Before.IsLegal(Move))
	{
		UE_LOG(LogChessSim, Error, TEXT("Opponent produced illegal move %hs; using fallback"), Move.ToUci().c_str());
		const ChessMove Fb = PickFallbackMove(Before, 1, 1);
		if (!Fb.IsValid())
		{
			CheckGameOver();
			return;
		}
		ApplyOpponentMove(Fb);
		return;
	}
	Phase = EMatchPhase::Animating;
	OnChanged.Broadcast();
	const uint32 MyEpoch = Epoch;
	TWeakObjectPtr<UChessMatch> Self(this);
	auto Done = [Self, MyEpoch, Move]() {
		if (Self.IsValid() && Self->Epoch == MyEpoch) Self->OnOpponentMoveAnimated(Move);
	};
	if (AChessBoardActor* B = Board.Get())
	{
		B->ClearHighlights(true);
		B->AnimateMove(Before, Move, Done);
	}
	else
	{
		Done();
	}
}

void UChessMatch::OnOpponentMoveAnimated(const ChessMove& Move)
{
	const bool bCapture = Game.Current().IsCapture(Move);
	Game.Play(Move);
	RefreshBoardHighlights();
	if (CheckGameOver()) return;

	if (Game.Current().InCheck()) Emit(TEXT("OpponentCheck"));
	else if (bCapture) Emit(TEXT("OpponentCapture"));

	Phase = EMatchPhase::PlayerTurn;
	OnChanged.Broadcast();

	// Baseline evaluation for judging the player's next move. Same FIFO analyst,
	// so it always lands before the analysis of the player's reply.
	if (UChessEngineSubsystem* E = Engine.Get())
	{
		const uint32 MyEpoch = Epoch;
		TWeakObjectPtr<UChessMatch> Self(this);
		E->RequestAnalysis(Game, GetDefault<UChessSimSettings>()->AnalysisDepth, [Self, MyEpoch](const FChessAnalysis& A) {
			if (!Self.IsValid() || Self->Epoch != MyEpoch || !A.bValid) return;
			Self->WhiteEval = A.WhiteCp;
			const int32 Eval = Self->GetPlayerEval();
			if (Eval <= -WinningCp && !Self->bOpponentWinningAnnounced)
			{
				Self->bOpponentWinningAnnounced = true;
				Self->Emit(TEXT("OpponentWinning"));
			}
			if (Eval > -WinningCp / 2) Self->bOpponentWinningAnnounced = false;
			Self->OnChanged.Broadcast();
		});
	}
}

void UChessMatch::RefreshBoardHighlights()
{
	AChessBoardActor* B = Board.Get();
	if (!B || Game.Moves().empty()) return;
	B->SetLastMove(Game.Moves().back());
	if (Game.Current().InCheck()) B->SetCheck(Game.Current().KingSquare(Game.Current().SideToMove()));
}

bool UChessMatch::CheckGameOver()
{
	const GameStatus S = Game.Status();
	if (S == GameStatus::Ongoing) return false;
	if (S == GameStatus::Checkmate)
	{
		const std::optional<Color> W = Game.Winner();
		Finish(W && *W == PlayerColor ? EChessResult::Win : EChessResult::Lose, S);
	}
	else
	{
		Finish(EChessResult::Draw, S);
	}
	return true;
}

void UChessMatch::Finish(EChessResult InResult, GameStatus Status)
{
	++Epoch;
	if (UChessEngineSubsystem* E = Engine.Get()) E->CancelAll();
	Phase = EMatchPhase::Over;
	Result = InResult;
	EndStatus = Status;
	Emit(InResult == EChessResult::Win ? TEXT("PlayerWin") : InResult == EChessResult::Lose ? TEXT("OpponentWin") : TEXT("Draw"));
	OnChanged.Broadcast();
	OnOver.Broadcast(Result, Status);
}

void UChessMatch::Resign()
{
	if (Phase == EMatchPhase::Over || Phase == EMatchPhase::Idle) return;
	Finish(EChessResult::Lose, GameStatus::Ongoing);
}

void UChessMatch::RequestHint()
{
	if (Phase != EMatchPhase::PlayerTurn) return;
	UChessEngineSubsystem* E = Engine.Get();
	if (!E) return;
	++HintsUsed;
	Emit(TEXT("Hint"));
	const uint32 MyEpoch = Epoch;
	TWeakObjectPtr<UChessMatch> Self(this);
	E->RequestAnalysis(Game, GetDefault<UChessSimSettings>()->AnalysisDepth, [Self, MyEpoch](const FChessAnalysis& A) {
		if (!Self.IsValid() || Self->Epoch != MyEpoch || !A.BestMove.IsValid()) return;
		if (Self->Phase != EMatchPhase::PlayerTurn) return;
		if (AChessBoardActor* B = Self->Board.Get()) B->ShowHint(A.BestMove);
		if (Self->OnHint) Self->OnHint(A.BestMove);
	});
}

void UChessMatch::Emit(const FString& Trigger)
{
	if (!Trigger.IsEmpty()) OnReaction.Broadcast(Trigger);
}

TStatId UChessMatch::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UChessMatch, STATGROUP_Tickables); }
