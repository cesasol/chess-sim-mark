// SPDX-License-Identifier: Apache-2.0
#include "Chess/ChessBoardActor.h"

#include "ChessSimMark.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

using namespace chesscore;

namespace
{
const TCHAR* BasicShapeMaterialPath = TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial");
const TCHAR* ColorMaterialPath = TEXT("/Game/Materials/M_Color.M_Color");
const TCHAR* HighlightMaterialPath = TEXT("/Game/Materials/M_Highlight.M_Highlight");
const FName PieceTag(TEXT("ChessPiece"));

UStaticMesh* LoadBasicShape(const TCHAR* Name)
{
	return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Name, Name));
}

FLinearColor HighlightColor(EBoardHighlight T)
{
	switch (T)
	{
	case EBoardHighlight::Selected: return FLinearColor(1.0f, 0.85f, 0.25f, 0.55f);
	case EBoardHighlight::LegalMove: return FLinearColor(0.35f, 0.9f, 0.55f, 0.6f);
	case EBoardHighlight::LegalCapture: return FLinearColor(1.0f, 0.35f, 0.3f, 0.6f);
	case EBoardHighlight::LastMove: return FLinearColor(0.45f, 0.65f, 1.0f, 0.35f);
	case EBoardHighlight::Check: return FLinearColor(1.0f, 0.1f, 0.15f, 0.7f);
	case EBoardHighlight::Hint: return FLinearColor(0.8f, 0.4f, 1.0f, 0.6f);
	}
	return FLinearColor::White;
}

/** Offset that puts a mesh's bounding-box bottom centre at the component origin. */
FVector BaseOffset(const UStaticMeshComponent* Comp)
{
	const UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
	if (!Mesh) return FVector::ZeroVector;
	const FBox Box = Mesh->GetBoundingBox();
	const FVector Scale = Comp->GetRelativeScale3D();
	const FVector Centre = Box.GetCenter() * Scale;
	const FVector Rotated = Comp->GetRelativeRotation().RotateVector(FVector(Centre.X, Centre.Y, 0.f));
	return -FVector(Rotated.X, Rotated.Y, Box.Min.Z * Scale.Z);
}
} // namespace

AChessBoardActor::AChessBoardActor()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	Pivot->SetupAttachment(Root);

	BoardVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BoardVisual"));
	BoardVisual->SetupAttachment(Pivot);
	BoardVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	PickVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("PickVolume"));
	PickVolume->SetupAttachment(Pivot);
	PickVolume->SetCollisionProfileName(TEXT("BlockAll"));
	PickVolume->SetHiddenInGame(true);

	// Default to the imported Poly Haven set (asset names as produced by Tools/import_assets.py).
	static const TCHAR* Names[] = {TEXT("pawn"), TEXT("knight"), TEXT("bishop"), TEXT("rook"), TEXT("queen"), TEXT("king")};
	for (const TCHAR* N : Names)
	{
		const bool bPaired = FCString::Strcmp(N, TEXT("pawn")) == 0 || FCString::Strcmp(N, TEXT("knight")) == 0 ||
			FCString::Strcmp(N, TEXT("bishop")) == 0 || FCString::Strcmp(N, TEXT("rook")) == 0;
		const TCHAR* Suffix = bPaired ? TEXT("_01") : TEXT("");
		WhiteMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
			FString::Printf(TEXT("/Game/Art/Chess/chess_set/StaticMeshes/piece_%s_white%s.piece_%s_white%s"), N, Suffix, N, Suffix))));
		BlackMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
			FString::Printf(TEXT("/Game/Art/Chess/chess_set/StaticMeshes/piece_%s_black%s.piece_%s_black%s"), N, Suffix, N, Suffix))));
	}
	BoardMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Art/Chess/chess_set/StaticMeshes/board.board")));
}

void AChessBoardActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const float Half = SquareSize * 4.f;
	PickVolume->SetBoxExtent(FVector(Half, Half, 1.f));
	PickVolume->SetRelativeLocation(FVector(0, 0, SurfaceHeight - 0.5f));
}

