// SPDX-License-Identifier: Apache-2.0
#include "Game/ChessSimGameMode.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ChessSimMark.h"
#include "ChessSimSettings.h"
#include "Characters/OpponentCharacter.h"
#include "Chess/ChessBoardActor.h"
#include "Chess/ChessEngineSubsystem.h"
#include "Chess/ChessMatch.h"
#include "Data/GameDataSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Game/ChessSimPlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UnrealClient.h"
#include "ShaderCompiler.h"
#include "Misc/Paths.h"
#include "UI/ChessSimHud.h"
#include "Widgets/SWeakWidget.h"

using namespace chesscore;

namespace
{
double Now() { return FPlatformTime::Seconds(); }

/** Default layout used when the level does not provide tagged actors (see Tools/build_map.py). */
const FVector DefaultBoardLocation(0, 0, 100.4f);
const FVector DefaultOpponentLocation(0, 74, 0);
} // namespace

AChessSimGameMode::AChessSimGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	DefaultPawnClass = nullptr;
	PlayerControllerClass = AChessSimPlayerController::StaticClass();
}

UGameDataSubsystem* AChessSimGameMode::Data() const { return GetGameInstance()->GetSubsystem<UGameDataSubsystem>(); }
UChessEngineSubsystem* AChessSimGameMode::Engine() const { return GetGameInstance()->GetSubsystem<UChessEngineSubsystem>(); }

void AChessSimGameMode::BeginPlay()
{
	Super::BeginPlay();
	Random.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
	SetupScene();
	CreateHud();
	Match = NewObject<UChessMatch>(this);
	Match->OnReaction.AddUObject(this, &AChessSimGameMode::OnMatchReaction);
	Match->OnChanged.AddUObject(this, &AChessSimGameMode::OnMatchChanged);
	Match->OnOver.AddUObject(this, &AChessSimGameMode::OnMatchOver);
	Match->OnHint = [this](const ChessMove& M) {
		if (Hud) Hud->ShowToast(FText::Format(FText::FromString(TEXT("{0}: {1}")), Data()->GetLine(TEXT("ui.hint")),
			FText::FromString(UTF8_TO_TCHAR(Match->GetGame().Current().ToSan(M).c_str()))), 4.f);
	};

	for (const FString& E : Data()->GetLoadErrors())
		if (Hud) Hud->ShowToast(FText::FromString(TEXT("Data: ") + E), 8.f);

	// Automation hooks: -ChessSimAutoPilot[=<opponent>] plays unattended; -ChessSimAutoStart=<opponent> skips the title.
	if (FParse::Param(FCommandLine::Get(), TEXT("ChessSimAutoPilot")) || FParse::Value(FCommandLine::Get(), TEXT("ChessSimAutoPilot="), AutoOpponentId))
	{
		bAutoPilot = true;
		AutoNext = Now() + 4.0; // let shaders/streaming settle
		UE_LOG(LogChessSim, Display, TEXT("AutoPilot enabled (opponent '%s')"), *AutoOpponentId);
	}
	FString AutoOpponent;
	if (FParse::Value(FCommandLine::Get(), TEXT("ChessSimAutoStart="), AutoOpponent))
	{
		StartGame(AutoOpponent, EChessDifficulty::Novice, TEXT("Sora"));
		return;
	}
	ShowTitle();
}

void AChessSimGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Match) Match->Abort();
	if (Hud && GEngine && GEngine->GameViewport) GEngine->GameViewport->RemoveViewportWidgetContent(Hud.ToSharedRef());
	Hud.Reset();
	Super::EndPlay(Reason);
}

