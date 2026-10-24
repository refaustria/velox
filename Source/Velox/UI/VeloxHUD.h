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

	//~AHUD interface
	/** M1 debug block: movement speed, ground/air, grace, latch, corrections, fps, ping (cvar velox.hud.Movement). */
	virtual void DrawHUD() override;
	//~End of AHUD interface
};
