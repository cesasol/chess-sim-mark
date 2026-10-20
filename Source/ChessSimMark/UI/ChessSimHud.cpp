// SPDX-License-Identifier: Apache-2.0
#include "UI/ChessSimHud.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateColorBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace HudStyle
{
const FLinearColor Ink(0.03f, 0.03f, 0.06f, 0.82f);
const FLinearColor InkSolid(0.03f, 0.03f, 0.06f, 0.95f);
const FLinearColor Paper(0.98f, 0.96f, 0.93f, 0.96f);
const FLinearColor Gold(1.0f, 0.82f, 0.45f, 1.f);
const FLinearColor Text(0.95f, 0.94f, 0.97f, 1.f);
const FLinearColor Muted(0.7f, 0.7f, 0.78f, 1.f);

FSlateFontInfo Font(float Size, const TCHAR* Weight = TEXT("Regular"))
{
	return FCoreStyle::GetDefaultFontStyle(Weight, FMath::RoundToInt(Size));
}

/** Brushes must outlive the widgets; keep a small cache keyed by look. */
const FSlateBrush* Rounded(const FLinearColor& Fill, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f)
{
	static TMap<FString, TSharedPtr<FSlateBrush>> Cache;
	const FString Key = FString::Printf(TEXT("%s|%.1f|%s|%.1f"), *Fill.ToString(), Radius, *Outline.ToString(), OutlineWidth);
	if (TSharedPtr<FSlateBrush>* Found = Cache.Find(Key)) return Found->Get();
	TSharedPtr<FSlateBrush> B = MakeShared<FSlateRoundedBoxBrush>(Fill, Radius, Outline, OutlineWidth);
	Cache.Add(Key, B);
	return B.Get();
}

const FSlateBrush* Solid(const FLinearColor& Fill)
{
	static TMap<FString, TSharedPtr<FSlateBrush>> Cache;
	const FString Key = Fill.ToString();
	if (TSharedPtr<FSlateBrush>* Found = Cache.Find(Key)) return Found->Get();
	TSharedPtr<FSlateBrush> B = MakeShared<FSlateColorBrush>(Fill);
	Cache.Add(Key, B);
	return B.Get();
}
} // namespace HudStyle

using namespace HudStyle;

FText SChessSimHud::L(const FString& Key) const { return Loc ? Loc(Key) : FText::FromString(Key); }

FText SChessSimHud::Hearts(int32 Affection)
{
	FString S;
	const int32 N = FMath::Clamp(Affection, 0, 5);
	for (int32 I = 0; I < 5; ++I) S += I < N ? TEXT("\u2665") : TEXT("\u2661");
	return FText::FromString(S);
}