void AChessSimGameMode::SetupScene()
{
	UWorld* World = GetWorld();
	if (TActorIterator<AChessBoardActor> It(World); It) Board = *It;
	if (TActorIterator<AOpponentCharacter> It(World); It) Opponent = *It;

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!Board) Board = World->SpawnActor<AChessBoardActor>(DefaultBoardLocation, FRotator::ZeroRotator, P);
	if (!Opponent) Opponent = World->SpawnActor<AOpponentCharacter>(DefaultOpponentLocation, FRotator(0, -90, 0), P);

	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		for (const FName& Tag : It->Tags)
		{
			FString S = Tag.ToString();
			if (S.RemoveFromStart(TEXT("cam."))) Shots.Add(S, *It);
		}
	}

	// Derive any missing shots from the board/opponent layout.
	const FVector B = Board->GetActorLocation();
	const FVector Toward = (Opponent->GetActorLocation() - B).GetSafeNormal2D(); // board -> opponent
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Toward);
	const FVector Head = Opponent->GetActorLocation() + FVector(0, 0, 150.f);
	// Board shot: behind the player's side, pitched so the whole board and the opponent's face fit.
	const float BoardPitch = FMath::DegreesToRadians(27.f);
	const FVector BoardCam = B - Toward * 82.f + FVector(0, 0, 66.f);
	const FVector BoardLook = BoardCam + (Toward * FMath::Cos(BoardPitch) - FVector::UpVector * FMath::Sin(BoardPitch)) * 100.f;
	auto Ensure = [&](const TCHAR* Name, const FVector& Loc, const FVector& LookAt, float Fov) {
		if (Shots.Contains(Name)) return;
		ACameraActor* Cam = World->SpawnActor<ACameraActor>(Loc, (LookAt - Loc).Rotation(), P);
		Cam->GetCameraComponent()->SetFieldOfView(Fov);
		Cam->GetCameraComponent()->bConstrainAspectRatio = false;
		Cam->Tags.Add(FName(FString(TEXT("cam.")) + Name));
		Shots.Add(Name, Cam);
	};
	Ensure(TEXT("board"), BoardCam, BoardLook, 80.f);
	Ensure(TEXT("conversation"), B - Toward * 95.f + Side * 18.f + FVector(0, 0, 55.f), Head - FVector(0, 0, 12.f), 45.f);
	Ensure(TEXT("closeup"), Head - Toward * 85.f + Side * 10.f + FVector(0, 0, -8.f), Head - FVector(0, 0, 4.f), 32.f);
	Ensure(TEXT("wide"), B - Toward * 290.f + Side * 160.f + FVector(0, 0, 70.f), B + Toward * 40.f + FVector(0, 0, 30.f), 60.f);

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		if (ACameraActor* Wide = Shots.FindRef(TEXT("wide"))) PC->SetViewTarget(Wide);
	}
	CurrentShot = TEXT("wide");
}

void AChessSimGameMode::CreateHud()
{
	if (!GEngine || !GEngine->GameViewport) return; // headless (e.g. -nullrhi tests)
	Hud = SNew(SChessSimHud);
	TWeakObjectPtr<AChessSimGameMode> Self(this);
	Hud->Loc = [Self](const FString& Key) { return Self.IsValid() ? Self->Data()->GetLine(Key) : FText::FromString(Key); };
	Hud->OnAdvance = [Self]() { if (Self.IsValid()) Self->Advance(); };
	Hud->OnChoice = [Self](int32 I) { if (Self.IsValid()) Self->Choose(I); };
	Hud->OnHint = [Self]() { if (Self.IsValid()) Self->Hint(); };
	Hud->OnResign = [Self]() { if (Self.IsValid()) Self->Resign(); };
	Hud->OnPromote = [Self](PieceType T) { if (Self.IsValid()) Self->Promote(T); };
	Hud->OnReturnToTitle = [Self]() { if (Self.IsValid()) Self->BackToTitle(); };
	Hud->OnQuit = [Self]() {
		if (Self.IsValid()) UKismetSystemLibrary::QuitGame(Self.Get(), nullptr, EQuitPreference::Quit, false);
	};
	Hud->OnCultureChanged = [Self](const FString& Culture) {
		if (!Self.IsValid()) return;
		Self->Data()->SetCulture(Culture);
		Self->ShowTitle();
	};
	Hud->OnStart = [Self](const FString& Opp, EChessDifficulty D, const FString& Name) {
		if (Self.IsValid()) Self->StartGame(Opp, D, Name);
	};
	Hud->GetBubbleAnchor = [Self]() { return Self.IsValid() ? Self->BubbleAnchor() : TOptional<FVector2D>(); };
	GEngine->GameViewport->AddViewportWidgetContent(SNew(SWeakWidget).PossiblyNullContent(Hud.ToSharedRef()), 10);
}

