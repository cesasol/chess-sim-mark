// SPDX-License-Identifier: Apache-2.0
#include "Narrative/ScriptRunner.h"

#include "ChessSimMark.h"
#include "Data/GameDataSubsystem.h"

void FScriptRunner::Start(const UGameDataSubsystem* InData, const FString& Scene)
{
	Data = InData;
	Affection = 0;
	Flags.Reset();
	Result = EChessResult::None;
	PendingChoices.Reset();
	bFinished = false;
	Pc = Data ? Data->FindSceneStart(Scene) : INDEX_NONE;
}

FString FScriptRunner::GetCurrentScene() const
{
	if (!Data || !Data->GetScript().IsValidIndex(Pc)) return FString();
	return Data->GetScript()[Pc].Scene;
}

FScriptEvent FScriptRunner::MakeError(const FString& Why)
{
	UE_LOG(LogChessSim, Error, TEXT("Script error: %s"), *Why);
	bFinished = true;
	FScriptEvent E;
	E.Type = EScriptEventType::Error;
	E.Arg = Why;
	return E;
}

bool FScriptRunner::Jump(const FString& Target, int32 FromIndex)
{
	const TArray<FScriptStep>& Script = Data->GetScript();
	FString Scene = Script[FromIndex].Scene, Step = Target;
	if (Target.Split(TEXT(":"), &Scene, &Step))
	{
		Pc = Step.IsEmpty() ? Data->FindSceneStart(Scene) : Data->FindStep(Scene, Step);
	}
	else
	{
		Pc = Data->FindStep(Scene, Step);
	}
	return Pc != INDEX_NONE;
}

bool FScriptRunner::EvaluateCondition(const FString& Condition) const
{
	const FString C = Condition.TrimStartAndEnd();
	if (C.StartsWith(TEXT("affection>="))) return Affection >= FCString::Atoi(*C.RightChop(11));
	if (C.StartsWith(TEXT("affection<"))) return Affection < FCString::Atoi(*C.RightChop(10));
	if (C.StartsWith(TEXT("!flag:"))) return !Flags.Contains(C.RightChop(6));
	if (C.StartsWith(TEXT("flag:"))) return Flags.Contains(C.RightChop(5));
	if (C == TEXT("result=win")) return Result == EChessResult::Win;
	if (C == TEXT("result=lose")) return Result == EChessResult::Lose;
	if (C == TEXT("result=draw")) return Result == EChessResult::Draw;
	UE_LOG(LogChessSim, Warning, TEXT("Unknown script condition '%s' (treated as false)"), *Condition);
	return false;
}

bool FScriptRunner::Choose(int32 Index)
{
	if (!PendingChoices.IsValidIndex(Index) || Pc == INDEX_NONE) return false;
	const FScriptChoice Choice = PendingChoices[Index];
	PendingChoices.Reset();
	Affection += Choice.Affection;
	return Jump(Choice.Next, Pc);
}

FScriptEvent FScriptRunner::Advance()
{
	if (!Data) return MakeError(TEXT("no data"));
	if (bFinished) return MakeError(TEXT("script already finished"));
	if (PendingChoices.Num() > 0) return MakeError(TEXT("Advance() called while a choice is pending"));

	const TArray<FScriptStep>& Script = Data->GetScript();
	for (int32 Guard = 0; Guard < 10000; ++Guard)
	{
		if (!Script.IsValidIndex(Pc)) return MakeError(TEXT("jumped to a missing step"));
		const FScriptStep& S = Script[Pc];
		const int32 Here = Pc;

		// Default continuation: next row if it is in the same scene, otherwise the scene ends.
		auto Continue = [&]() -> bool {
			if (!S.Next.IsEmpty()) return Jump(S.Next, Here);
			Pc = Here + 1;
			return Script.IsValidIndex(Pc) && Script[Pc].Scene == S.Scene;
		};
		auto ContinueOrError = [&]() -> TOptional<FScriptEvent> {
			if (!Continue()) return MakeError(FString::Printf(TEXT("%s/%s: scene ended without END or jump"), *S.Scene, *S.Step));
			return {};
		};

		switch (S.Op)
		{
		case EScriptOp::Line:
		{
			FScriptEvent E;
			E.Type = EScriptEventType::Line;
			E.Speaker = S.Speaker;
			E.TextKey = S.TextKey;
			E.Emotion = S.Emotion;
			// Defer the "scene ended" error until the next Advance so this line still shows.
			if (!Continue()) Pc = INDEX_NONE;
			return E;
		}
		case EScriptOp::Choice:
		{
			FScriptEvent E;
			E.Type = EScriptEventType::Choice;
			E.TextKey = S.TextKey;
			E.Emotion = S.Emotion;
			for (int32 I = Here + 1; I < Script.Num() && Script[I].Op == EScriptOp::Option && Script[I].Scene == S.Scene; ++I)
			{
				FScriptChoice C;
				C.TextKey = Script[I].TextKey;
				C.Affection = FCString::Atoi(*Script[I].Arg);
				C.Next = Script[I].Next;
				E.Choices.Add(C);
			}
			if (E.Choices.Num() == 0) return MakeError(FString::Printf(TEXT("%s/%s: CHOICE without OPTION rows"), *S.Scene, *S.Step));
			PendingChoices = E.Choices;
			return E;
		}
		case EScriptOp::Option:
			// Reached by falling through a choice block; options only run via Choose().
			if (auto Err = ContinueOrError()) return *Err;
			break;
		case EScriptOp::Affection:
			Affection += FCString::Atoi(*S.Arg);
			if (auto Err = ContinueOrError()) return *Err;
			break;
		case EScriptOp::Flag:
			Flags.Add(S.Arg);
			if (auto Err = ContinueOrError()) return *Err;
			break;
		case EScriptOp::If:
			if (EvaluateCondition(S.Arg))
			{
				if (!Jump(S.Next, Here)) return MakeError(FString::Printf(TEXT("%s/%s: bad target %s"), *S.Scene, *S.Step, *S.Next));
			}
			else
			{
				Pc = Here + 1;
				if (!Script.IsValidIndex(Pc) || Script[Pc].Scene != S.Scene)
					return MakeError(FString::Printf(TEXT("%s/%s: scene ended after IF"), *S.Scene, *S.Step));
			}
			break;
		case EScriptOp::Goto:
			if (!Jump(S.Next, Here)) return MakeError(FString::Printf(TEXT("%s/%s: bad target %s"), *S.Scene, *S.Step, *S.Next));
			break;
		case EScriptOp::Camera:
		{
			FScriptEvent E;
			E.Type = EScriptEventType::Camera;
			E.Arg = S.Arg;
			if (!Continue()) Pc = INDEX_NONE;
			return E;
		}
		case EScriptOp::Chess:
		{
			FScriptEvent E;
			E.Type = EScriptEventType::StartChess;
			E.Arg = S.Arg;
			Result = EChessResult::None;
			if (!Continue()) Pc = INDEX_NONE;
			return E;
		}
		case EScriptOp::End:
		{
			bFinished = true;
			FScriptEvent E;
			E.Type = EScriptEventType::End;
			E.Arg = S.Arg;
			return E;
		}
		case EScriptOp::Invalid:
		default:
			return MakeError(FString::Printf(TEXT("%s/%s: invalid op"), *S.Scene, *S.Step));
		}
	}
	return MakeError(TEXT("script loop guard tripped (infinite GOTO loop?)"));
}
