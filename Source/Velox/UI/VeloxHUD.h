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

protected:
	/** Q3-style server-confirmed hit marker: four ticks around the centre, longer and red on a kill (ClientConfirmHit state). */
	void DrawHitMarker();
	/** Death overlay while the player waits for a respawn (countdown to MinRespawnDelay, then the prompt). */
	void DrawDeathOverlay();
};
