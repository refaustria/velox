// Velox - front-end game mode (ADR 0004 section 7, task M3-10): the mode of L_MainMenu. No pawn, no HUD, no match rules;
// AVeloxMenuPlayerController shows the main menu. GameDefaultMap is L_MainMenu, so a standalone game and every return
// from a failed connect or "disconnect" lands here.

#pragma once

#include "GameFramework/GameModeBase.h"

#include "VeloxMenuGameMode.generated.h"

UCLASS()
class VELOX_API AVeloxMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AVeloxMenuGameMode();

	//~AGameModeBase interface
	/** The menu never spawns a pawn. */
	virtual void RestartPlayer(AController* NewPlayer) override;
	//~End of AGameModeBase interface
};