void AChessBoardActor::BeginPlay()
{
	Super::BeginPlay();
	ColorMaterial = LoadObject<UMaterialInterface>(nullptr, ColorMaterialPath);
	if (!ColorMaterial) ColorMaterial = LoadObject<UMaterialInterface>(nullptr, BasicShapeMaterialPath);
	SquarePieces.SetNum(64);
	BuildBoardVisual();
	SyncToPosition(Position::Start());
}

void AChessBoardActor::BuildBoardVisual()
{
	if (UStaticMesh* Imported = BoardMesh.LoadSynchronous())
	{
		bUsingImportedBoard = true;
		BoardVisual->SetStaticMesh(Imported);
		BoardVisual->SetRelativeScale3D(FVector(PieceScale));
		// Put the imported board's top surface at SurfaceHeight, centred.
		const FBox Box = Imported->GetBoundingBox();
		const FVector C = Box.GetCenter() * PieceScale;
		BoardVisual->SetRelativeLocation(FVector(-C.X, -C.Y, SurfaceHeight - Box.Max.Z * PieceScale));
		return;
	}

	// Procedural fallback: frame plus 64 tiles.
	bUsingImportedBoard = false;
	UStaticMesh* Cube = LoadBasicShape(TEXT("Cube"));
	BoardVisual->SetStaticMesh(Cube);
	const float Frame = SquareSize * 8.6f;
	BoardVisual->SetRelativeScale3D(FVector(Frame / 100.f, Frame / 100.f, SurfaceHeight / 100.f));
	BoardVisual->SetRelativeLocation(FVector(0, 0, SurfaceHeight * 0.5f - 0.05f));
	if (UMaterialInstanceDynamic* M = BoardVisual->CreateDynamicMaterialInstance(0, ColorMaterial))
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.06f, 0.03f));
	for (Square S = 0; S < 64; ++S)
	{
		UStaticMeshComponent* Tile = NewObject<UStaticMeshComponent>(this);
		Tile->SetStaticMesh(Cube);
		Tile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Tile->SetupAttachment(Pivot);
		Tile->SetRelativeScale3D(FVector(SquareSize / 100.f, SquareSize / 100.f, 0.002f));
		Tile->SetRelativeLocation(LocalSquare(S) + FVector(0, 0, -0.1f));
		Tile->RegisterComponent();
		const bool bDark = ((FileOf(S) + RankOf(S)) & 1) == 0;
		if (UMaterialInstanceDynamic* M = Tile->CreateDynamicMaterialInstance(0, ColorMaterial))
			M->SetVectorParameterValue(TEXT("Color"), bDark ? FLinearColor(0.28f, 0.16f, 0.09f) : FLinearColor(0.85f, 0.72f, 0.52f));
		ProceduralSquares.Add(Tile);
	}
}

FVector AChessBoardActor::LocalSquare(Square S) const
{
	return FVector((FileOf(S) - 3.5f) * SquareSize, (RankOf(S) - 3.5f) * SquareSize, SurfaceHeight);
}

FVector AChessBoardActor::SquareToWorld(Square S, float ZOffset) const
{
	return Pivot->GetComponentTransform().TransformPosition(LocalSquare(S) + FVector(0, 0, ZOffset));
}

bool AChessBoardActor::WorldToSquare(const FVector& World, Square& OutSquare) const
{
	const FVector L = Pivot->GetComponentTransform().InverseTransformPosition(World);
	const int32 F = FMath::FloorToInt(L.X / SquareSize + 4.f);
	const int32 R = FMath::FloorToInt(L.Y / SquareSize + 4.f);
	if (F < 0 || F > 7 || R < 0 || R > 7) return false;
	OutSquare = MakeSquare(F, R);
	return true;
}

void AChessBoardActor::SetPlayerColor(Color C)
{
	Pivot->SetRelativeRotation(FRotator(0, C == Color::White ? 0.f : 180.f, 0));
}

