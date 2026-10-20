// SPDX-License-Identifier: Apache-2.0
#include "Chess/ChessEngineSubsystem.h"

#include "ChessSimMark.h"
#include "ChessSimSettings.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"

#if PLATFORM_LINUX || PLATFORM_MAC
#include <sys/stat.h>
#endif

FDifficultyPreset ChessDifficulty::GetPreset(EChessDifficulty D)
{
	FDifficultyPreset P;
	switch (D)
	{
	case EChessDifficulty::Novice: P.SkillLevel = 0; P.Depth = 1; P.RandomMoveChance = 0.35f; P.FallbackDepth = 0; break;
	case EChessDifficulty::Casual: P.SkillLevel = 2; P.Depth = 3; P.RandomMoveChance = 0.1f; P.FallbackDepth = 1; break;
	case EChessDifficulty::Club: P.SkillLevel = 6; P.Depth = 6; P.MoveTimeMs = 400; P.FallbackDepth = 2; break;
	case EChessDifficulty::Strong: P.SkillLevel = 12; P.MoveTimeMs = 600; P.FallbackDepth = 2; break;
	case EChessDifficulty::Champion: P.SkillLevel = 20; P.MoveTimeMs = 1200; P.FallbackDepth = 2; break;
	}
	return P;
}

FString ChessDifficulty::ToString(EChessDifficulty D)
{
	switch (D)
	{
	case EChessDifficulty::Novice: return TEXT("Novice");
	case EChessDifficulty::Casual: return TEXT("Casual");
	case EChessDifficulty::Club: return TEXT("Club");
	case EChessDifficulty::Strong: return TEXT("Strong");
	case EChessDifficulty::Champion: return TEXT("Champion");
	}
	return TEXT("Club");
}

bool ChessDifficulty::FromString(const FString& S, EChessDifficulty& Out)
{
	for (EChessDifficulty D : {EChessDifficulty::Novice, EChessDifficulty::Casual, EChessDifficulty::Club, EChessDifficulty::Strong,
			 EChessDifficulty::Champion})
	{
		if (S.Equals(ToString(D), ESearchCase::IgnoreCase))
		{
			Out = D;
			return true;
		}
	}
	return false;
}

namespace
{
FString ToFString(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

bool IsExecutableFile(const FString& Path)
{
	if (Path.IsEmpty() || !FPaths::FileExists(Path)) return false;
#if PLATFORM_LINUX || PLATFORM_MAC
	// Staging can drop the executable bit; restore it for our own bundled copy.
	struct stat St;
	const auto Utf8 = StringCast<UTF8CHAR>(*Path);
	if (stat(reinterpret_cast<const char*>(Utf8.Get()), &St) == 0 && (St.st_mode & S_IXUSR) == 0)
	{
		chmod(reinterpret_cast<const char*>(Utf8.Get()), St.st_mode | S_IXUSR | S_IXGRP | S_IXOTH);
	}
#endif
	return true;
}
} // namespace

FString UChessEngineSubsystem::FindStockfish()
{
	const FString Exe = PLATFORM_WINDOWS ? TEXT("stockfish.exe") : TEXT("stockfish");

	const FString Env = FPlatformMisc::GetEnvironmentVariable(TEXT("CHESSSIM_STOCKFISH"));
	if (IsExecutableFile(Env)) return Env;

	const UChessSimSettings* Settings = GetDefault<UChessSimSettings>();
	if (Settings && !Settings->StockfishPath.IsEmpty())
	{
		FString P = Settings->StockfishPath;
		if (FPaths::IsRelative(P)) P = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), P);
		if (IsExecutableFile(P)) return P;
	}

	const FString Bundled = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectContentDir() / TEXT("ThirdParty/Stockfish") / FPlatformProperties::IniPlatformName() / Exe);
	if (IsExecutableFile(Bundled)) return Bundled;

	TArray<FString> Dirs;
	FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")).ParseIntoArray(Dirs, PLATFORM_WINDOWS ? TEXT(";") : TEXT(":"));
	for (const FString& Dir : Dirs)
	{
		const FString Candidate = Dir / Exe;
		if (IsExecutableFile(Candidate)) return Candidate;
	}
	return FString();
}

void UChessEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Random.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
	StartEngines();
}

void UChessEngineSubsystem::Deinitialize()
{
	CancelAll();
	Opponent.Reset();
	Analyst.Reset();
	Super::Deinitialize();
}

void UChessEngineSubsystem::StartEngines()
{
	StockfishPath = FindStockfish();
	Opponent = MakeUnique<FUciEngine>(TEXT("opponent"));
	Analyst = MakeUnique<FUciEngine>(TEXT("analyst"));
	if (StockfishPath.IsEmpty())
	{
		UE_LOG(LogChessSim, Warning, TEXT("Stockfish not found; using built-in fallback AI. Set CHESSSIM_STOCKFISH or run `just fetch-stockfish`."));
		return;
	}
	Opponent->Start(StockfishPath);
	Analyst->Start(StockfishPath);
}

bool UChessEngineSubsystem::IsStockfishAvailable() const
{
	return Opponent && !StockfishPath.IsEmpty() && !Opponent->HasFailed();
}

FString UChessEngineSubsystem::GetEngineDescription() const
{
	if (!IsStockfishAvailable()) return TEXT("Built-in fallback AI");
	return Opponent->GetEngineId().IsEmpty() ? TEXT("Stockfish") : Opponent->GetEngineId();
}