TSharedRef<SWidget> SChessSimHud::MakeButton(TAttribute<FText> Label, TFunction<void()> OnClick, const FLinearColor& Accent, float FontSize)
{
	static FButtonStyle Style = [] {
		FButtonStyle S = FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("Button");
		S.SetNormal(*Rounded(FLinearColor(0.08f, 0.08f, 0.14f, 0.9f), 10.f, FLinearColor(1, 1, 1, 0.25f), 1.5f));
		S.SetHovered(*Rounded(FLinearColor(0.18f, 0.14f, 0.26f, 0.95f), 10.f, Gold, 2.f));
		S.SetPressed(*Rounded(FLinearColor(0.3f, 0.2f, 0.35f, 1.f), 10.f, Gold, 2.f));
		S.SetNormalPadding(FMargin(18, 8));
		S.SetPressedPadding(FMargin(18, 9, 18, 7));
		return S;
	}();
	return SNew(SButton)
		.ButtonStyle(&Style)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked_Lambda([OnClick]() {
			if (OnClick) OnClick();
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Text(Label).Font(Font(FontSize, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Text)).ShadowOffset(FVector2D(1, 1))
		];
}

void SChessSimHud::Construct(const FArguments&)
{
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[SAssignNew(TitleLayer, SBox).Visibility(EVisibility::Collapsed)]
		+ SOverlay::Slot()[SAssignNew(ChessLayer, SBox).Visibility(EVisibility::Collapsed)[BuildChessPanel()]]
		+ SOverlay::Slot()[SAssignNew(StoryLayer, SBox).Visibility(EVisibility::Collapsed)[BuildDialogue()]]
		+ SOverlay::Slot()
		[
			SAssignNew(ChoiceLayer, SBox).Visibility(EVisibility::Collapsed).HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(FMargin(0, 0, 0, 160))
			[
				SNew(SBox).WidthOverride(720)[SAssignNew(ChoiceList, SVerticalBox)]
			]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(BubbleCanvas, SCanvas).Visibility(EVisibility::HitTestInvisible)
			+ SCanvas::Slot()
			.Position_Lambda([this]() {
				const TOptional<FVector2D> A = GetBubbleAnchor ? GetBubbleAnchor() : TOptional<FVector2D>();
				return A.IsSet() ? *A : FVector2D(640, 120);
			})
			.Size(FVector2D(440, 160))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			[
				SNew(SBox).WidthOverride(440).VAlign(VAlign_Bottom)
				[
					SAssignNew(Bubble, SBorder)
					.Visibility(EVisibility::Collapsed)
					.BorderImage_Lambda([this]() { return Rounded(Paper, 18.f, BubbleAccent, 3.f); })
					.Padding(FMargin(18, 12))
					[
						SAssignNew(BubbleText, STextBlock).Font(Font(19, TEXT("Bold"))).ColorAndOpacity(FSlateColor(FLinearColor(0.1f, 0.08f, 0.12f))).WrapTextAt(400.f).Justification(ETextJustify::Center)
					]
				]
			]
		]
		+ SOverlay::Slot()[SAssignNew(PromotionLayer, SBox).Visibility(EVisibility::Collapsed)[BuildPromotion()]]
		+ SOverlay::Slot()[SAssignNew(EndingLayer, SBox).Visibility(EVisibility::Collapsed)[BuildEnding()]]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(FMargin(0, 28, 0, 0))
		[
			SAssignNew(ToastBox, SBorder).Visibility(EVisibility::Collapsed).BorderImage(Rounded(Ink, 12.f, Gold, 1.5f)).Padding(FMargin(20, 10))
			[
				SAssignNew(ToastText, STextBlock).Font(Font(17)).ColorAndOpacity(FSlateColor(Text))
			]
		]
		+ SOverlay::Slot()
		[
			SAssignNew(FadeLayer, SBorder).Visibility(EVisibility::HitTestInvisible).BorderImage(Solid(FLinearColor::Black))
			.BorderBackgroundColor_Lambda([this]() { return FSlateColor(FLinearColor(0, 0, 0, Fade)); })
		]
	];
}

TSharedRef<SWidget> SChessSimHud::BuildDialogue()
{
	return SNew(SBox)
		.VAlign(VAlign_Bottom)
		.HAlign(HAlign_Center)
		.Padding(FMargin(40, 0, 40, 36))
		[
			SNew(SBox).WidthOverride(1180).MinDesiredHeight(190)
			[
				SNew(SOverlay)
				+ SOverlay::Slot().Padding(FMargin(0, 26, 0, 0))
				[
					SAssignNew(DialogueBox, SBorder)
					.BorderImage(Rounded(Ink, 22.f, FLinearColor(1, 1, 1, 0.18f), 1.5f))
					.Padding(FMargin(34, 34, 34, 18))
					.OnMouseButtonDown_Lambda([this](const FGeometry&, const FPointerEvent&) {
						if (OnAdvance) OnAdvance();
						return FReply::Handled();
					})
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().FillHeight(1.f)
						[
							SAssignNew(DialogueText, STextBlock).Font(Font(23)).ColorAndOpacity(FSlateColor(Text)).AutoWrapText(true).LineHeightPercentage(1.15f).ShadowOffset(FVector2D(1, 1))
						]
						+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
						[
							SAssignNew(ContinueHint, STextBlock).Font(Font(13)).ColorAndOpacity(FSlateColor(Muted))
						]
					]
				]
				+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(FMargin(28, 0, 0, 0))
				[
					SAssignNew(SpeakerPlate, SBorder)
					.BorderImage(Rounded(FLinearColor::White, 14.f))
					.Padding(FMargin(22, 8))
					[
						SAssignNew(SpeakerText, STextBlock).Font(Font(21, TEXT("Bold"))).ColorAndOpacity(FSlateColor(FLinearColor::White)).ShadowOffset(FVector2D(1, 1))
					]
				]
			]
		];
}

