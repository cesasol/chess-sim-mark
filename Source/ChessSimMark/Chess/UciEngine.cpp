// SPDX-License-Identifier: Apache-2.0
#include "Chess/UciEngine.h"

#include "ChessSimMark.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

namespace Uci
{
bool ParseInfo(const FString& Line, FUciInfo& Out)
{
	TArray<FString> Tok;
	Line.ParseIntoArrayWS(Tok);
	if (Tok.Num() == 0 || Tok[0] != TEXT("info")) return false;

	FUciInfo Info;
	bool bAny = false;
	for (int32 I = 1; I < Tok.Num(); ++I)
	{
		const FString& T = Tok[I];
		if (T == TEXT("depth") && I + 1 < Tok.Num())
		{
			Info.Depth = FCString::Atoi(*Tok[++I]);
			bAny = true;
		}
		else if (T == TEXT("multipv") && I + 1 < Tok.Num())
		{
			Info.MultiPv = FCString::Atoi(*Tok[++I]);
		}
		else if (T == TEXT("score") && I + 2 < Tok.Num())
		{
			Info.bHasScore = true;
			Info.bIsMate = Tok[I + 1] == TEXT("mate");
			const int32 V = FCString::Atoi(*Tok[I + 2]);
			if (Info.bIsMate) Info.MateIn = V;
			else Info.ScoreCp = V;
			I += 2;
			if (I + 1 < Tok.Num() && (Tok[I + 1] == TEXT("lowerbound") || Tok[I + 1] == TEXT("upperbound")))
			{
				Info.bBound = true;
				++I;
			}
		}
		else if (T == TEXT("pv"))
		{
			for (++I; I < Tok.Num(); ++I) Info.Pv.Add(Tok[I]);
		}
		else if (T == TEXT("string"))
		{
			// Free text until end of line; not a search report.
			return false;
		}
	}
	if (!bAny && !Info.bHasScore) return false;
	Out = MoveTemp(Info);
	return true;
}

bool ParseBestMove(const FString& Line, FString& OutBest, FString& OutPonder)
{
	TArray<FString> Tok;
	Line.ParseIntoArrayWS(Tok);
	if (Tok.Num() < 2 || Tok[0] != TEXT("bestmove")) return false;
	OutBest = Tok[1];
	OutPonder = (Tok.Num() >= 4 && Tok[2] == TEXT("ponder")) ? Tok[3] : FString();
	return true;
}

int32 ScoreToCp(const FUciInfo& Info)
{
	if (!Info.bHasScore) return 0;
	if (!Info.bIsMate) return Info.ScoreCp;
	if (Info.MateIn == 0) return -10000; // side to move is mated
	const int32 Sign = Info.MateIn > 0 ? 1 : -1;
	return Sign * (10000 - FMath::Abs(Info.MateIn) * 10);
}
} // namespace Uci

FUciEngine::FUciEngine(const FString& InName) : Name(InName) {}

FUciEngine::~FUciEngine() { Shutdown(); }

bool FUciEngine::Start(const FString& ExecutablePath)
{
	Shutdown();
	bFailed = false;
	bUciOk = false;
	Partial.Reset();

	if (!FPaths::FileExists(ExecutablePath))
	{
		Fail(FString::Printf(TEXT("could not launch '%s' (file not found)"), *ExecutablePath));
		return false;
	}
	if (!FPlatformProcess::CreatePipe(StdoutRead, StdoutWrite) ||
		!FPlatformProcess::CreatePipe(StdinRead, StdinWrite, /*bWritePipeLocal*/ true))
	{
		Fail(TEXT("could not create pipes"));
		return false;
	}

	Proc = FPlatformProcess::CreateProc(*ExecutablePath, TEXT(""), /*bLaunchDetached*/ false, /*bLaunchHidden*/ true,
		/*bLaunchReallyHidden*/ true, nullptr, 0, nullptr, StdoutWrite, StdinRead);
	if (!Proc.IsValid())
	{
		Fail(FString::Printf(TEXT("could not launch '%s'"), *ExecutablePath));
		return false;
	}

	LaunchTime = FPlatformTime::Seconds();
	UE_LOG(LogChessSim, Log, TEXT("[%s] launched UCI engine %s"), *Name, *ExecutablePath);
	Send(TEXT("uci"));
	return true;
}