void AChessBoardActor::ApplyPieceLook(UStaticMeshComponent* Comp, const Piece& Pc)
{
	const int32 Idx = static_cast<int32>(Pc.Type) - 1;
	const TArray<TSoftObjectPtr<UStaticMesh>>& Set = Pc.Side == Color::White ? WhiteMeshes : BlackMeshes;
	UStaticMesh* Mesh = Set.IsValidIndex(Idx) ? Set[Idx].LoadSynchronous() : nullptr;
	if (Mesh)
	{
		Comp->SetStaticMesh(Mesh);
		Comp->SetRelativeScale3D(FVector(PieceScale));
		Comp->SetRelativeRotation(FRotator(0, ImportedPieceYaw, 0));
		Comp->EmptyOverrideMaterials();
		return;
	}

	// Basic-shape stand-ins: distinct silhouettes per type.
	const float S = SquareSize / 100.f;
	struct FShape { const TCHAR* Mesh; FVector Scale; };
	FShape Shape{TEXT("Cylinder"), FVector(0.5f, 0.5f, 0.5f)};
	switch (Pc.Type)
	{
	case PieceType::Pawn: Shape = {TEXT("Sphere"), FVector(0.45f, 0.45f, 0.6f)}; break;
	case PieceType::Knight: Shape = {TEXT("Cone"), FVector(0.55f, 0.35f, 0.85f)}; break;
	case PieceType::Bishop: Shape = {TEXT("Cone"), FVector(0.5f, 0.5f, 1.0f)}; break;
	case PieceType::Rook: Shape = {TEXT("Cylinder"), FVector(0.6f, 0.6f, 0.8f)}; break;
	case PieceType::Queen: Shape = {TEXT("Cylinder"), FVector(0.55f, 0.55f, 1.2f)}; break;
	case PieceType::King: Shape = {TEXT("Cube"), FVector(0.5f, 0.5f, 1.35f)}; break;
	default: break;
	}
	Comp->SetStaticMesh(LoadBasicShape(Shape.Mesh));
	Comp->SetRelativeScale3D(Shape.Scale * S);
	Comp->SetRelativeRotation(FRotator(0, Pc.Side == Color::White ? 90.f : -90.f, 0)); // knight cones lean toward the enemy
	if (UMaterialInstanceDynamic* M = Comp->CreateDynamicMaterialInstance(0, ColorMaterial))
		M->SetVectorParameterValue(TEXT("Color"), Pc.Side == Color::White ? FLinearColor(0.9f, 0.86f, 0.78f) : FLinearColor(0.05f, 0.04f, 0.05f));
}

UStaticMeshComponent* AChessBoardActor::MakePiece(const Piece& Pc)
{
	UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
	Comp->SetupAttachment(Pivot);
	Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Comp->ComponentTags.Add(PieceTag);
	Comp->RegisterComponent();
	ApplyPieceLook(Comp, Pc);
	return Comp;
}

void AChessBoardActor::ClearPieces()
{
	for (TObjectPtr<UStaticMeshComponent>& C : SquarePieces)
	{
		if (C) C->DestroyComponent();
		C = nullptr;
	}
	for (TObjectPtr<UStaticMeshComponent>& C : CapturedPieces)
		if (C) C->DestroyComponent();
	CapturedPieces.Reset();
	GraveyardCount[0] = GraveyardCount[1] = 0;
	Tweens.Reset();
	LiftedSquare = NoSquare;
}

void AChessBoardActor::SyncToPosition(const Position& Pos)
{
	ClearPieces();
	SquarePieces.SetNum(64);
	for (Square S = 0; S < 64; ++S)
	{
		const Piece& Pc = Pos.At(S);
		if (Pc.IsEmpty()) continue;
		UStaticMeshComponent* Comp = MakePiece(Pc);
		Comp->SetRelativeLocation(LocalSquare(S) + BaseOffset(Comp));
		SquarePieces[S] = Comp;
	}
}

FVector AChessBoardActor::NextGraveyardSlot(Color Captured)
{
	// Captured pieces line up beside the board on the capturer's right.
	const int32 Side = Captured == Color::White ? 1 : 0;
	const int32 N = GraveyardCount[Side]++;
	const float X = (Captured == Color::White ? 1.f : -1.f) * (SquareSize * 5.0f + (N / 8) * SquareSize * 0.9f);
	const float Y = (Captured == Color::White ? 1.f : -1.f) * ((N % 8) - 3.5f) * SquareSize * 0.9f;
	return FVector(X, Y, SurfaceHeight - 0.3f);
}