FLinearColor AChessSimGameMode::AccentFor(const FString& CharacterId) const
{
	if (CharacterId == TEXT("player")) return FLinearColor(0.35f, 0.75f, 0.55f);
	if (const FCharacterDef* C = Data()->FindCharacter(CharacterId)) return C->Accent;
	return FLinearColor(0.45f, 0.45f, 0.55f);
}

TOptional<FVector2D> AChessSimGameMode::BubbleAnchor() const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC || !Opponent) return {};
	FVector2D Screen;
	if (!PC->ProjectWorldLocationToScreen(Opponent->GetHeadWorldLocation(), Screen, true)) return {};
	const float Scale = UWidgetLayoutLibrary::GetViewportScale(PC);
	FVector2D Local = Screen / FMath::Max(Scale, 0.01f);
	// The canvas slot aligns the bubble's bottom-centre to this point; keep it fully on screen
	// and clear of the chess side panel.
	FVector2D ViewSize;
	if (GEngine && GEngine->GameViewport) GEngine->GameViewport->GetViewportSize(ViewSize);
	ViewSize /= FMath::Max(Scale, 0.01f);
	Local.X = FMath::Clamp(Local.X, 230.0, FMath::Max(230.0, ViewSize.X - 600.0));
	Local.Y = FMath::Clamp(Local.Y, 160.0, FMath::Max(160.0, ViewSize.Y - 260.0));
	return Local;
}

void AChessSimGameMode::ShowTitle()
{
	Flow = EGameFlow::Title;
	if (Match) Match->Abort();
	BlendTo(TEXT("wide"), 0.f);
	if (Board) Board->ClearHighlights(false);
	if (!Hud) return;
	TArray<FTitleOpponent> Opps;
	for (const FCharacterDef* C : Data()->GetOpponents())
	{
		FTitleOpponent O;
		O.Id = C->Id;
		O.Name = Data()->GetCharacterName(C->Id);
		O.Archetype = FText::FromString(C->Archetype);
		O.Blurb = Data()->GetLine(C->BlurbKey);
		O.Accent = C->Accent;
		O.bFemale = C->bFemale;
		Opps.Add(O);
	}
	Hud->FadeTo(0.f, 0.6f);
	Hud->ShowTitle(Opps, Data()->GetAvailableCultures(), Data()->GetCulture(), Difficulty, Data()->GetPlayerName(), OpponentId);
}

void AChessSimGameMode::StartGame(const FString& InOpponent, EChessDifficulty InDifficulty, const FString& PlayerName)
{
	const FCharacterDef* Def = Data()->FindCharacter(InOpponent);
	if (!Def)
	{
		const TArray<const FCharacterDef*> Opps = Data()->GetOpponents();
		if (Opps.Num() == 0)
		{
			UE_LOG(LogChessSim, Error, TEXT("No opponents defined in Characters.csv"));
			return;
		}
		Def = Opps[0];
	}
	OpponentId = Def->Id;
	Difficulty = InDifficulty;
	Data()->SetActiveOpponent(OpponentId);
	Data()->SetPlayerName(PlayerName);
	if (Opponent) Opponent->ApplyCharacter(*Def);

	if (Hud)
	{
		Hud->HideAll();
		Hud->ShowChessPanel(false);
	}
	if (Board) Board->SyncToPosition(Position::Start());
	Runner.Start(Data());
	Flow = EGameFlow::Story;
	if (!Engine()->IsStockfishAvailable() && Hud) Hud->ShowToast(Data()->GetLine(TEXT("ui.engine_missing")), 6.f);
	RunScript();
}

void AChessSimGameMode::BlendTo(const FString& Shot, float Seconds)
{
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ACameraActor* Cam = Shots.FindRef(Shot);
	if (!PC || !Cam) return;
	if (Seconds < 0.f) Seconds = GetDefault<UChessSimSettings>()->CameraBlendSeconds;
	CurrentShot = Shot;
	PC->SetViewTargetWithBlend(Cam, Seconds, EViewTargetBlendFunction::VTBlend_Linear);
}