TSharedRef<SWidget> SChessSimHud::BuildChessPanel()
{
	return SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		.Padding(FMargin(0, 24, 24, 24))
		[
			SNew(SBox).WidthOverride(340)
			[
				SNew(SBorder).BorderImage(Rounded(Ink, 20.f, FLinearColor(1, 1, 1, 0.15f), 1.5f)).Padding(FMargin(20))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(ChessOpponent, STextBlock).Font(Font(26, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Text))]
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(ChessArchetype, STextBlock).Font(Font(14)).ColorAndOpacity(FSlateColor(Muted))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 6)[SAssignNew(ChessAffection, STextBlock).Font(Font(22)).ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.4f, 0.55f)))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 4)
					[
						SNew(SBorder).BorderImage(Rounded(FLinearColor(1, 1, 1, 0.06f), 10.f)).Padding(FMargin(12, 10))
						[
							SAssignNew(ChessStatus, STextBlock).Font(Font(18, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Gold)).AutoWrapText(true)
						]
					]
					// Advantage meter: player share on the left.
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 8)
					[
						SNew(SBox).HeightOverride(10)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(TAttribute<float>::CreateLambda([this]() { return EvalFraction; }))
							[
								SNew(SBorder).BorderImage(Rounded(FLinearColor(0.95f, 0.9f, 0.8f), 5.f))
							]
							+ SHorizontalBox::Slot().FillWidth(TAttribute<float>::CreateLambda([this]() { return 1.f - EvalFraction; }))
							[
								SNew(SBorder).BorderImage(Rounded(FLinearColor(0.2f, 0.12f, 0.25f), 5.f))
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 4)
					[
						SNew(STextBlock).Text_Lambda([this]() { return L(TEXT("ui.move_list")); }).Font(Font(14, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Muted))
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SNew(SBorder).BorderImage(Rounded(FLinearColor(0, 0, 0, 0.25f), 10.f)).Padding(FMargin(10))
						[
							SAssignNew(MoveList, SScrollBox)
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 6, 0)
						[
							SNew(SBox)[MakeButton(TAttribute<FText>::CreateLambda([this]() { return L(TEXT("ui.hint")); }), [this]() { if (OnHint) OnHint(); }, Gold)]
						]
						+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6, 0, 0, 0)
						[
							SNew(SBox)[MakeButton(TAttribute<FText>::CreateLambda([this]() { return L(TEXT("ui.resign")); }), [this]() { if (OnResign) OnResign(); }, Gold)]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
					[
						SAssignNew(ChessEngine, STextBlock).Font(Font(11)).ColorAndOpacity(FSlateColor(Muted)).AutoWrapText(true)
					]
				]
			]
		];
}

TSharedRef<SWidget> SChessSimHud::BuildPromotion()
{
	using chesscore::PieceType;
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	const TPair<PieceType, const TCHAR*> Pieces[] = {
		{PieceType::Queen, TEXT("\u265B")}, {PieceType::Rook, TEXT("\u265C")}, {PieceType::Bishop, TEXT("\u265D")}, {PieceType::Knight, TEXT("\u265E")}};
	for (const auto& P : Pieces)
	{
		const PieceType T = P.Key;
		Row->AddSlot().AutoWidth().Padding(8)
		[
			SNew(SBox).WidthOverride(96).HeightOverride(96)
			[
				MakeButton(FText::FromString(P.Value), [this, T]() { if (OnPromote) OnPromote(T); }, Gold, 44.f)
			]
		];
	}
	return SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBorder).BorderImage(Rounded(InkSolid, 20.f, Gold, 2.f)).Padding(FMargin(24))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 10)
			[
				SNew(STextBlock).Text_Lambda([this]() { return L(TEXT("ui.promote")); }).Font(Font(22, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Text))
			]
			+ SVerticalBox::Slot().AutoHeight()[Row]
		]
	];
}

