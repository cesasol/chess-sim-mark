// SPDX-License-Identifier: Apache-2.0
#include "Chess/ChessMatch.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatchClassifyTest, "ChessSim.Match.ClassifyReaction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMatchClassifyTest::RunTest(const FString&)
{
	auto C = [](int32 Before, int32 After, bool Cap = false, bool Queen = false, bool Check = false, bool Castle = false, bool Promo = false) {
		return UChessMatch::ClassifyPlayerMove(Before, After, Cap, Queen, Check, Castle, Promo);
	};
	TestEqual(TEXT("quiet move"), C(20, 10), FString());
	TestEqual(TEXT("blunder beats everything"), C(50, -300, true, true, true), FString(TEXT("PlayerBlunder")));
	TestEqual(TEXT("mistake"), C(0, -150), FString(TEXT("PlayerMistake")));
	TestEqual(TEXT("good move"), C(0, 180), FString(TEXT("PlayerGoodMove")));
	TestEqual(TEXT("queen capture"), C(0, 900, true, true), FString(TEXT("PlayerCaptureQueen")));
	TestEqual(TEXT("check over capture"), C(0, 0, true, false, true), FString(TEXT("PlayerCheck")));
	TestEqual(TEXT("plain capture"), C(0, 20, true), FString(TEXT("PlayerCapture")));
	TestEqual(TEXT("castle"), C(0, 10, false, false, false, true), FString(TEXT("PlayerCastle")));
	TestEqual(TEXT("promotion"), C(0, 50, false, false, false, false, true), FString(TEXT("PlayerPromote")));
	// Mate scores are clamped: mate-in-5 -> mate-in-3 is not a blunder/brilliancy swing.
	TestEqual(TEXT("mate clamp"), C(9950, 9970), FString());
	TestEqual(TEXT("losing a forced mate is a blunder"), C(9950, 0), FString(TEXT("PlayerBlunder")));
	return true;
}

#endif
