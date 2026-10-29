// Velox - front-end game mode.

#include "GameModes/VeloxMenuGameMode.h"

#include "GameModes/VeloxMenuPlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxMenuGameMode)

AVeloxMenuGameMode::AVeloxMenuGameMode()
{
	PlayerControllerClass = AVeloxMenuPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
	bUseSeamlessTravel = false;
}

void AVeloxMenuGameMode::RestartPlayer(AController* NewPlayer)
{
	// No pawn on the front-end (and no PlayerStart is required in the map).
}
