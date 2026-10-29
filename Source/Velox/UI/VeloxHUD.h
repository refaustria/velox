// Velox - HUD. M1 Canvas debug block (speed, velocity, FPS, ping, non-shipping); since M3 (ADR 0004 section 7) the player
// HUD is UVeloxHUDWidget (UMG), created here from the config-selected class and fed by a UVeloxHUDViewModel refreshed in Tick.

#pragma once

#include "GameFramework/HUD.h"
#include "Templates/SubclassOf.h"

#include "VeloxHUD.generated.h"

class UVeloxHUDViewModel;
class UVeloxHUDWidget;
class UVeloxScoreboardWidget;

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

	//~AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	//~End of AActor interface

	UVeloxHUDWidget* GetHUDWidget() const { return HUDWidget; }
	UVeloxHUDViewModel* GetViewModel() const { return ViewModel; }
	UVeloxScoreboardWidget* GetScoreboardWidget() const { return ScoreboardWidget; }

protected:
	/**
	 * The HUD widget class (DefaultGame.ini [/Script/Velox.VeloxHUD] HUDWidgetClass): UVeloxHUDWidget by default, a WBP_HUD
	 * subclass once one is authored.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|HUD")
	TSubclassOf<UVeloxHUDWidget> HUDWidgetClass;

	/** The scoreboard widget class (DefaultGame.ini): UVeloxScoreboardWidget by default, a WBP_Scoreboard subclass later. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|HUD")
	TSubclassOf<UVeloxScoreboardWidget> ScoreboardWidgetClass;

	/** Viewport z-order of the scoreboard (above the HUD, below the menus). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|HUD")
	int32 ScoreboardWidgetZOrder = 10;

	/** Viewport z-order of the HUD widget (menus and the scoreboard go above). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|HUD")
	int32 HUDWidgetZOrder = 0;

private:
	UPROPERTY(Transient)
	TObjectPtr<UVeloxHUDViewModel> ViewModel;

	UPROPERTY(Transient)
	TObjectPtr<UVeloxHUDWidget> HUDWidget;

	UPROPERTY(Transient)
	TObjectPtr<UVeloxScoreboardWidget> ScoreboardWidget;

	bool bLoggedWidget = false;
	bool bLoggedScoreboardVisible = false;
};
