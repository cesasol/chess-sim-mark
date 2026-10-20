// SPDX-License-Identifier: Apache-2.0
#include "Data/GameDataSubsystem.h"

#include "ChessSimMark.h"
#include "Data/CsvReader.h"
#include "HAL/FileManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/Paths.h"

EEmotion Emotion::FromString(const FString& S, EEmotion Default)
{
	static const TPair<const TCHAR*, EEmotion> Table[] = {
		{TEXT("Neutral"), EEmotion::Neutral}, {TEXT("Happy"), EEmotion::Happy}, {TEXT("Smug"), EEmotion::Smug},
		{TEXT("Annoyed"), EEmotion::Annoyed}, {TEXT("Flustered"), EEmotion::Flustered}, {TEXT("Surprised"), EEmotion::Surprised},
		{TEXT("Sad"), EEmotion::Sad}, {TEXT("Thinking"), EEmotion::Thinking}, {TEXT("Determined"), EEmotion::Determined},
		{TEXT("Dark"), EEmotion::Dark},
	};
	for (const auto& P : Table)
		if (S.Equals(P.Key, ESearchCase::IgnoreCase)) return P.Value;
	return Default;
}

FString Emotion::ToString(EEmotion E)
{
	const UEnum* Enum = StaticEnum<EEmotion>();
	return Enum ? Enum->GetNameStringByValue(static_cast<int64>(E)) : TEXT("Neutral");
}

namespace
{
EScriptOp ParseOp(const FString& S)
{
	static const TPair<const TCHAR*, EScriptOp> Table[] = {
		{TEXT("LINE"), EScriptOp::Line}, {TEXT("CHOICE"), EScriptOp::Choice}, {TEXT("OPTION"), EScriptOp::Option},
		{TEXT("AFFECTION"), EScriptOp::Affection}, {TEXT("FLAG"), EScriptOp::Flag}, {TEXT("IF"), EScriptOp::If},
		{TEXT("GOTO"), EScriptOp::Goto}, {TEXT("CAMERA"), EScriptOp::Camera}, {TEXT("CHESS"), EScriptOp::Chess},
		{TEXT("END"), EScriptOp::End},
	};
	for (const auto& P : Table)
		if (S.Equals(P.Key, ESearchCase::IgnoreCase)) return P.Value;
	return EScriptOp::Invalid;
}

FLinearColor ParseHexColor(const FString& Hex)
{
	if (Hex.Len() == 7 && Hex[0] == TEXT('#')) return FLinearColor(FColor::FromHex(Hex));
	return FLinearColor::White;
}
} // namespace

FString UGameDataSubsystem::DefaultDataDir() { return FPaths::ProjectContentDir() / TEXT("Data"); }

void UGameDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Start in the OS/game culture when we have lines for it.
	const FString Lang = FInternationalization::Get().GetCurrentLanguage()->GetTwoLetterISOLanguageName();
	DataDir = DefaultDataDir();
	if (GetAvailableCultures().Contains(Lang)) Culture = Lang;
	LoadFrom(DataDir);
}

TArray<FString> UGameDataSubsystem::GetAvailableCultures() const
{
	TArray<FString> Dirs;
	IFileManager::Get().FindFiles(Dirs, *(DataDir / TEXT("Lines") / TEXT("*")), /*Files*/ false, /*Dirs*/ true);
	Dirs.RemoveAll([this](const FString& D) { return !FPaths::FileExists(DataDir / TEXT("Lines") / D / TEXT("Lines.csv")); });
	Dirs.Sort();
	return Dirs;
}

void UGameDataSubsystem::SetCulture(const FString& InCulture)
{
	Culture = InCulture;
	CultureLines.Reset();
	if (Culture != TEXT("en")) LoadLines(Culture, CultureLines);
	// Keep engine-side text (number formatting etc.) consistent.
	FInternationalization::Get().SetCurrentLanguage(Culture);
}

bool UGameDataSubsystem::LoadLines(const FString& InCulture, TMap<FString, FString>& Out) const
{
	TArray<Csv::FRow> Rows;
	FString Err;
	const FString Path = DataDir / TEXT("Lines") / InCulture / TEXT("Lines.csv");
	if (!Csv::LoadTable(Path, Rows, &Err))
	{
		UE_LOG(LogChessSim, Warning, TEXT("Lines for culture '%s': %s"), *InCulture, *Err);
		return false;
	}
	for (const Csv::FRow& R : Rows)
	{
		const FString& Key = Csv::Get(R, TEXT("key"));
		if (!Key.IsEmpty()) Out.Add(Key, Csv::Get(R, TEXT("text")));
	}
	return true;
}