TSharedRef<SWidget> SChessSimHud::BuildEnding()
{
	return SNew(SBorder).BorderImage(Solid(FLinearColor(0.02f, 0.01f, 0.04f, 0.72f))).HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SBox).WidthOverride(820)
		[
			SNew(SBorder).BorderImage(Rounded(InkSolid, 26.f, Gold, 2.f)).Padding(FMargin(48, 40))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(EndingTitle, STextBlock).Font(Font(40, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Gold)).Justification(ETextJustify::Center).AutoWrapText(true)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10)
				[
					SAssignNew(EndingHearts, STextBlock).Font(Font(30)).ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.4f, 0.55f)))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 14)
				[
					SAssignNew(EndingBody, STextBlock).Font(Font(21)).ColorAndOpacity(FSlateColor(Text)).AutoWrapText(true).Justification(ETextJustify::Center).LineHeightPercentage(1.2f)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 20, 0, 0)
				[
					SNew(SBox).MinDesiredWidth(280)
					[
						MakeButton(TAttribute<FText>::CreateLambda([this]() { return L(TEXT("ui.ending_back")); }), [this]() { if (OnReturnToTitle) OnReturnToTitle(); }, Gold, 20.f)
					]
				]
			]
		]
	];
}

void SChessSimHud::ShowTitle(const TArray<FTitleOpponent>& Opponents, const TArray<FString>& Cultures, const FString& Culture,
	EChessDifficulty Difficulty, const FString& PlayerName, const FString& SelectedOpponent)
{
	TitleOpponents = Opponents;
	TitleCultures = Cultures;
	TitleCulture = Culture;
	TitleDifficulty = Difficulty;
	TitleName = PlayerName;
	TitleSelected = SelectedOpponent.IsEmpty() && Opponents.Num() > 0 ? Opponents[0].Id : SelectedOpponent;
	HideAll();
	RebuildTitle();
	TitleLayer->SetVisibility(EVisibility::Visible);
}

