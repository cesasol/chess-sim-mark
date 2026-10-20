// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Chess/ChessCore.h"
#include "Chess/ChessTypes.h"
#include "Widgets/SCompoundWidget.h"

class SBorder;
class SBox;
class SCanvas;
class SEditableTextBox;
class SScrollBox;
class STextBlock;
class SVerticalBox;
class SWrapBox;

struct FTitleOpponent
{
	FString Id;
	FText Name;
	FText Archetype;
	FText Blurb;
	FLinearColor Accent;
	bool bFemale = true;
};

struct FChessPanelState
{
	FText OpponentName;
	FText OpponentArchetype;
	FLinearColor Accent = FLinearColor::White;
	FText Status;
	int32 Affection = 0;
	int32 PlayerEval = 0;
	TArray<FString> SanMoves;
	bool bPlayerTurn = false;
	FText EngineNote;
};

/**
 * All game UI in one code-built Slate widget (no UMG assets), styled as a
 * Modern-JRPG visual-novel overlay. The game mode drives it through the Show/Set
 * calls and receives intents through the callbacks.
 */
class CHESSSIMMARK_API SChessSimHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SChessSimHud) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	// Intents -------------------------------------------------------------
	TFunction<FText(const FString&)> Loc;
	TFunction<void()> OnAdvance;
	TFunction<void(int32)> OnChoice;
	TFunction<void(const FString& OpponentId, EChessDifficulty, const FString& PlayerName)> OnStart;
	TFunction<void(const FString& Culture)> OnCultureChanged;
	TFunction<void()> OnQuit;
	TFunction<void()> OnHint;
	TFunction<void()> OnResign;
	TFunction<void(chesscore::PieceType)> OnPromote;
	TFunction<void()> OnReturnToTitle;
	/** Screen-space (Slate units) anchor for the reaction bubble, if visible. */
	TFunction<TOptional<FVector2D>()> GetBubbleAnchor;

	// Screens --------------------------------------------------------------
	void ShowTitle(const TArray<FTitleOpponent>& Opponents, const TArray<FString>& Cultures, const FString& Culture,
		EChessDifficulty Difficulty, const FString& PlayerName, const FString& SelectedOpponent);
	void HideAll();

	void ShowDialogue(const FText& Speaker, const FText& Text, const FLinearColor& Accent);
	bool IsTyping() const;
	void CompleteTyping();
	void ShowChoices(const FText& Prompt, const TArray<FText>& Options);
	bool IsShowingChoices() const { return bChoicesVisible; }
	void HideStory();

	void ShowChessPanel(bool bShow);
	void SetChessPanel(const FChessPanelState& State);
	void ShowReaction(const FText& Text, const FLinearColor& Accent, float Seconds);
	void ShowPromotion(bool bShow);
	bool IsShowingPromotion() const { return bPromotionVisible; }

	void ShowEnding(const FText& Title, const FText& Body, int32 Affection, const FLinearColor& Accent);
	void ShowToast(const FText& Text, float Seconds = 3.f);

	/** Fade to black (Alpha 1) or clear (Alpha 0) over Seconds. */
	void FadeTo(float Alpha, float Seconds);

private:
	TSharedRef<SWidget> BuildDialogue();
	TSharedRef<SWidget> BuildChessPanel();
	TSharedRef<SWidget> BuildPromotion();
	TSharedRef<SWidget> BuildEnding();
	TSharedRef<SWidget> MakeButton(TAttribute<FText> Label, TFunction<void()> OnClick, const FLinearColor& Accent, float FontSize = 18.f);
	void RebuildTitle();
	FText L(const FString& Key) const;
	static FText Hearts(int32 Affection);

	// Title state
	TArray<FTitleOpponent> TitleOpponents;
	TArray<FString> TitleCultures;
	FString TitleCulture;
	EChessDifficulty TitleDifficulty = EChessDifficulty::Club;
	FString TitleName;
	FString TitleSelected;

	TSharedPtr<SBox> TitleLayer;
	TSharedPtr<SBox> StoryLayer;
	TSharedPtr<SBorder> DialogueBox;
	TSharedPtr<STextBlock> SpeakerText;
	TSharedPtr<SBorder> SpeakerPlate;
	TSharedPtr<STextBlock> DialogueText;
	TSharedPtr<STextBlock> ContinueHint;
	TSharedPtr<SVerticalBox> ChoiceList;
	TSharedPtr<SBox> ChoiceLayer;
	TSharedPtr<SBox> ChessLayer;
	TSharedPtr<STextBlock> ChessOpponent;
	TSharedPtr<STextBlock> ChessArchetype;
	TSharedPtr<STextBlock> ChessStatus;
	TSharedPtr<STextBlock> ChessAffection;
	TSharedPtr<STextBlock> ChessEngine;
	TSharedPtr<SScrollBox> MoveList;
	TSharedPtr<SBox> EvalFill;
	TSharedPtr<SCanvas> BubbleCanvas;
	TSharedPtr<SBorder> Bubble;
	TSharedPtr<STextBlock> BubbleText;
	TSharedPtr<SBox> PromotionLayer;
	TSharedPtr<SBox> EndingLayer;
	TSharedPtr<STextBlock> EndingTitle;
	TSharedPtr<STextBlock> EndingBody;
	TSharedPtr<STextBlock> EndingHearts;
	TSharedPtr<SBorder> ToastBox;
	TSharedPtr<STextBlock> ToastText;
	TSharedPtr<SBorder> FadeLayer;

	FString FullText;
	float Revealed = 0.f;
	float CharsPerSecond = 45.f;
	bool bChoicesVisible = false;
	bool bPromotionVisible = false;
	float BubbleTime = 0.f;
	float ToastTime = 0.f;
	float Fade = 0.f, FadeTarget = 0.f, FadeSpeed = 1.f;
	FLinearColor BubbleAccent = FLinearColor::White;
	int32 LastMoveCount = -1;
	float EvalFraction = 0.5f;
	float EvalTarget = 0.5f;
};