void FUciEngine::Shutdown()
{
	if (Proc.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(Proc))
		{
			Send(TEXT("quit"));
			const double Deadline = FPlatformTime::Seconds() + 0.5;
			while (FPlatformProcess::IsProcRunning(Proc) && FPlatformTime::Seconds() < Deadline)
			{
				FPlatformProcess::Sleep(0.01f);
			}
			if (FPlatformProcess::IsProcRunning(Proc)) FPlatformProcess::TerminateProc(Proc, true);
		}
		FPlatformProcess::CloseProc(Proc);
	}
	Proc.Reset();
	if (StdoutRead || StdoutWrite) FPlatformProcess::ClosePipe(StdoutRead, StdoutWrite);
	if (StdinRead || StdinWrite) FPlatformProcess::ClosePipe(StdinRead, StdinWrite);
	StdoutRead = StdoutWrite = StdinRead = StdinWrite = nullptr;
	bUciOk = false;
	Queue.Reset();
	Active.Reset();
	CurrentSkill = -1;
}

bool FUciEngine::IsRunning() const
{
	FProcHandle Copy = Proc;
	return Proc.IsValid() && FPlatformProcess::IsProcRunning(Copy);
}

uint32 FUciEngine::Search(const FEngineSearchParams& Params, FOnEngineSearchDone OnDone)
{
	FPending P;
	P.Id = NextId++;
	P.Params = Params;
	P.OnDone = MoveTemp(OnDone);
	const uint32 Id = P.Id;
	Queue.Add(MoveTemp(P));
	if (bFailed)
	{
		// Report the failure asynchronously-ish (next Tick) so callers see a consistent flow.
		return Id;
	}
	StartNext();
	return Id;
}

void FUciEngine::CancelAll()
{
	Queue.Reset();
	if (Active.IsSet())
	{
		Active->bDiscard = true;
		if (!bStopSent)
		{
			Send(TEXT("stop"));
			bStopSent = true;
		}
	}
}

void FUciEngine::Send(const FString& Line)
{
	if (!StdinWrite || !IsRunning()) return;
	UE_LOG(LogChessSim, VeryVerbose, TEXT("[%s] > %s"), *Name, *Line);
	FPlatformProcess::WritePipe(StdinWrite, Line);
}

void FUciEngine::Tick()
{
	if (bFailed)
	{
		// Drain requests with failures so callers can fall back.
		TArray<FPending> Pending = MoveTemp(Queue);
		Queue.Reset();
		if (Active.IsSet())
		{
			Pending.Insert(MoveTemp(*Active), 0);
			Active.Reset();
		}
		for (FPending& P : Pending)
		{
			if (!P.bDiscard)
			{
				FEngineSearchResult R;
				P.OnDone.ExecuteIfBound(R);
			}
		}
		return;
	}
	if (!Proc.IsValid()) return;

	// Read whatever is available and split into complete lines; keep the tail.
	for (int32 Guard = 0; Guard < 64; ++Guard)
	{
		const FString Chunk = FPlatformProcess::ReadPipe(StdoutRead);
		if (Chunk.IsEmpty()) break;
		Partial += Chunk;
	}
	int32 NewlineIdx;
	while (Partial.FindChar(TEXT('\n'), NewlineIdx))
	{
		FString Line = Partial.Left(NewlineIdx);
		Partial.RightChopInline(NewlineIdx + 1);
		Line.TrimEndInline();
		if (!Line.IsEmpty()) HandleLine(Line);
	}

	const double Now = FPlatformTime::Seconds();
	if (!bUciOk && Now - LaunchTime > HandshakeTimeout)
	{
		Fail(TEXT("no 'uciok' from engine (is it a UCI engine?)"));
		return;
	}
	if (!IsRunning())
	{
		Fail(TEXT("engine process exited"));
		return;
	}
	if (Active.IsSet())
	{
		const FEngineSearchParams& P = Active->Params;
		const double Budget = (P.MoveTimeMs > 0 ? P.MoveTimeMs / 1000.0 : 5.0) + SearchGrace;
		if (Now - ActiveStart > Budget)
		{
			if (!bStopSent)
			{
				UE_LOG(LogChessSim, Warning, TEXT("[%s] search overran its budget, sending stop"), *Name);
				Send(TEXT("stop"));
				bStopSent = true;
				ActiveStart = Now - Budget + 2.0; // give it two more seconds
			}
			else
			{
				Fail(TEXT("engine did not answer 'stop'"));
			}
		}
	}
}

