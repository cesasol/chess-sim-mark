// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "Chess/ChessCore.h"
#include "GameFramework/Actor.h"
#include "ChessBoardActor.generated.h"

class UBoxComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;
class UStaticMeshComponent;

UENUM()
enum class EBoardHighlight : uint8
{
	Selected,
	LegalMove,
	LegalCapture,
	LastMove,
	Check,
	Hint,
};

/**
 * The 3D chess board: lays out piece meshes for a chesscore::Position, animates
 * moves, shows highlights and maps world hits to squares. Uses the imported
 * CC0 chess set when present (see Tools/import_assets.py), otherwise builds
 * stand-in pieces from engine basic shapes.
 *
 * Local frame: X = files (a..h), Y = ranks (1..8), Z up; board centre at origin.
 * The player's side is -Y; flip the whole actor (yaw 180) to play Black.
 */
UCLASS()
class CHESSSIMMARK_API AChessBoardActor : public AActor
{
	GENERATED_BODY()

public:
	AChessBoardActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Snap all pieces to the given position (no animation). */
	void SyncToPosition(const chesscore::Position& Position);
	/** Animate a legal move played from Before, then call OnDone. */
	void AnimateMove(const chesscore::Position& Before, const chesscore::ChessMove& Move, TFunction<void()> OnDone);
	bool IsAnimating() const { return Tweens.Num() > 0; }

	bool WorldToSquare(const FVector& World, chesscore::Square& OutSquare) const;
	FVector SquareToWorld(chesscore::Square Square, float ZOffset = 0.f) const;

	void ClearHighlights(bool bKeepLastMove = true);
	void AddHighlight(chesscore::Square Square, EBoardHighlight Type);
	void ShowSelection(chesscore::Square From, const TArray<chesscore::ChessMove>& Moves, const chesscore::Position& Position);
	void SetLastMove(const chesscore::ChessMove& Move);
	void SetCheck(chesscore::Square KingSquare);
	void ShowHint(const chesscore::ChessMove& Move);
	/** Lift the piece on a square (selected feedback). */
	void SetLifted(chesscore::Square Square);

	/** Player plays from the near side; rotates the board so their colour faces -Y. */
	void SetPlayerColor(chesscore::Color Color);

	/** Board square edge length in cm. */
	UPROPERTY(EditAnywhere, Category = "Board")
	float SquareSize = 5.79f; // matches the Poly Haven chess_set board

	/** Height of the playing surface above the actor origin. */
	UPROPERTY(EditAnywhere, Category = "Board")
	float SurfaceHeight = 1.74f;

	UPROPERTY(EditAnywhere, Category = "Board")
	float PieceScale = 1.f;

	UPROPERTY(EditAnywhere, Category = "Board")
	float MoveSeconds = 0.45f;

	/** Yaw correction applied to imported piece meshes so knights face the opponent. */
	UPROPERTY(EditAnywhere, Category = "Board")
	float ImportedPieceYaw = 0.f;

	/** Imported meshes, indexed by PieceType-1 (pawn..king). Empty entries use basic shapes. */
	UPROPERTY(EditAnywhere, Category = "Board")
	TArray<TSoftObjectPtr<UStaticMesh>> WhiteMeshes;
	UPROPERTY(EditAnywhere, Category = "Board")
	TArray<TSoftObjectPtr<UStaticMesh>> BlackMeshes;
	UPROPERTY(EditAnywhere, Category = "Board")
	TSoftObjectPtr<UStaticMesh> BoardMesh;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Board")
	TObjectPtr<USceneComponent> Root;

	/** Rotates for Black; pieces and highlights hang below it. */
	UPROPERTY(VisibleAnywhere, Category = "Board")
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY(VisibleAnywhere, Category = "Board")
	TObjectPtr<UStaticMeshComponent> BoardVisual;

	/** Click target for mouse picking. */
	UPROPERTY(VisibleAnywhere, Category = "Board")
	TObjectPtr<UBoxComponent> PickVolume;

private:
	struct FTween
	{
		TWeakObjectPtr<UStaticMeshComponent> Comp;
		FVector From;
		FVector To;
		float Elapsed = 0.f;
		float Duration = 0.3f;
		float ArcHeight = 0.f;
		bool bHideAtEnd = false;
	};

	void BuildBoardVisual();
	UStaticMeshComponent* MakePiece(const chesscore::Piece& Piece);
	void ApplyPieceLook(UStaticMeshComponent* Comp, const chesscore::Piece& Piece);
	FVector LocalSquare(chesscore::Square Square) const;
	FVector NextGraveyardSlot(chesscore::Color CapturedColor);
	UStaticMeshComponent* AcquireHighlight();
	void ClearPieces();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> SquarePieces; // 64 entries

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CapturedPieces;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> HighlightPool;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ProceduralSquares;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> ColorMaterial;

	TArray<FTween> Tweens;
	TFunction<void()> OnTweensDone;
	int32 ActiveHighlights = 0;
	TArray<chesscore::Square> LastMoveSquares;
	chesscore::Square LiftedSquare = chesscore::NoSquare;
	int32 GraveyardCount[2] = {0, 0};
	bool bUsingImportedBoard = false;
};