void SChessSimHud::RebuildTitle()
{
	// Opponent cards.
	TSharedRef<SHorizontalBox> Cards = SNew(SHorizontalBox);
	for (const FTitleOpponent& O : TitleOpponents)
	{
		const bool bSel = O.Id == TitleSelected;
		const FString Id = O.Id;
		Cards->AddSlot().AutoWidth().Padding(14, 0)
		[
			SNew(SBox).WidthOverride(360).HeightOverride(230)
			[
				SNew(SBorder)
				.BorderImage(Rounded(bSel ? FLinearColor(0.12f, 0.08f, 0.16f, 0.95f) : Ink, 20.f, bSel ? O.Accent : FLinearColor(1, 1, 1, 0.2f), bSel ? 4.f : 1.5f))
				.Padding(FMargin(22, 18))
				.OnMouseButtonDown_Lambda([this, Id](const FGeometry&, const FPointerEvent&) {
					TitleSelected = Id;
					RebuildTitle();
					return FReply::Handled();
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(STextBlock).Text(O.Name).Font(Font(28, TEXT("Bold"))).ColorAndOpacity(FSlateColor(O.Accent))]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString(O.bFemale ? TEXT("\u2640") : TEXT("\u2642"))).Font(Font(26)).ColorAndOpacity(FSlateColor(O.Accent))
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 10)[SNew(STextBlock).Text(O.Archetype).Font(Font(15, TEXT("Italic"))).ColorAndOpacity(FSlateColor(Muted))]
					+ SVerticalBox::Slot().FillHeight(1.f)[SNew(STextBlock).Text(O.Blurb).Font(Font(16)).ColorAndOpacity(FSlateColor(Text)).AutoWrapText(true)]
				]
			]
		];
	}

	// Difficulty selector.
	TSharedRef<SHorizontalBox> Diffs = SNew(SHorizontalBox);
	for (EChessDifficulty D : {EChessDifficulty::Novice, EChessDifficulty::Casual, EChessDifficulty::Club, EChessDifficulty::Strong, EChessDifficulty::Champion})
	{
		const bool bSel = D == TitleDifficulty;
		Diffs->AddSlot().AutoWidth().Padding(4)
		[
			SNew(SBorder)
			.BorderImage(Rounded(bSel ? FLinearColor(0.3f, 0.22f, 0.1f, 0.95f) : Ink, 10.f, bSel ? Gold : FLinearColor(1, 1, 1, 0.2f), bSel ? 2.f : 1.f))
			.Padding(FMargin(14, 6))
			.OnMouseButtonDown_Lambda([this, D](const FGeometry&, const FPointerEvent&) {
				TitleDifficulty = D;
				RebuildTitle();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(L(TEXT("ui.difficulty.") + ChessDifficulty::ToString(D))).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Text))
			]
		];
	}

	// Language selector.
	TSharedRef<SHorizontalBox> Langs = SNew(SHorizontalBox);
	for (const FString& C : TitleCultures)
	{
		const bool bSel = C == TitleCulture;
		Langs->AddSlot().AutoWidth().Padding(4)
		[
			SNew(SBorder)
			.BorderImage(Rounded(bSel ? FLinearColor(0.3f, 0.22f, 0.1f, 0.95f) : Ink, 10.f, bSel ? Gold : FLinearColor(1, 1, 1, 0.2f), 1.f))
			.Padding(FMargin(12, 6))
			.OnMouseButtonDown_Lambda([this, C](const FGeometry&, const FPointerEvent&) {
				if (OnCultureChanged) OnCultureChanged(C);
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(FText::FromString(C.ToUpper())).Font(Font(15, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Text))
			]
		];
	}

	TSharedPtr<SEditableTextBox> NameBox;
	TitleLayer->SetContent(
		SNew(SBorder).BorderImage(Solid(FLinearColor(0.02f, 0.01f, 0.05f, 0.45f))).HAlign(HAlign_Center).VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[
				SNew(STextBlock).Text(L(TEXT("ui.title"))).Font(Font(64, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Gold)).ShadowOffset(FVector2D(3, 3)).ShadowColorAndOpacity(FLinearColor(0.3f, 0.05f, 0.2f, 0.9f))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 4, 0, 26)
			[
				SNew(STextBlock).Text(L(TEXT("ui.subtitle"))).Font(Font(20, TEXT("Italic"))).ColorAndOpacity(FSlateColor(Text)).ShadowOffset(FVector2D(1, 1))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 0, 0, 12)
			[
				SNew(STextBlock).Text(L(TEXT("ui.preference_prompt"))).Font(Font(20, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Text)).ShadowOffset(FVector2D(1, 1))
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)[Cards]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 22, 0, 0)
			[
				SNew(SBorder).BorderImage(Rounded(Ink, 16.f)).Padding(FMargin(22, 14))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
						[
							SNew(SBox).WidthOverride(150)[SNew(STextBlock).Text(L(TEXT("ui.name_prompt"))).Font(Font(16, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Muted))]
						]
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox).WidthOverride(260)
							[
								SAssignNew(NameBox, SEditableTextBox)
								.Text(FText::FromString(TitleName))
								.Font(Font(17))
								.OnTextChanged_Lambda([this](const FText& T) { TitleName = T.ToString().Left(16); })
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
						[
							SNew(SBox).WidthOverride(150)[SNew(STextBlock).Text(L(TEXT("ui.difficulty"))).Font(Font(16, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Muted))]
						]
						+ SHorizontalBox::Slot().AutoWidth()[Diffs]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
						[
							SNew(SBox).WidthOverride(150)[SNew(STextBlock).Text(L(TEXT("ui.language"))).Font(Font(16, TEXT("Bold"))).ColorAndOpacity(FSlateColor(Muted))]
						]
						+ SHorizontalBox::Slot().AutoWidth()[Langs]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 24, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0)
				[
					SNew(SBox).MinDesiredWidth(240)
					[
						MakeButton(L(TEXT("ui.start")), [this]() {
							if (OnStart) OnStart(TitleSelected, TitleDifficulty, TitleName.TrimStartAndEnd().IsEmpty() ? FString(TEXT("Sora")) : TitleName.TrimStartAndEnd());
						}, Gold, 24.f)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(10, 0)
				[
					SNew(SBox).MinDesiredWidth(160)[MakeButton(L(TEXT("ui.quit")), [this]() { if (OnQuit) OnQuit(); }, Gold, 20.f)]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 22, 0, 0)
			[
				SNew(STextBlock).Text(L(TEXT("ui.age_notice"))).Font(Font(13)).ColorAndOpacity(FSlateColor(Muted)).ShadowOffset(FVector2D(1, 1))
			]
		]);
}