void AChessBoardActor::AnimateMove(const Position& Before, const ChessMove& Move, TFunction<void()> OnDone)
{
	if (SquarePieces.Num() != 64) SquarePieces.SetNum(64);
	SetLifted(NoSquare);
	const Piece Moving = Before.At(Move.From);
	UStaticMeshComponent* Comp = SquarePieces[Move.From];
	if (!Comp)
	{
		// Out of sync: recover by snapping.
		UE_LOG(LogChessSim, Warning, TEXT("Board out of sync at %hs; resyncing"), SquareName(Move.From).c_str());
		SyncToPosition(Before.Apply(Move));
		if (OnDone) OnDone();
		return;
	}

	auto AddTween = [this](UStaticMeshComponent* C, const FVector& To, float Duration, float Arc, bool bHide) {
		FTween T;
		T.Comp = C;
		T.From = C->GetRelativeLocation();
		T.To = To;
		T.Duration = Duration;
		T.ArcHeight = Arc;
		T.bHideAtEnd = bHide;
		Tweens.Add(T);
	};

	// Capture (including en passant).
	Square CaptureSq = Before.At(Move.To).IsEmpty() ? NoSquare : Move.To;
	if (Moving.Type == PieceType::Pawn && Move.To == Before.EnPassant())
		CaptureSq = MakeSquare(FileOf(Move.To), RankOf(Move.From));
	if (CaptureSq != NoSquare && SquarePieces[CaptureSq])
	{
		UStaticMeshComponent* Victim = SquarePieces[CaptureSq];
		SquarePieces[CaptureSq] = nullptr;
		CapturedPieces.Add(Victim);
		AddTween(Victim, NextGraveyardSlot(Before.At(CaptureSq).Side) + BaseOffset(Victim), MoveSeconds * 1.2f, SquareSize * 1.5f, false);
	}

	SquarePieces[Move.From] = nullptr;
	SquarePieces[Move.To] = Comp;
	const bool bKnight = Moving.Type == PieceType::Knight;
	AddTween(Comp, LocalSquare(Move.To) + BaseOffset(Comp), MoveSeconds, bKnight ? SquareSize * 1.2f : SquareSize * 0.35f, false);

	// Castling moves the rook too.
	if (Moving.Type == PieceType::King && FMath::Abs(FileOf(Move.To) - FileOf(Move.From)) == 2)
	{
		const int32 Rank = RankOf(Move.From);
		const Square RookFrom = MakeSquare(FileOf(Move.To) == 6 ? 7 : 0, Rank);
		const Square RookTo = MakeSquare(FileOf(Move.To) == 6 ? 5 : 3, Rank);
		if (UStaticMeshComponent* Rook = SquarePieces[RookFrom])
		{
			SquarePieces[RookFrom] = nullptr;
			SquarePieces[RookTo] = Rook;
			AddTween(Rook, LocalSquare(RookTo) + BaseOffset(Rook), MoveSeconds, SquareSize * 0.6f, false);
		}
	}

	const PieceType Promo = Move.Promotion;
	const Color Side = Moving.Side;
	TWeakObjectPtr<UStaticMeshComponent> WeakComp = Comp;
	const Square To = Move.To;
	OnTweensDone = [this, Promo, Side, WeakComp, To, OnDone]() {
		if (Promo != PieceType::None && WeakComp.IsValid())
		{
			ApplyPieceLook(WeakComp.Get(), Piece{Promo, Side});
			WeakComp->SetRelativeLocation(LocalSquare(To) + BaseOffset(WeakComp.Get()));
		}
		if (OnDone) OnDone();
	};
}

void AChessBoardActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Tweens.Num() == 0) return;
	for (int32 I = Tweens.Num() - 1; I >= 0; --I)
	{
		FTween& T = Tweens[I];
		if (!T.Comp.IsValid())
		{
			Tweens.RemoveAt(I);
			continue;
		}
		T.Elapsed += DeltaSeconds;
		const float A = FMath::Clamp(T.Elapsed / FMath::Max(T.Duration, 0.01f), 0.f, 1.f);
		const float E = FMath::InterpEaseInOut(0.f, 1.f, A, 2.2f);
		FVector P = FMath::Lerp(T.From, T.To, E);
		P.Z += FMath::Sin(A * PI) * T.ArcHeight;
		T.Comp->SetRelativeLocation(P);
		if (A >= 1.f)
		{
			if (T.bHideAtEnd) T.Comp->SetVisibility(false);
			Tweens.RemoveAt(I);
		}
	}
	if (Tweens.Num() == 0 && OnTweensDone)
	{
		TFunction<void()> Done = MoveTemp(OnTweensDone);
		OnTweensDone = nullptr;
		Done();
	}
}

