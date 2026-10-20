// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ChessSimPlayerController.generated.h"

/** Mouse-and-keyboard input: clicks pick board squares / advance dialogue; keys map to game intents. */
UCLASS()
class CHESSSIMMARK_API AChessSimPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AChessSimPlayerController();
	virtual void BeginPlay() override;
	virtual bool InputKey(const FInputKeyEventArgs& Params) override;
};