void SChessSimHud::HideAll()
{
	TitleLayer->SetVisibility(EVisibility::Collapsed);
	StoryLayer->SetVisibility(EVisibility::Collapsed);
	ChoiceLayer->SetVisibility(EVisibility::Collapsed);
	ChessLayer->SetVisibility(EVisibility::Collapsed);
	PromotionLayer->SetVisibility(EVisibility::Collapsed);
	EndingLayer->SetVisibility(EVisibility::Collapsed);
	Bubble->SetVisibility(EVisibility::Collapsed);
	bChoicesVisible = false;
	bPromotionVisible = false;
}

void SChessSimHud::ShowDialogue(const FText& Speaker, const FText& InText, const FLinearColor& Accent)
{
	StoryLayer->SetVisibility(EVisibility::SelfHitTestInvisible);
	ChoiceLayer->SetVisibility(EVisibility::Collapsed);
	bChoicesVisible = false;
	const bool bHasSpeaker = !Speaker.IsEmpty();
	SpeakerPlate->SetVisibility(bHasSpeaker ? EVisibility::Visible : EVisibility::Hidden);
	SpeakerText->SetText(Speaker);
	SpeakerPlate->SetBorderImage(Rounded(Accent * FLinearColor(0.85f, 0.85f, 0.85f, 1.f), 14.f, FLinearColor(1, 1, 1, 0.5f), 1.5f));
	FullText = InText.ToString();
	Revealed = 0.f;
	DialogueText->SetText(FText::GetEmpty());
	ContinueHint->SetText(L(TEXT("ui.continue_hint")));
	ContinueHint->SetVisibility(EVisibility::Hidden);
}

bool SChessSimHud::IsTyping() const { return Revealed < FullText.Len(); }

void SChessSimHud::CompleteTyping()
{
	Revealed = FullText.Len();
	DialogueText->SetText(FText::FromString(FullText));
	ContinueHint->SetVisibility(EVisibility::Visible);
}

void SChessSimHud::ShowChoices(const FText& Prompt, const TArray<FText>& Options)
{
	ChoiceList->ClearChildren();
	if (!Prompt.IsEmpty())
	{
		StoryLayer->SetVisibility(EVisibility::SelfHitTestInvisible);
		SpeakerPlate->SetVisibility(EVisibility::Hidden);
		FullText = Prompt.ToString();
		CompleteTyping();
		ContinueHint->SetVisibility(EVisibility::Hidden);
	}
	for (int32 I = 0; I < Options.Num(); ++I)
	{
		ChoiceList->AddSlot().AutoHeight().Padding(0, 7)
		[
			MakeButton(FText::Format(FText::FromString(TEXT("{0}.  {1}")), FText::AsNumber(I + 1), Options[I]), [this, I]() { if (OnChoice) OnChoice(I); }, Gold, 20.f)
		];
	}
	ChoiceLayer->SetVisibility(EVisibility::SelfHitTestInvisible);
	bChoicesVisible = true;
}

void SChessSimHud::HideStory()
{
	StoryLayer->SetVisibility(EVisibility::Collapsed);
	ChoiceLayer->SetVisibility(EVisibility::Collapsed);
	bChoicesVisible = false;
}