void AChessSimGameMode::RunScript()
{
	for (int32 Guard = 0; Guard < 256; ++Guard)
	{
		const FScriptEvent E = Runner.Advance();
		switch (E.Type)
		{
		case EScriptEventType::Line:
			HandleLine(E);
			return;
		case EScriptEventType::Choice:
		{
			if (E.Emotion.IsSet() && Opponent) Opponent->SetEmotion(*E.Emotion);
			TArray<FText> Options;
			for (const FScriptChoice& C : E.Choices) Options.Add(Data()->GetLine(C.TextKey));
			if (Opponent) Opponent->SetTalking(false);
			if (Hud) Hud->ShowChoices(E.TextKey.IsEmpty() ? FText::GetEmpty() : Data()->GetLine(E.TextKey), Options);
			return;
		}
		case EScriptEventType::Camera:
			BlendTo(E.Arg);
			continue;
		case EScriptEventType::StartChess:
			StartChess(E.Arg);
			return;
		case EScriptEventType::End:
			ShowEnding(E.Arg);
			return;
		case EScriptEventType::Error:
			if (Hud) Hud->ShowToast(FText::FromString(TEXT("Script error: ") + E.Arg), 8.f);
			ShowTitle();
			return;
		}
	}
}

void AChessSimGameMode::HandleLine(const FScriptEvent& E)
{
	FString Speaker = E.Speaker;
	if (Speaker == TEXT("{opponent}")) Speaker = OpponentId;
	const bool bOpponentSpeaks = Speaker == OpponentId;
	FText Name;
	if (Speaker == TEXT("player")) Name = FText::FromString(Data()->GetPlayerName());
	else if (Speaker == TEXT("narrator")) Name = FText::GetEmpty();
	else Name = Data()->GetCharacterName(Speaker);

	if (Opponent)
	{
		if (E.Emotion.IsSet()) Opponent->React(*E.Emotion);
		Opponent->SetTalking(bOpponentSpeaks);
	}
	TalkUntil = bOpponentSpeaks ? Now() + 30.0 : 0.0;
	if (Hud) Hud->ShowDialogue(Name, Data()->GetLine(E.TextKey), AccentFor(Speaker));
}

void AChessSimGameMode::Advance()
{
	if (Flow != EGameFlow::Story || Runner.IsAwaitingChoice()) return;
	if (Hud && Hud->IsTyping())
	{
		Hud->CompleteTyping();
		return;
	}
	RunScript();
}

void AChessSimGameMode::Choose(int32 Index)
{
	if (Flow != EGameFlow::Story || !Runner.IsAwaitingChoice()) return;
	if (!Runner.Choose(Index)) return;
	if (Hud) Hud->HideStory();
	RunScript();
}

void AChessSimGameMode::StartChess(const FString& ColorArg)
{
	Color PlayerColor = Color::White;
	if (ColorArg == TEXT("black")) PlayerColor = Color::Black;
	else if (ColorArg == TEXT("random")) PlayerColor = Random.FRand() < 0.5f ? Color::White : Color::Black;

	Flow = EGameFlow::Chess;
	AutoPlayerMoves = 0;
	ClearSelection();
	if (Hud)
	{
		Hud->HideStory();
		Hud->ShowChessPanel(true);
	}
	if (Opponent)
	{
		Opponent->SetTalking(false);
		Opponent->SetEmotion(EEmotion::Determined);
	}
	if (CurrentShot != TEXT("board")) BlendTo(TEXT("board"));
	Match->Begin(Engine(), Board, PlayerColor, Difficulty);
	OnMatchChanged();
}

void AChessSimGameMode::OnMatchReaction(const FString& Trigger)
{
	FRandomStream& R = Random;
	const FReactionDef* Def = Data()->PickReaction(OpponentId, Trigger, R);
	if (!Def) return;
	if (Opponent) Opponent->React(Def->Emotion);
	if (Hud) Hud->ShowReaction(Data()->GetLine(Def->TextKey), AccentFor(OpponentId), GetDefault<UChessSimSettings>()->ReactionSeconds);
	if (Opponent) Opponent->SetTalking(true);
	TalkUntil = Now() + 1.4;
	LookAtPlayerUntil = Now() + 2.5;
}

