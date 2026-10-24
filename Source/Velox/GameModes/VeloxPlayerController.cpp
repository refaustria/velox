// Velox - player controller.

#include "GameModes/VeloxPlayerController.h"

#include "Core/VeloxLog.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxPlayerController)

AVeloxPlayerController::AVeloxPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AVeloxPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Called once a local player is attached, so GetLocalPlayer() is valid here for local controllers.
	if (!IsLocalPlayerController() || DefaultMappingContext.IsNull())
	{
		return;
	}

	const UInputMappingContext* MappingContext = DefaultMappingContext.LoadSynchronous();
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!MappingContext || !InputSubsystem)
	{
		UE_LOG(LogVelox, Warning, TEXT("%s: could not add mapping context [%s]."), *GetName(), *DefaultMappingContext.ToString());
		return;
	}

	InputSubsystem->AddMappingContext(MappingContext, DefaultMappingContextPriority);
}