void SChessSimHud::ShowChessPanel(bool bShow)
{
	ChessLayer->SetVisibility(bShow ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
	if (!bShow) Bubble->SetVisibility(EVisibility::Collapsed);
	LastMoveCount = -1;
}

void SChessSimHud::SetChessPanel(const FChessPanelState& S)
{
	ChessOpponent->SetText(S.OpponentName);
	ChessOpponent->SetColorAndOpacity(FSlateColor(S.Accent));
	ChessArchetype->SetText(S.OpponentArchetype);
	ChessAffection->SetText(Hearts(S.Affection));
	ChessStatus->SetText(S.Status);
	ChessEngine->SetText(S.EngineNote);
	// Map centipawns to a 0..1 share with a soft curve (±600 cp ~ 90%).
	EvalTarget = 0.5f + 0.5f * FMath::Clamp(static_cast<float>(S.PlayerEval) / 750.f, -0.95f, 0.95f);

	if (S.SanMoves.Num() != LastMoveCount)
	{
		LastMoveCount = S.SanMoves.Num();
		MoveList->ClearChildren();
		for (int32 I = 0; I < S.SanMoves.Num(); I += 2)
		{
			const FString Line = FString::Printf(TEXT("%3d.  %-8s %s"), I / 2 + 1, *S.SanMoves[I], I + 1 < S.SanMoves.Num() ? *S.SanMoves[I + 1] : TEXT(""));
			MoveList->AddSlot()[SNew(STextBlock).Text(FText::FromString(Line)).Font(FCoreStyle::GetDefaultFontStyle("Mono", 14)).ColorAndOpacity(FSlateColor(Text))];
		}
		MoveList->ScrollToEnd();
	}
}

void SChessSimHud::ShowReaction(const FText& InText, const FLinearColor& Accent, float Seconds)
{
	BubbleText->SetText(InText);
	BubbleAccent = Accent;
	Bubble->SetVisibility(EVisibility::HitTestInvisible);
	BubbleTime = Seconds;
}

void SChessSimHud::ShowPromotion(bool bShow)
{
	PromotionLayer->SetVisibility(bShow ? EVisibility::Visible : EVisibility::Collapsed);
	bPromotionVisible = bShow;
}

void SChessSimHud::ShowEnding(const FText& Title, const FText& Body, int32 Affection, const FLinearColor& Accent)
{
	HideAll();
	EndingTitle->SetText(Title);
	EndingTitle->SetColorAndOpacity(FSlateColor(Accent));
	EndingBody->SetText(Body);
	EndingHearts->SetText(Hearts(Affection));
	EndingLayer->SetVisibility(EVisibility::Visible);
}

void SChessSimHud::ShowToast(const FText& InText, float Seconds)
{
	ToastText->SetText(InText);
	ToastBox->SetVisibility(EVisibility::HitTestInvisible);
	ToastTime = Seconds;
}

void SChessSimHud::FadeTo(float Alpha, float Seconds)
{
	FadeTarget = Alpha;
	FadeSpeed = Seconds <= 0.f ? 1000.f : 1.f / Seconds;
}

void SChessSimHud::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float Dt)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, Dt);
	if (StoryLayer->GetVisibility() != EVisibility::Collapsed && Revealed < FullText.Len())
	{
		Revealed = FMath::Min<float>(FullText.Len(), Revealed + Dt * CharsPerSecond);
		DialogueText->SetText(FText::FromString(FullText.Left(FMath::FloorToInt(Revealed))));
		if (Revealed >= FullText.Len()) ContinueHint->SetVisibility(EVisibility::Visible);
	}
	if (BubbleTime > 0.f && (BubbleTime -= Dt) <= 0.f) Bubble->SetVisibility(EVisibility::Collapsed);
	if (ToastTime > 0.f && (ToastTime -= Dt) <= 0.f) ToastBox->SetVisibility(EVisibility::Collapsed);
	Fade = FMath::FInterpConstantTo(Fade, FadeTarget, Dt, FadeSpeed);
	EvalFraction = FMath::FInterpTo(EvalFraction, EvalTarget, Dt, 4.f);
}