FText AChessSimGameMode::StatusText() const
{
	if (!Match) return FText::GetEmpty();
	switch (Match->GetPhase())
	{
	case EMatchPhase::PlayerTurn:
		return Match->GetGame().Current().InCheck()
			? FText::Format(FText::FromString(TEXT("{0}  {1}")), Data()->GetLine(TEXT("ui.check")), Data()->GetLine(TEXT("ui.your_turn")))
			: Data()->GetLine(TEXT("ui.your_turn"));
	case EMatchPhase::Analyzing:
	case EMatchPhase::OpponentThinking:
		return Data()->GetLine(TEXT("ui.thinking"));
	case EMatchPhase::Animating:
		return FText::FromString(TEXT("..."));
	case EMatchPhase::Over:
	{
		const TCHAR* Key = Match->GetResult() == EChessResult::Win ? TEXT("ui.result.win")
			: Match->GetResult() == EChessResult::Lose ? TEXT("ui.result.lose") : TEXT("ui.result.draw");
		const TCHAR* Why = nullptr;
		switch (Match->GetEndStatus())
		{
		case GameStatus::Checkmate: Why = TEXT("ui.status.checkmate"); break;
		case GameStatus::Stalemate: Why = TEXT("ui.status.stalemate"); break;
		case GameStatus::DrawFiftyMove: Why = TEXT("ui.status.fifty"); break;
		case GameStatus::DrawThreefold: Why = TEXT("ui.status.threefold"); break;
		case GameStatus::DrawInsufficientMaterial: Why = TEXT("ui.status.insufficient"); break;
		default: break;
		}
		return Why ? FText::Format(FText::FromString(TEXT("{0}\n{1}")), Data()->GetLine(Key), Data()->GetLine(Why)) : Data()->GetLine(Key);
	}
	default:
		return FText::GetEmpty();
	}
}

void AChessSimGameMode::OnMatchChanged()
{
	if (!Hud || !Match || Flow != EGameFlow::Chess) return;
	const FCharacterDef* Def = Data()->FindCharacter(OpponentId);
	FChessPanelState S;
	S.OpponentName = Data()->GetCharacterName(OpponentId);
	S.OpponentArchetype = FText::Format(FText::FromString(TEXT("{0} \u00B7 {1}")), FText::FromString(Def ? Def->Archetype : FString()),
		Data()->GetLine(TEXT("ui.difficulty.") + ChessDifficulty::ToString(Difficulty)));
	S.Accent = AccentFor(OpponentId);
	S.Status = StatusText();
	S.Affection = Runner.GetAffection();
	S.PlayerEval = Match->GetPlayerEval();
	for (const std::string& San : Match->GetGame().SanMoves()) S.SanMoves.Add(UTF8_TO_TCHAR(San.c_str()));
	S.bPlayerTurn = Match->IsPlayerTurn();
	S.EngineNote = FText::Format(FText::FromString(TEXT("{0}\n{1}")), FText::FromString(Engine()->GetEngineDescription()), Data()->GetLine(TEXT("ui.controls")));
	Hud->SetChessPanel(S);
	if (Opponent && Match->GetPhase() == EMatchPhase::OpponentThinking && Opponent->GetEmotion() == EEmotion::Neutral)
		Opponent->SetEmotion(EEmotion::Thinking);
}

void AChessSimGameMode::OnMatchOver(EChessResult Result, GameStatus)
{
	ClearSelection();
	Runner.SetChessResult(Result);
	ResumeAt = Now() + 3.5; // let the final reaction land
	OnMatchChanged();
}

void AChessSimGameMode::ResumeAfterChess()
{
	ResumeAt = 0.0;
	if (Flow != EGameFlow::Chess) return;
	Flow = EGameFlow::Story;
	if (Hud) Hud->ShowChessPanel(false);
	BlendTo(TEXT("conversation"));
	RunScript();
}

void AChessSimGameMode::ShowEnding(const FString& EndingId)
{
	Flow = EGameFlow::Ending;
	if (Opponent) Opponent->SetTalking(false);
	BlendTo(TEXT("closeup"));
	if (!Hud) return;
	Hud->ShowEnding(Data()->GetLine(FString::Printf(TEXT("ending.%s.title"), *EndingId)),
		Data()->GetLine(FString::Printf(TEXT("ending.%s.body"), *EndingId)), Runner.GetAffection(), AccentFor(OpponentId));
}

