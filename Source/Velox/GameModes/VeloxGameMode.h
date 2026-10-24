// Velox - base game mode. AGameMode (not AGameModeBase) for its match-state machine (ADR 0001 section 1).

#pragma once

#include "GameFramework/GameMode.h"

#include "VeloxGameMode.generated.h"

UCLASS(Config = Game)
class VELOX_API AVeloxGameMode : public AGameMode
{
	GENERATED_BODY()

public:
	AVeloxGameMode(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