bool UGameDataSubsystem::LoadFrom(const FString& InDataDir)
{
	DataDir = InDataDir;
	LoadErrors.Reset();
	Characters.Reset();
	Script.Reset();
	Reactions.Reset();
	FallbackLines.Reset();
	CultureLines.Reset();

	auto Load = [this](const TCHAR* File, TArray<Csv::FRow>& Rows) {
		FString Err;
		if (!Csv::LoadTable(DataDir / File, Rows, &Err))
		{
			LoadErrors.Add(FString::Printf(TEXT("%s: %s"), File, *Err));
			return false;
		}
		return true;
	};

	TArray<Csv::FRow> CharRows, ScriptRows, ReactionRows;
	bool bOk = Load(TEXT("Characters.csv"), CharRows);
	bOk &= Load(TEXT("Script.csv"), ScriptRows);
	bOk &= Load(TEXT("Reactions.csv"), ReactionRows);
	if (!LoadLines(TEXT("en"), FallbackLines))
	{
		LoadErrors.Add(TEXT("Lines/en/Lines.csv missing"));
		bOk = false;
	}
	if (Culture != TEXT("en")) LoadLines(Culture, CultureLines);

	for (const Csv::FRow& R : CharRows)
	{
		FCharacterDef C;
		C.Id = Csv::Get(R, TEXT("id"));
		C.bFemale = Csv::Get(R, TEXT("gender")).Equals(TEXT("Female"), ESearchCase::IgnoreCase);
		C.bOpponent = Csv::Get(R, TEXT("role")).Equals(TEXT("Opponent"), ESearchCase::IgnoreCase);
		C.Archetype = Csv::Get(R, TEXT("archetype"));
		C.NameKey = Csv::Get(R, TEXT("namekey"));
		C.BlurbKey = Csv::Get(R, TEXT("blurbkey"));
		C.Accent = ParseHexColor(Csv::Get(R, TEXT("accentcolor")));
		C.Age = FCString::Atoi(*Csv::Get(R, TEXT("age")));
		ChessDifficulty::FromString(Csv::Get(R, TEXT("difficulty")), C.Difficulty);
		C.Model = Csv::Get(R, TEXT("model"));
		const FString& Scale = Csv::Get(R, TEXT("modelscale"));
		C.ModelScale = Scale.IsEmpty() ? 1.f : FMath::Max(0.1f, FCString::Atof(*Scale));
		if (C.Id.IsEmpty()) continue;
		if (C.Age < 18)
		{
			// Hard content rule: every character is an adult. Refuse to load otherwise.
			LoadErrors.Add(FString::Printf(TEXT("character '%s' has Age %d (< 18); skipped"), *C.Id, C.Age));
			continue;
		}
		Characters.Add(MoveTemp(C));
	}

	for (const Csv::FRow& R : ScriptRows)
	{
		FScriptStep S;
		S.Scene = Csv::Get(R, TEXT("scene"));
		S.Step = Csv::Get(R, TEXT("step"));
		S.Op = ParseOp(Csv::Get(R, TEXT("op")));
		S.Speaker = Csv::Get(R, TEXT("speaker"));
		S.TextKey = Csv::Get(R, TEXT("textkey"));
		const FString& Emo = Csv::Get(R, TEXT("emotion"));
		if (!Emo.IsEmpty()) S.Emotion = Emotion::FromString(Emo);
		S.Arg = Csv::Get(R, TEXT("arg"));
		S.Next = Csv::Get(R, TEXT("next"));
		if (S.Op == EScriptOp::Invalid)
			LoadErrors.Add(FString::Printf(TEXT("Script %s/%s: unknown op '%s'"), *S.Scene, *S.Step, *Csv::Get(R, TEXT("op"))));
		Script.Add(MoveTemp(S));
	}

	for (const Csv::FRow& R : ReactionRows)
	{
		FReactionDef D;
		D.Character = Csv::Get(R, TEXT("character"));
		D.Trigger = Csv::Get(R, TEXT("trigger"));
		D.TextKey = Csv::Get(R, TEXT("textkey"));
		D.Emotion = Emotion::FromString(Csv::Get(R, TEXT("emotion")));
		const FString& W = Csv::Get(R, TEXT("weight"));
		D.Weight = W.IsEmpty() ? 1.f : FMath::Max(0.f, FCString::Atof(*W));
		Reactions.Add(MoveTemp(D));
	}

	ValidateReferences();
	for (const FString& E : LoadErrors) UE_LOG(LogChessSim, Warning, TEXT("Game data: %s"), *E);
	UE_LOG(LogChessSim, Log, TEXT("Game data loaded from %s: %d characters, %d script steps, %d reactions, %d lines (culture %s)"),
		*DataDir, Characters.Num(), Script.Num(), Reactions.Num(), FallbackLines.Num(), *Culture);
	if (ActiveOpponent.IsEmpty())
	{
		const TArray<const FCharacterDef*> Opps = GetOpponents();
		if (Opps.Num() > 0) ActiveOpponent = Opps[0]->Id;
	}
	return bOk;
}

