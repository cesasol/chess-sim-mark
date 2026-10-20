// SPDX-License-Identifier: Apache-2.0
#include "Chess/ChessEngineSubsystem.h"
#include "Chess/UciEngine.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUciParseTest, "ChessSim.Uci.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FUciParseTest::RunTest(const FString&)
{
	FUciInfo I;
	TestTrue(TEXT("info cp"), Uci::ParseInfo(TEXT("info depth 12 seldepth 18 multipv 1 score cp -34 nodes 1000 nps 1 pv e2e4 e7e5"), I));
	TestEqual(TEXT("depth"), I.Depth, 12);
	TestEqual(TEXT("cp"), I.ScoreCp, -34);
	TestFalse(TEXT("not mate"), I.bIsMate);
	TestEqual(TEXT("pv"), I.Pv.Num(), 2);

	FUciInfo M;
	TestTrue(TEXT("info mate"), Uci::ParseInfo(TEXT("info depth 5 score mate -2 pv a1a2"), M));
	TestTrue(TEXT("mate"), M.bIsMate);
	TestEqual(TEXT("mate in"), M.MateIn, -2);
	TestTrue(TEXT("mate score negative"), Uci::ScoreToCp(M) < -9000);

	FUciInfo B;
	TestTrue(TEXT("bound"), Uci::ParseInfo(TEXT("info depth 9 score cp 20 lowerbound"), B));
	TestTrue(TEXT("bound flag"), B.bBound);

	FUciInfo S;
	TestFalse(TEXT("info string ignored"), Uci::ParseInfo(TEXT("info string NNUE evaluation using nn.nnue"), S));

	FString Best, Ponder;
	TestTrue(TEXT("bestmove"), Uci::ParseBestMove(TEXT("bestmove e2e4 ponder e7e5"), Best, Ponder));
	TestEqual(TEXT("best"), Best, FString(TEXT("e2e4")));
	TestEqual(TEXT("ponder"), Ponder, FString(TEXT("e7e5")));
	TestFalse(TEXT("not bestmove"), Uci::ParseBestMove(TEXT("readyok"), Best, Ponder));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUciLiveStockfishTest, "ChessSim.Uci.LiveStockfish",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FUciLiveStockfishTest::RunTest(const FString&)
{
	const FString Path = UChessEngineSubsystem::FindStockfish();
	if (Path.IsEmpty())
	{
		AddWarning(TEXT("Stockfish not installed; skipping live test"));
		return true;
	}
	FUciEngine Engine(TEXT("test"));
	TestTrue(TEXT("launch"), Engine.Start(Path));

	// Mate in one for White: Ra8#.
	FEngineSearchParams P;
	P.Fen = TEXT("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
	P.Depth = 6;
	bool bDone = false;
	FEngineSearchResult Result;
	Engine.Search(P, FOnEngineSearchDone::CreateLambda([&](const FEngineSearchResult& R) { Result = R; bDone = true; }));

	const double Deadline = FPlatformTime::Seconds() + 15.0;
	while (!bDone && FPlatformTime::Seconds() < Deadline)
	{
		Engine.Tick();
		FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("search finished"), bDone);
	TestTrue(TEXT("handshake"), Engine.IsHandshakeDone());
	TestTrue(TEXT("success"), Result.bSuccess);
	TestEqual(TEXT("finds mate"), Result.BestMove, FString(TEXT("a1a8")));
	TestTrue(TEXT("mate score"), Result.Info.bIsMate && Result.Info.MateIn == 1);
	AddInfo(FString::Printf(TEXT("%s answered in %.3fs"), *Engine.GetEngineId(), Result.ElapsedSeconds));

	// A second search on the same process must also work (queue/state reset).
	bDone = false;
	P.Fen = TEXT("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
	P.Moves = TEXT("e2e4");
	P.SkillLevel = 0;
	P.Depth = 3;
	Engine.Search(P, FOnEngineSearchDone::CreateLambda([&](const FEngineSearchResult& R) { Result = R; bDone = true; }));
	while (!bDone && FPlatformTime::Seconds() < Deadline + 10.0)
	{
		Engine.Tick();
		FPlatformProcess::Sleep(0.01f);
	}
	TestTrue(TEXT("second search"), bDone && Result.bSuccess && Result.BestMove.Len() >= 4);
	Engine.Shutdown();
	TestFalse(TEXT("stopped"), Engine.IsRunning());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUciMissingBinaryTest, "ChessSim.Uci.MissingBinary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FUciMissingBinaryTest::RunTest(const FString&)
{
	FUciEngine Engine(TEXT("missing"));
	AddExpectedMessage(TEXT("UCI engine unavailable"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	Engine.Start(TEXT("/nonexistent/stockfish"));
	bool bCalled = false, bOk = true;
	FEngineSearchParams P;
	P.Fen = TEXT("6k1/5ppp/8/8/8/8/8/R5K1 w - - 0 1");
	P.Depth = 1;
	Engine.Search(P, FOnEngineSearchDone::CreateLambda([&](const FEngineSearchResult& R) { bCalled = true; bOk = R.bSuccess; }));
	Engine.Tick();
	TestTrue(TEXT("failed engine reports"), bCalled);
	TestFalse(TEXT("failure result"), bOk);
	return true;
}

#endif