TStatId UChessEngineSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UChessEngineSubsystem, STATGROUP_Tickables);
}

void UChessEngineSubsystem::Tick(float)
{
	if (Opponent) Opponent->Tick();
	if (Analyst) Analyst->Tick();
	if (Deferred.Num() > 0)
	{
		TArray<TFunction<void()>> Run = MoveTemp(Deferred);
		Deferred.Reset();
		for (TFunction<void()>& F : Run) F();
	}
}

FEngineSearchParams UChessEngineSubsystem::MakeParams(const chesscore::Game& Game) const
{
	FEngineSearchParams P;
	P.Fen = ToFString(Game.StartFen());
	P.Moves = ToFString(Game.UciMoveList());
	return P;
}

void UChessEngineSubsystem::CancelAll()
{
	++Generation;
	Deferred.Reset();
	if (Opponent) Opponent->CancelAll();
	if (Analyst) Analyst->CancelAll();
}

void UChessEngineSubsystem::RequestOpponentMove(const chesscore::Game& Game, EChessDifficulty Difficulty, FMoveCallback OnMove)
{
	const FDifficultyPreset Preset = ChessDifficulty::GetPreset(Difficulty);
	const chesscore::Position Pos = Game.Current();
	const uint32 Gen = Generation;
	const uint32 Seed = static_cast<uint32>(Random.GetUnsignedInt());

	auto Fallback = [this, Pos, Preset, Seed, Gen, OnMove]() {
		if (Gen != Generation) return;
		OnMove(chesscore::PickFallbackMove(Pos, Preset.FallbackDepth, Seed));
	};

	// Occasional random move keeps the lowest difficulties beatable by beginners.
	if (Preset.RandomMoveChance > 0.f && Random.FRand() < Preset.RandomMoveChance)
	{
		const std::vector<chesscore::ChessMove> Moves = Pos.LegalMoves();
		if (!Moves.empty())
		{
			const chesscore::ChessMove M = Moves[Random.RandHelper(static_cast<int32>(Moves.size()))];
			Deferred.Add([this, M, Gen, OnMove]() { if (Gen == Generation) OnMove(M); });
			return;
		}
	}

	if (!IsStockfishAvailable())
	{
		Deferred.Add(Fallback);
		return;
	}

	FEngineSearchParams P = MakeParams(Game);
	P.SkillLevel = Preset.SkillLevel;
	P.Depth = Preset.Depth;
	P.MoveTimeMs = Preset.MoveTimeMs;
	Opponent->Search(P, FOnEngineSearchDone::CreateLambda([this, Pos, Gen, OnMove, Fallback](const FEngineSearchResult& R) {
		if (Gen != Generation) return;
		if (R.bSuccess)
		{
			const std::optional<chesscore::ChessMove> M = chesscore::ChessMove::FromUciString(TCHAR_TO_UTF8(*R.BestMove));
			if (M && Pos.IsLegal(*M))
			{
				OnMove(*M);
				return;
			}
			UE_LOG(LogChessSim, Warning, TEXT("Engine returned unusable move '%s'; falling back"), *R.BestMove);
		}
		Fallback();
	}));
}

void UChessEngineSubsystem::RequestAnalysis(const chesscore::Game& Game, int32 Depth, FAnalysisCallback OnDone)
{
	const chesscore::Position Pos = Game.Current();
	const uint32 Gen = Generation;
	const bool bWhiteToMove = Pos.SideToMove() == chesscore::Color::White;

	auto Fallback = [this, Pos, Gen, OnDone, bWhiteToMove]() {
		if (Gen != Generation) return;
		FChessAnalysis A;
		A.bValid = true;
		A.WhiteCp = chesscore::Evaluate(Pos);
		if (Pos.LegalMoves().empty() && Pos.InCheck()) A.WhiteCp = bWhiteToMove ? -10000 : 10000;
		A.BestMove = chesscore::PickFallbackMove(Pos, 1, 1);
		OnDone(A);
	};

	if (!IsStockfishAvailable())
	{
		Deferred.Add(Fallback);
		return;
	}

	FEngineSearchParams P = MakeParams(Game);
	P.SkillLevel = 20;
	P.Depth = FMath::Max(1, Depth);
	P.MoveTimeMs = 0;
	Analyst->Search(P, FOnEngineSearchDone::CreateLambda([this, Pos, Gen, OnDone, Fallback, bWhiteToMove](const FEngineSearchResult& R) {
		if (Gen != Generation) return;
		if (!R.Info.bHasScore && !R.bSuccess)
		{
			// Terminal positions report "bestmove (none)" with a mate-0 / cp-0 score;
			// anything without a score means the engine failed.
			Fallback();
			return;
		}
		FChessAnalysis A;
		A.bValid = true;
		A.bFromStockfish = true;
		A.bMate = R.Info.bIsMate;
		A.MateIn = R.Info.MateIn;
		const int32 SideCp = Uci::ScoreToCp(R.Info);
		A.WhiteCp = bWhiteToMove ? SideCp : -SideCp;
		if (R.bSuccess)
		{
			if (const std::optional<chesscore::ChessMove> M = chesscore::ChessMove::FromUciString(TCHAR_TO_UTF8(*R.BestMove)))
				A.BestMove = *M;
		}
		OnDone(A);
	}));
}
