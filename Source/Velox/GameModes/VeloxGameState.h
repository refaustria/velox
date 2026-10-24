// Velox - game state. Match timer and item timers arrive in M3.

#pragma once

#include "GameFramework/GameState.h"

#include "VeloxGameState.generated.h"

UCLASS(Config = Game)
class VELOX_API AVeloxGameState : public AGameState
{
	GENERATED_BODY()

public:
	AVeloxGameState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
