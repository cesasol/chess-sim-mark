// SPDX-License-Identifier: Apache-2.0
#include "Data/CsvReader.h"
#include "Data/GameDataSubsystem.h"
#include "Narrative/ScriptRunner.h"
#include "Engine/GameInstance.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
constexpr EAutomationTestFlags DataTestFlags =
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter;

UGameDataSubsystem* LoadRealData(FAutomationTestBase& Test)
{
	UGameInstance* Outer = NewObject<UGameInstance>(GetTransientPackage());
	Outer->AddToRoot();
	UGameDataSubsystem* Data = NewObject<UGameDataSubsystem>(Outer);
	Test.TestTrue(TEXT("data loads"), Data->LoadFrom(UGameDataSubsystem::DefaultDataDir()));
	for (const FString& E : Data->GetLoadErrors()) Test.AddError(E);
	return Data;
}

/** Plays the script to completion choosing option `Pick` (clamped) and feeding chess results in order. */
FString PlayThrough(FAutomationTestBase& Test, UGameDataSubsystem* Data, const FString& Opponent, int32 Pick,
	TArray<EChessResult> Results, int32& OutChessGames, int32& OutAffection)
{
	Data->SetActiveOpponent(Opponent);
	FScriptRunner Runner;
	Runner.Start(Data);
	OutChessGames = 0;
	for (int32 Steps = 0; Steps < 2000; ++Steps)
	{
		FScriptEvent E = Runner.Advance();
		switch (E.Type)
		{
		case EScriptEventType::Line:
			if (Data->GetLine(E.TextKey).ToString().StartsWith(TEXT("!!")))
				Test.AddError(FString::Printf(TEXT("[%s] missing line %s"), *Opponent, *E.TextKey));
			break;
		case EScriptEventType::Choice:
			Test.TestTrue(TEXT("choose"), Runner.Choose(FMath::Min(Pick, E.Choices.Num() - 1)));
			break;
		case EScriptEventType::Camera:
			break;
		case EScriptEventType::StartChess:
			++OutChessGames;
			Runner.SetChessResult(Results.Num() > 0 ? Results[0] : EChessResult::Win);
			if (Results.Num() > 0) Results.RemoveAt(0);
			break;
		case EScriptEventType::End:
			OutAffection = Runner.GetAffection();
			return E.Arg;
		case EScriptEventType::Error:
			Test.AddError(FString::Printf(TEXT("[%s] script error: %s"), *Opponent, *E.Arg));
			return FString();
		}
	}
	Test.AddError(TEXT("script did not terminate"));
	return FString();
}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCsvParseTest, "ChessSim.Data.Csv", DataTestFlags)