void AChessSimGameMode::BackToTitle()
{
	if (Match) Match->Abort();
	if (Board) Board->SyncToPosition(Position::Start());
	ShowTitle();
}

void AChessSimGameMode::ClearSelection()
{
	Selected = NoSquare;
	PendingPromotion.Reset();
	if (Hud) Hud->ShowPromotion(false);
	if (Board)
	{
		Board->SetLifted(NoSquare);
		Board->ClearHighlights(true);
		if (Match && Match->GetGame().Current().InCheck())
			Board->SetCheck(Match->GetGame().Current().KingSquare(Match->GetGame().Current().SideToMove()));
	}
}

void AChessSimGameMode::ClickWorld(const FVector& WorldLocation, bool bHit)
{
	if (Flow == EGameFlow::Story)
	{
		Advance();
		return;
	}
	if (Flow != EGameFlow::Chess || !Match || !Board || !Match->IsPlayerTurn() || PendingPromotion.IsSet()) return;

	Square Sq;
	if (!bHit || !Board->WorldToSquare(WorldLocation, Sq))
	{
		ClearSelection();
		return;
	}
	const Position& Pos = Match->GetGame().Current();
	if (Selected != NoSquare)
	{
		if (Sq == Selected)
		{
			ClearSelection();
			return;
		}
		const ChessMove Plain{Selected, Sq, PieceType::None};
		if (Match->NeedsPromotion(Selected, Sq))
		{
			PendingPromotion = ChessMove{Selected, Sq, PieceType::Queen};
			if (Hud) Hud->ShowPromotion(true);
			return;
		}
		if (Pos.IsLegal(Plain))
		{
			Selected = NoSquare;
			Board->SetLifted(NoSquare);
			Match->TryPlayerMove(Plain);
			return;
		}
	}
	const Piece& Pc = Pos.At(Sq);
	if (!Pc.IsEmpty() && Pc.Side == Match->GetPlayerColor())
	{
		Selected = Sq;
		const std::vector<ChessMove> Moves = Pos.LegalMovesFrom(Sq);
		Board->ShowSelection(Sq, TArray<ChessMove>(Moves.data(), static_cast<int32>(Moves.size())), Pos);
		if (Pos.InCheck()) Board->SetCheck(Pos.KingSquare(Pos.SideToMove()));
	}
	else
	{
		ClearSelection();
	}
}

void AChessSimGameMode::Promote(PieceType Type)
{
	if (!PendingPromotion.IsSet() || !Match) return;
	ChessMove M = *PendingPromotion;
	M.Promotion = Type;
	PendingPromotion.Reset();
	Selected = NoSquare;
	if (Hud) Hud->ShowPromotion(false);
	Match->TryPlayerMove(M);
}

void AChessSimGameMode::Hint()
{
	if (Flow == EGameFlow::Chess && Match) Match->RequestHint();
}

void AChessSimGameMode::Resign()
{
	if (Flow != EGameFlow::Chess || !Match || Match->GetPhase() == EMatchPhase::Over) return;
	if (Now() < ResignArmedUntil)
	{
		ResignArmedUntil = 0.0;
		Match->Resign();
		return;
	}
	ResignArmedUntil = Now() + 3.0;
	if (Hud) Hud->ShowToast(Data()->GetLine(TEXT("ui.resign_confirm")), 3.f);
}

bool AChessSimGameMode::Shot(const FString& Name)
{
	if (AutoShotsTaken.Contains(Name)) return false;
	AutoShotsTaken.Add(Name);
	// Offscreen capture can lag the request by several frames; hold state still meanwhile.
	AutoNext = Now() + 1.5;
	const FString Path = FPaths::ProjectSavedDir() / TEXT("AutoPilot") / (Name + TEXT(".png"));
	FScreenshotRequest::RequestScreenshot(Path, /*bShowUI*/ true, /*bAddFilenameSuffix*/ false);
	UE_LOG(LogChessSim, Display, TEXT("AutoPilot screenshot %s"), *Path);
	return true;
}