void UGameDataSubsystem::ValidateReferences()
{
	for (const FScriptStep& S : Script)
	{
		if (S.Next.IsEmpty()) continue;
		FString Scene = S.Scene, Step = S.Next;
		if (S.Next.Split(TEXT(":"), &Scene, &Step))
		{
			if (FindSceneStart(Scene) == INDEX_NONE)
				LoadErrors.Add(FString::Printf(TEXT("Script %s/%s: unknown scene '%s'"), *S.Scene, *S.Step, *Scene));
			else if (!Step.IsEmpty() && FindStep(Scene, Step) == INDEX_NONE)
				LoadErrors.Add(FString::Printf(TEXT("Script %s/%s: unknown target '%s'"), *S.Scene, *S.Step, *S.Next));
		}
		else if (FindStep(S.Scene, Step) == INDEX_NONE)
		{
			LoadErrors.Add(FString::Printf(TEXT("Script %s/%s: unknown target '%s'"), *S.Scene, *S.Step, *S.Next));
		}
	}
}

const FString* UGameDataSubsystem::FindRaw(const FString& Key) const
{
	if (!ActiveOpponent.IsEmpty() && !Key.StartsWith(ActiveOpponent + TEXT(".")))
	{
		const FString Prefixed = ActiveOpponent + TEXT(".") + Key;
		if (const FString* V = CultureLines.Find(Prefixed)) return V;
		if (const FString* V = FallbackLines.Find(Prefixed)) return V;
	}
	if (const FString* V = CultureLines.Find(Key)) return V;
	return FallbackLines.Find(Key);
}

bool UGameDataSubsystem::HasLine(const FString& Key) const { return FindRaw(Key) != nullptr; }

FText UGameDataSubsystem::GetLine(const FString& Key) const
{
	const FString* Raw = FindRaw(Key);
	if (!Raw)
	{
		UE_LOG(LogChessSim, Warning, TEXT("Missing line '%s' (culture %s, opponent %s)"), *Key, *Culture, *ActiveOpponent);
		return FText::FromString(FString::Printf(TEXT("!!%s!!"), *Key));
	}
	FString Text = *Raw;
	if (Text.Contains(TEXT("{")))
	{
		Text.ReplaceInline(TEXT("{player}"), *PlayerName);
		if (Text.Contains(TEXT("{opponent}")))
			Text.ReplaceInline(TEXT("{opponent}"), *GetCharacterName(ActiveOpponent).ToString());
	}
	return FText::FromString(Text);
}

FText UGameDataSubsystem::GetCharacterName(const FString& CharacterId) const
{
	if (CharacterId == TEXT("player")) return FText::FromString(PlayerName);
	const FCharacterDef* C = FindCharacter(CharacterId);
	const FString Key = C ? C->NameKey : FString::Printf(TEXT("name.%s"), *CharacterId);
	// Names are never opponent-prefixed.
	if (const FString* V = CultureLines.Find(Key)) return FText::FromString(*V);
	if (const FString* V = FallbackLines.Find(Key)) return FText::FromString(*V);
	return FText::FromString(CharacterId);
}

const FCharacterDef* UGameDataSubsystem::FindCharacter(const FString& Id) const
{
	return Characters.FindByPredicate([&](const FCharacterDef& C) { return C.Id == Id; });
}

TArray<const FCharacterDef*> UGameDataSubsystem::GetOpponents() const
{
	TArray<const FCharacterDef*> Out;
	for (const FCharacterDef& C : Characters)
		if (C.bOpponent) Out.Add(&C);
	return Out;
}

int32 UGameDataSubsystem::FindSceneStart(const FString& Scene) const
{
	return Script.IndexOfByPredicate([&](const FScriptStep& S) { return S.Scene == Scene; });
}

int32 UGameDataSubsystem::FindStep(const FString& Scene, const FString& Step) const
{
	return Script.IndexOfByPredicate([&](const FScriptStep& S) { return S.Scene == Scene && S.Step == Step; });
}

const FReactionDef* UGameDataSubsystem::PickReaction(const FString& CharacterId, const FString& Trigger, FRandomStream& Random) const
{
	for (const FString& Who : {CharacterId, FString(TEXT("*"))})
	{
		TArray<const FReactionDef*> Candidates;
		float Total = 0.f;
		for (const FReactionDef& R : Reactions)
		{
			if (R.Character == Who && R.Trigger == Trigger && R.Weight > 0.f)
			{
				Candidates.Add(&R);
				Total += R.Weight;
			}
		}
		if (Candidates.Num() == 0) continue;
		float Roll = Random.FRand() * Total;
		for (const FReactionDef* R : Candidates)
		{
			Roll -= R->Weight;
			if (Roll <= 0.f) return R;
		}
		return Candidates.Last();
	}
	return nullptr;
}