bool FCsvParseTest::RunTest(const FString&)
{
	TArray<Csv::FRow> Rows;
	const FString Text = TEXT("\xFEFFKey , Text\r\nhello,\"Hi, \"\"there\"\"\"\r\n\r\nmulti,\"line one\nline two\"\nshort\n");
	TestTrue(TEXT("parses"), Csv::ParseTable(Text, Rows));
	TestEqual(TEXT("rows (blank skipped)"), Rows.Num(), 3);
	TestEqual(TEXT("quoted comma+quotes"), Csv::Get(Rows[0], TEXT("text")), FString(TEXT("Hi, \"there\"")));
	TestEqual(TEXT("header trimmed/lowercased"), Csv::Get(Rows[0], TEXT("key")), FString(TEXT("hello")));
	TestEqual(TEXT("embedded newline"), Csv::Get(Rows[1], TEXT("text")), FString(TEXT("line one\nline two")));
	TestEqual(TEXT("missing column"), Csv::Get(Rows[2], TEXT("text")), FString());

	TArray<Csv::FRow> Bad;
	FString Err;
	TestFalse(TEXT("unterminated quote"), Csv::ParseTable(TEXT("a,b\n\"oops,1\n"), Bad, &Err));
	TestTrue(TEXT("error message"), Err.Contains(TEXT("unterminated")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGameDataLoadTest, "ChessSim.Data.Load", DataTestFlags)

bool FGameDataLoadTest::RunTest(const FString&)
{
	UGameDataSubsystem* Data = LoadRealData(*this);
	const TArray<const FCharacterDef*> Opps = Data->GetOpponents();
	bool bFemale = false, bMale = false;
	for (const FCharacterDef* C : Opps)
	{
		bFemale |= C->bFemale;
		bMale |= !C->bFemale;
	}
	TestTrue(TEXT("female opponent"), bFemale);
	TestTrue(TEXT("male opponent"), bMale);
	for (const FCharacterDef& C : Data->GetCharacters()) TestTrue(TEXT("adult"), C.Age >= 18);

	TestTrue(TEXT("en available"), Data->GetAvailableCultures().Contains(TEXT("en")));
	Data->SetPlayerName(TEXT("Tester"));
	Data->SetActiveOpponent(TEXT("akane"));
	// Opponent prefix resolution and token substitution.
	TestTrue(TEXT("prefixed line resolves"), Data->HasLine(TEXT("meet.01")));
	const FString Thinking = Data->GetLine(TEXT("ui.thinking")).ToString();
	TestTrue(TEXT("{opponent} substituted"), !Thinking.Contains(TEXT("{opponent}")) && Thinking.Contains(Data->GetCharacterName(TEXT("akane")).ToString()));

	AddExpectedMessage(TEXT("Missing line"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("missing key marker"), Data->GetLine(TEXT("does.not.exist")).ToString().StartsWith(TEXT("!!")));

	if (Data->GetAvailableCultures().Contains(TEXT("es")))
	{
		const FString En = Data->GetLine(TEXT("ui.start")).ToString();
		Data->SetCulture(TEXT("es"));
		const FString Es = Data->GetLine(TEXT("ui.start")).ToString();
		TestNotEqual(TEXT("spanish differs"), Es, En);
		Data->SetCulture(TEXT("en"));
	}

	FRandomStream Random(42);
	for (const FCharacterDef* C : Opps)
	{
		for (const TCHAR* T : {TEXT("GameStart"), TEXT("PlayerBlunder"), TEXT("PlayerWin"), TEXT("OpponentWin"), TEXT("Draw")})
			TestNotNull(*FString::Printf(TEXT("%s reaction %s"), *C->Id, T), Data->PickReaction(C->Id, T, Random));
	}
	Data->GetOuter()->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScriptPlaythroughTest, "ChessSim.Narrative.Playthrough", DataTestFlags)

bool FScriptPlaythroughTest::RunTest(const FString&)
{
	UGameDataSubsystem* Data = LoadRealData(*this);
	for (const FCharacterDef* C : Data->GetOpponents())
	{
		int32 Games = 0, Affection = 0;
		// Best answers, immediate win.
		const FString Best = PlayThrough(*this, Data, C->Id, 0, {EChessResult::Win}, Games, Affection);
		TestEqual(*FString::Printf(TEXT("%s best ending"), *C->Id), Best, FString(TEXT("true_love")));
		TestEqual(TEXT("one game"), Games, 1);

		// Worst answers: lose, draw, then win -> retries must be allowed.
		const FString Worst = PlayThrough(*this, Data, C->Id, 2, {EChessResult::Lose, EChessResult::Draw, EChessResult::Win}, Games, Affection);
		TestFalse(*FString::Printf(TEXT("%s worst ending exists"), *C->Id), Worst.IsEmpty());
		TestEqual(TEXT("three games (retry after loss and draw)"), Games, 3);

		// Every ending has its title/body lines.
		Data->SetActiveOpponent(C->Id);
		for (const FString& Ending : {Best, Worst})
		{
			TestTrue(TEXT("ending title"), Data->HasLine(FString::Printf(TEXT("ending.%s.title"), *Ending)));
			TestTrue(TEXT("ending body"), Data->HasLine(FString::Printf(TEXT("ending.%s.body"), *Ending)));
		}
	}
	Data->GetOuter()->RemoveFromRoot();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScriptConditionTest, "ChessSim.Narrative.Conditions", DataTestFlags)

bool FScriptConditionTest::RunTest(const FString&)
{
	FScriptRunner R;
	TestTrue(TEXT("affection>=0"), R.EvaluateCondition(TEXT("affection>=0")));
	TestFalse(TEXT("affection>=1"), R.EvaluateCondition(TEXT("affection>=1")));
	TestTrue(TEXT("affection<1"), R.EvaluateCondition(TEXT("affection<1")));
	TestTrue(TEXT("!flag"), R.EvaluateCondition(TEXT("!flag:x")));
	R.SetChessResult(EChessResult::Draw);
	TestTrue(TEXT("draw"), R.EvaluateCondition(TEXT("result=draw")));
	TestFalse(TEXT("win"), R.EvaluateCondition(TEXT("result=win")));
	return true;
}

#endif