void AChessSimGameMode::TickAutoPilot()
{
	if (!bAutoPilot || Now() < AutoNext) return;
	AutoNext = Now() + 0.5;
	// Screenshots taken while materials compile show the WorldGrid fallback; wait them out.
	if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0)
	{
		AutoNext = Now() + 1.0;
		return;
	}
	switch (Flow)
	{
	case EGameFlow::Title:
		if (Shot(TEXT("01_title"))) return;
		if (AutoGames > 0)
		{
			UE_LOG(LogChessSim, Display, TEXT("AutoPilot finished: loop complete"));
			bAutoPilot = false;
			UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
			return;
		}
		StartGame(AutoOpponentId, EChessDifficulty::Novice, TEXT("Sora"));
		AutoNext = Now() + 2.5;
		return;
	case EGameFlow::Story:
		if (Runner.IsAwaitingChoice())
		{
			if (Shot(FString::Printf(TEXT("03_choice_%s"), *Runner.GetCurrentScene()))) return;
			Choose(0);
			AutoNext = Now() + 1.2;
			return;
		}
		if (Hud && Hud->IsTyping())
		{
			Hud->CompleteTyping();
			AutoNext = Now() + 0.3;
			return;
		}
		++AutoLines;
		if (AutoLines >= 3 && Shot(TEXT("02_prologue"))) return;
		if (CurrentShot == TEXT("conversation") && Shot(FString::Printf(TEXT("04_conversation_%s"), *Runner.GetCurrentScene()))) return;
		if (CurrentShot == TEXT("closeup") && Shot(FString::Printf(TEXT("05_closeup_%s"), *Runner.GetCurrentScene()))) return;
		AutoNext = Now() + 0.6;
		Advance();
		return;
	case EGameFlow::Chess:
		if (!Match) return;
		if (Match->GetPhase() == EMatchPhase::PlayerTurn)
		{
			if (AutoPlayerMoves == 2 && Shot(TEXT("06_chess_opening"))) return;
			if (AutoPlayerMoves == 6 && !AutoShotsTaken.Contains(TEXT("07_chess_selection")))
			{
				// Show selection highlights, then continue next tick.
				for (Square S = 0; S < 64; ++S)
				{
					const Piece& Pc = Match->GetGame().Current().At(S);
					if (Pc.Side == Match->GetPlayerColor() && Pc.Type == PieceType::Knight && !Match->GetGame().Current().LegalMovesFrom(S).empty())
					{
						Board->ShowSelection(S, TArray<ChessMove>(Match->GetGame().Current().LegalMovesFrom(S).data(),
							static_cast<int32>(Match->GetGame().Current().LegalMovesFrom(S).size())), Match->GetGame().Current());
						break;
					}
				}
				Shot(TEXT("07_chess_selection"));
				++AutoPlayerMoves;
				return;
			}
			if (AutoPlayerMoves >= 60)
			{
				Match->Resign();
				return;
			}
			ClearSelection();
			const ChessMove M = PickFallbackMove(Match->GetGame().Current(), 2, static_cast<uint32>(AutoPlayerMoves));
			if (M.IsValid())
			{
				Match->TryPlayerMove(M.Promotion != PieceType::None ? ChessMove{M.From, M.To, PieceType::Queen} : M);
				++AutoPlayerMoves;
			}
		}
		else if (Match->GetPhase() == EMatchPhase::Over)
		{
			Shot(TEXT("08_chess_over"));
		}
		return;
	case EGameFlow::Ending:
		if (Shot(TEXT("09_ending"))) return;
		++AutoGames;
		BackToTitle();
		return;
	}
}

void AChessSimGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TickAutoPilot();
	if (ResumeAt > 0.0 && Now() >= ResumeAt) ResumeAfterChess();

	if (Opponent)
	{
		if (TalkUntil > 0.0 && Flow == EGameFlow::Story && Hud && !Hud->IsTyping()) TalkUntil = 0.0;
		if (TalkUntil > 0.0 && Now() > TalkUntil) TalkUntil = 0.0;
		if (TalkUntil == 0.0) Opponent->SetTalking(false);

		// Gaze: the player (camera) in conversation; the board while playing, unless reacting.
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		const FVector CamLoc = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
		if (Flow == EGameFlow::Chess && Board && Now() > LookAtPlayerUntil) Opponent->SetLookTarget(Board->GetActorLocation());
		else Opponent->SetLookTarget(CamLoc);
	}
}
