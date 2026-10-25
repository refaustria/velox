// Velox - player controller.

#include "GameModes/VeloxPlayerController.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Core/VeloxLog.h"
#include "GameModes/VeloxPlayerState.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Net/VeloxLagCompensationSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxPlayerController)

AVeloxPlayerController::AVeloxPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FVeloxShotTiming AVeloxPlayerController::BuildShotTiming(const FVector& Origin, const FVector& Dir, float RangeCm) const
{
	return VeloxLagComp::BuildShotTiming(*this, Origin, Dir, RangeCm);
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

void AVeloxPlayerController::PostProcessInput(const float DeltaTime, const bool bGamePaused)
{
	if (const AVeloxPlayerState* VeloxPS = GetPlayerState<AVeloxPlayerState>())
	{
		if (UVeloxAbilitySystemComponent* ASC = VeloxPS->GetVeloxAbilitySystemComponent())
		{
			ASC->ProcessAbilityInput(DeltaTime, bGamePaused);
		}
	}

	Super::PostProcessInput(DeltaTime, bGamePaused);
}
