// SPDX-License-Identifier: Apache-2.0
#include "Game/ChessSimPlayerController.h"

#include "Chess/ChessCore.h"
#include "Engine/World.h"
#include "Game/ChessSimGameMode.h"
#include "InputCoreTypes.h"

AChessSimPlayerController::AChessSimPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	DefaultMouseCursor = EMouseCursor::Hand;
}

void AChessSimPlayerController::BeginPlay()
{
	Super::BeginPlay();
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
}

bool AChessSimPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
	AChessSimGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AChessSimGameMode>() : nullptr;
	if (!GM || Params.Event != IE_Pressed) return Super::InputKey(Params);

	const FKey& K = Params.Key;
	if (K == EKeys::LeftMouseButton)
	{
		FHitResult Hit;
		const bool bHit = GetHitResultUnderCursor(ECC_Visibility, false, Hit);
		GM->ClickWorld(Hit.ImpactPoint, bHit);
		return true;
	}
	if (K == EKeys::SpaceBar || K == EKeys::Enter || K == EKeys::Right)
	{
		GM->Advance();
		return true;
	}
	const FKey Numbers[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four};
	for (int32 I = 0; I < 4; ++I)
	{
		if (K == Numbers[I])
		{
			GM->Choose(I);
			return true;
		}
	}
	if (K == EKeys::H) { GM->Hint(); return true; }
	if (K == EKeys::R) { GM->Resign(); return true; }
	// Promotion shortcuts.
	if (K == EKeys::Q) { GM->Promote(chesscore::PieceType::Queen); return true; }
	if (K == EKeys::B) { GM->Promote(chesscore::PieceType::Bishop); return true; }
	if (K == EKeys::N) { GM->Promote(chesscore::PieceType::Knight); return true; }
	if (K == EKeys::T) { GM->Promote(chesscore::PieceType::Rook); return true; }
	if (K == EKeys::Escape && GM->GetFlow() == EGameFlow::Ending) { GM->BackToTitle(); return true; }
	return Super::InputKey(Params);
}
