// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Chess/ChessTypes.h"
#include "Data/GameDataTypes.h"

class UGameDataSubsystem;

enum class EScriptEventType : uint8
{
	Line,
	Choice,
	Camera,
	StartChess,
	End,
	Error,
};

struct FScriptChoice
{
	FString TextKey;
	int32 Affection = 0;
	FString Next;
};

/** A blocking (or presentation) step the game must act on. */
struct FScriptEvent
{
	EScriptEventType Type = EScriptEventType::Error;
	FString Speaker;
	FString TextKey;
	TOptional<EEmotion> Emotion;
	/** Camera shot / chess colour / ending id / error message. */
	FString Arg;
	TArray<FScriptChoice> Choices;
};

/**
 * Interprets Script.csv (docs/specs/data-format.md). Pure logic, no world
 * access: the game loop calls Advance(), presents the returned event and calls
 * Advance() again (after Choose() for choices, SetChessResult() for chess).
 */
class CHESSSIMMARK_API FScriptRunner
{
public:
	void Start(const UGameDataSubsystem* InData, const FString& Scene = TEXT("prologue"));
	FScriptEvent Advance();
	/** Resolve a pending Choice event. Returns false if there is none or the index is out of range. */
	bool Choose(int32 Index);
	void SetChessResult(EChessResult InResult) { Result = InResult; }

	int32 GetAffection() const { return Affection; }
	EChessResult GetResult() const { return Result; }
	bool HasFlag(const FString& Flag) const { return Flags.Contains(Flag); }
	bool IsFinished() const { return bFinished; }
	bool IsAwaitingChoice() const { return PendingChoices.Num() > 0; }
	FString GetCurrentScene() const;

	/** Exposed for tests. */
	bool EvaluateCondition(const FString& Condition) const;

private:
	bool Jump(const FString& Target, int32 FromIndex);
	FScriptEvent MakeError(const FString& Why);

	const UGameDataSubsystem* Data = nullptr;
	int32 Pc = INDEX_NONE;
	int32 Affection = 0;
	TSet<FString> Flags;
	EChessResult Result = EChessResult::None;
	TArray<FScriptChoice> PendingChoices;
	bool bFinished = false;
};
