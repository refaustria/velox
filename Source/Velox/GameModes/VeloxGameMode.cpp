// Velox - base game mode.

#include "GameModes/VeloxGameMode.h"

#include "Character/VeloxCharacter.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "UI/VeloxHUD.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameMode)

AVeloxGameMode::AVeloxGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	GameStateClass = AVeloxGameState::StaticClass();
	PlayerControllerClass = AVeloxPlayerController::StaticClass();
	PlayerStateClass = AVeloxPlayerState::StaticClass();
	DefaultPawnClass = AVeloxCharacter::StaticClass();
	HUDClass = AVeloxHUD::StaticClass();
}
