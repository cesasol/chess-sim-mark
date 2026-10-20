// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Data/GameDataTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameDataSubsystem.generated.h"

/**
 * Loads characters, scripted flows, reactions and localized lines from
 * Content/Data CSVs (see docs/specs/data-format.md). Lines are resolved with
 * opponent-prefixed keys and culture fallback to "en".
 */
UCLASS()
class CHESSSIMMARK_API UGameDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** (Re)load everything from DataDir. Returns false if a required file is missing or malformed. */
	bool LoadFrom(const FString& DataDir);
	bool Reload() { return LoadFrom(DataDir); }
	static FString DefaultDataDir();

	/** Problems found while loading (missing keys, bad ops...). Also logged. */
	const TArray<FString>& GetLoadErrors() const { return LoadErrors; }

	// Culture -----------------------------------------------------------
	TArray<FString> GetAvailableCultures() const;
	const FString& GetCulture() const { return Culture; }
	void SetCulture(const FString& InCulture);

	// Lines --------------------------------------------------------------
	/** Resolve a line for the active opponent with {player}/{opponent} substitution. */
	FText GetLine(const FString& Key) const;
	bool HasLine(const FString& Key) const;
	FText GetCharacterName(const FString& CharacterId) const;

	void SetActiveOpponent(const FString& CharacterId) { ActiveOpponent = CharacterId; }
	const FString& GetActiveOpponent() const { return ActiveOpponent; }
	void SetPlayerName(const FString& Name) { PlayerName = Name; }
	const FString& GetPlayerName() const { return PlayerName; }

	// Definitions ----------------------------------------------------------
	const TArray<FCharacterDef>& GetCharacters() const { return Characters; }
	const FCharacterDef* FindCharacter(const FString& Id) const;
	TArray<const FCharacterDef*> GetOpponents() const;
	const TArray<FScriptStep>& GetScript() const { return Script; }
	/** Index into GetScript() of a scene's first step, or INDEX_NONE. */
	int32 FindSceneStart(const FString& Scene) const;
	/** Index of `Step` within `Scene`, or INDEX_NONE. */
	int32 FindStep(const FString& Scene, const FString& Step) const;
	/** Weighted random reaction for the trigger (opponent-specific first, then "*"). */
	const FReactionDef* PickReaction(const FString& CharacterId, const FString& Trigger, FRandomStream& Random) const;

private:
	bool LoadLines(const FString& InCulture, TMap<FString, FString>& Out) const;
	const FString* FindRaw(const FString& Key) const;
	void ValidateReferences();

	FString DataDir;
	FString Culture = TEXT("en");
	FString ActiveOpponent;
	FString PlayerName = TEXT("Sora");

	TArray<FCharacterDef> Characters;
	TArray<FScriptStep> Script;
	TArray<FReactionDef> Reactions;
	TMap<FString, FString> FallbackLines;
	TMap<FString, FString> CultureLines;
	TArray<FString> LoadErrors;
};
