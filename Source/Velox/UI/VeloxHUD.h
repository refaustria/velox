// Velox - HUD. Canvas debug HUD (speed, velocity, FPS, ping) in M1; UMG via UVeloxUserWidget from M3.

#pragma once

#include "GameFramework/HUD.h"

#include "VeloxHUD.generated.h"

UCLASS(Config = Game)
class VELOX_API AVeloxHUD : public AHUD
{
	GENERATED_BODY()

public:
	AVeloxHUD(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