void AChessBoardActor::SetLifted(Square S)
{
	if (LiftedSquare != NoSquare && SquarePieces.IsValidIndex(LiftedSquare) && SquarePieces[LiftedSquare])
	{
		UStaticMeshComponent* C = SquarePieces[LiftedSquare];
		C->SetRelativeLocation(LocalSquare(LiftedSquare) + BaseOffset(C));
	}
	LiftedSquare = S;
	if (S != NoSquare && SquarePieces.IsValidIndex(S) && SquarePieces[S])
	{
		UStaticMeshComponent* C = SquarePieces[S];
		C->SetRelativeLocation(LocalSquare(S) + BaseOffset(C) + FVector(0, 0, SquareSize * 0.25f));
	}
}

UStaticMeshComponent* AChessBoardActor::AcquireHighlight()
{
	if (ActiveHighlights < HighlightPool.Num())
	{
		UStaticMeshComponent* C = HighlightPool[ActiveHighlights++];
		C->SetVisibility(true);
		return C;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(LoadBasicShape(TEXT("Cylinder")));
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->SetupAttachment(Pivot);
	C->RegisterComponent();
	UMaterialInterface* HighlightMat = LoadObject<UMaterialInterface>(nullptr, HighlightMaterialPath);
	C->CreateDynamicMaterialInstance(0, HighlightMat ? HighlightMat : ColorMaterial.Get());
	HighlightPool.Add(C);
	++ActiveHighlights;
	return C;
}

void AChessBoardActor::ClearHighlights(bool bKeepLastMove)
{
	for (UStaticMeshComponent* C : HighlightPool) C->SetVisibility(false);
	ActiveHighlights = 0;
	if (bKeepLastMove)
	{
		for (Square S : LastMoveSquares) AddHighlight(S, EBoardHighlight::LastMove);
	}
	else
	{
		LastMoveSquares.Reset();
	}
}

void AChessBoardActor::AddHighlight(Square S, EBoardHighlight Type)
{
	UStaticMeshComponent* C = AcquireHighlight();
	const bool bDot = Type == EBoardHighlight::LegalMove;
	const float Diameter = SquareSize * (bDot ? 0.32f : 0.92f);
	const float Layer = 0.05f + static_cast<float>(Type) * 0.01f;
	C->SetRelativeScale3D(FVector(Diameter / 100.f, Diameter / 100.f, 0.0004f));
	C->SetRelativeLocation(LocalSquare(S) + FVector(0, 0, Layer));
	if (UMaterialInstanceDynamic* M = Cast<UMaterialInstanceDynamic>(C->GetMaterial(0)))
	{
		const FLinearColor Col = HighlightColor(Type);
		M->SetVectorParameterValue(TEXT("Color"), Col);
		M->SetScalarParameterValue(TEXT("Opacity"), Col.A);
	}
}

void AChessBoardActor::ShowSelection(Square From, const TArray<ChessMove>& Moves, const Position& Pos)
{
	ClearHighlights(true);
	AddHighlight(From, EBoardHighlight::Selected);
	for (const ChessMove& M : Moves)
	{
		if (M.Promotion != PieceType::None && M.Promotion != PieceType::Queen) continue; // one marker per square
		AddHighlight(M.To, Pos.IsCapture(M) ? EBoardHighlight::LegalCapture : EBoardHighlight::LegalMove);
	}
	SetLifted(From);
}

void AChessBoardActor::SetLastMove(const ChessMove& Move)
{
	LastMoveSquares = {Move.From, Move.To};
	ClearHighlights(true);
}

void AChessBoardActor::SetCheck(Square KingSquare)
{
	if (KingSquare != NoSquare) AddHighlight(KingSquare, EBoardHighlight::Check);
}

void AChessBoardActor::ShowHint(const ChessMove& Move)
{
	if (!Move.IsValid()) return;
	AddHighlight(Move.From, EBoardHighlight::Hint);
	AddHighlight(Move.To, EBoardHighlight::Hint);
}
