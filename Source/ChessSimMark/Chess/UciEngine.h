// SPDX-License-Identifier: Apache-2.0
//
// Out-of-process UCI chess engine client (Stockfish).
//
// Licensing: Stockfish is GPLv3. It is never linked into the game; it runs as
// a separate executable and we talk to it over stdin/stdout using the public
// UCI text protocol. See docs/decisions/0001-stockfish-out-of-process.md.
#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"

/** Parsed `info` line. Scores are from the side to move. */
struct FUciInfo
{
	int32 Depth = 0;
	bool bHasScore = false;
	bool bIsMate = false;
	int32 ScoreCp = 0;  // valid when !bIsMate
	int32 MateIn = 0;   // moves; negative = side to move gets mated
	int32 MultiPv = 1;
	bool bBound = false; // lowerbound/upperbound
	TArray<FString> Pv;
};

namespace Uci
{
CHESSSIMMARK_API bool ParseInfo(const FString& Line, FUciInfo& Out);
CHESSSIMMARK_API bool ParseBestMove(const FString& Line, FString& OutBest, FString& OutPonder);
/** Maps a score to centipawns, mate scores clamped to +-(10000 - plies). */
CHESSSIMMARK_API int32 ScoreToCp(const FUciInfo& Info);
}

struct FEngineSearchParams
{
	/** FEN of the starting position. */
	FString Fen;
	/** Space-separated UCI moves applied after Fen. */
	FString Moves;
	/** Stockfish "Skill Level" 0..20. */
	int32 SkillLevel = 20;
	/** 0 = unlimited. At least one of Depth / MoveTimeMs must be set. */
	int32 Depth = 0;
	int32 MoveTimeMs = 0;
};

struct FEngineSearchResult
{
	bool bSuccess = false;
	FString BestMove;
	FString PonderMove;
	/** Last principal-variation score, side to move. */
	FUciInfo Info;
	double ElapsedSeconds = 0.0;
};

DECLARE_DELEGATE_OneParam(FOnEngineSearchDone, const FEngineSearchResult&);

/**
 * One engine process. Not thread-safe: create, Tick and destroy on the game
 * thread. All IO is non-blocking (pipes are polled from Tick).
 */
class CHESSSIMMARK_API FUciEngine
{
public:
	FUciEngine(const FString& InName);
	~FUciEngine();

	FUciEngine(const FUciEngine&) = delete;
	FUciEngine& operator=(const FUciEngine&) = delete;

	bool Start(const FString& ExecutablePath);
	void Shutdown();

	bool IsRunning() const;
	/** True once the engine has answered `uciok`. */
	bool IsHandshakeDone() const { return bUciOk; }
	/** True if the process failed to start, died, or never completed the handshake. */
	bool HasFailed() const { return bFailed; }
	const FString& GetEngineId() const { return EngineId; }

	/** Queue a search. The callback runs on the game thread from Tick. Returns a request id. */
	uint32 Search(const FEngineSearchParams& Params, FOnEngineSearchDone OnDone);
	/** Drop all queued requests and stop the active search without invoking callbacks. */
	void CancelAll();
	bool IsBusy() const { return Active.IsSet() || Queue.Num() > 0; }

	void Tick();

	/** Seconds to wait for `uciok` before declaring the engine broken. */
	double HandshakeTimeout = 5.0;
	/** Extra seconds beyond the search budget before a search is aborted. */
	double SearchGrace = 10.0;

private:
	struct FPending
	{
		uint32 Id = 0;
		FEngineSearchParams Params;
		FOnEngineSearchDone OnDone;
		bool bDiscard = false;
	};

	void Send(const FString& Line);
	void HandleLine(const FString& Line);
	void StartNext();
	void FinishActive(bool bSuccess, const FString& Best, const FString& Ponder);
	void Fail(const FString& Why);

	FString Name;
	FString EngineId;
	FProcHandle Proc;
	void* StdoutRead = nullptr;
	void* StdoutWrite = nullptr;
	void* StdinRead = nullptr;
	void* StdinWrite = nullptr;
	FString Partial;

	bool bUciOk = false;
	bool bFailed = false;
	double LaunchTime = 0.0;

	TArray<FPending> Queue;
	TOptional<FPending> Active;
	FUciInfo ActiveInfo;
	double ActiveStart = 0.0;
	bool bStopSent = false;
	int32 CurrentSkill = -1;
	uint32 NextId = 1;
};