void FUciEngine::HandleLine(const FString& Line)
{
	UE_LOG(LogChessSim, VeryVerbose, TEXT("[%s] < %s"), *Name, *Line);
	if (!bUciOk)
	{
		if (Line.StartsWith(TEXT("id name ")))
		{
			EngineId = Line.RightChop(8);
		}
		else if (Line == TEXT("uciok"))
		{
			bUciOk = true;
			UE_LOG(LogChessSim, Log, TEXT("[%s] handshake ok: %s"), *Name, *EngineId);
			Send(TEXT("setoption name Threads value 2"));
			Send(TEXT("setoption name Hash value 32"));
			Send(TEXT("isready"));
			StartNext();
		}
		return;
	}

	if (!Active.IsSet()) return;

	FUciInfo Info;
	if (Uci::ParseInfo(Line, Info))
	{
		if (Info.bHasScore && Info.MultiPv == 1 && !Info.bBound) ActiveInfo = MoveTemp(Info);
		return;
	}
	FString Best, Ponder;
	if (Uci::ParseBestMove(Line, Best, Ponder))
	{
		const bool bOk = Best != TEXT("(none)") && Best != TEXT("0000");
		FinishActive(bOk, Best, Ponder);
	}
}

void FUciEngine::StartNext()
{
	if (!bUciOk || bFailed || Active.IsSet() || Queue.Num() == 0) return;

	Active = MoveTemp(Queue[0]);
	Queue.RemoveAt(0);
	ActiveInfo = FUciInfo();
	bStopSent = false;
	ActiveStart = FPlatformTime::Seconds();

	const FEngineSearchParams& P = Active->Params;
	const int32 Skill = FMath::Clamp(P.SkillLevel, 0, 20);
	if (Skill != CurrentSkill)
	{
		Send(FString::Printf(TEXT("setoption name Skill Level value %d"), Skill));
		CurrentSkill = Skill;
	}
	FString PositionCmd = FString::Printf(TEXT("position fen %s"), *P.Fen);
	if (!P.Moves.IsEmpty()) PositionCmd += TEXT(" moves ") + P.Moves;
	Send(PositionCmd);

	FString Go = TEXT("go");
	if (P.Depth > 0) Go += FString::Printf(TEXT(" depth %d"), P.Depth);
	if (P.MoveTimeMs > 0) Go += FString::Printf(TEXT(" movetime %d"), P.MoveTimeMs);
	if (P.Depth <= 0 && P.MoveTimeMs <= 0) Go += TEXT(" movetime 1000");
	Send(Go);
}

void FUciEngine::FinishActive(bool bSuccess, const FString& Best, const FString& Ponder)
{
	if (!Active.IsSet()) return;
	FPending Done = MoveTemp(*Active);
	Active.Reset();

	FEngineSearchResult R;
	R.bSuccess = bSuccess;
	R.BestMove = Best;
	R.PonderMove = Ponder;
	R.Info = ActiveInfo;
	R.ElapsedSeconds = FPlatformTime::Seconds() - ActiveStart;

	StartNext();
	if (!Done.bDiscard) Done.OnDone.ExecuteIfBound(R);
}

void FUciEngine::Fail(const FString& Why)
{
	if (!bFailed) UE_LOG(LogChessSim, Warning, TEXT("[%s] UCI engine unavailable: %s"), *Name, *Why);
	bFailed = true;
	if (Proc.IsValid())
	{
		if (FPlatformProcess::IsProcRunning(Proc)) FPlatformProcess::TerminateProc(Proc, true);
		FPlatformProcess::CloseProc(Proc);
		Proc.Reset();
	}
}
