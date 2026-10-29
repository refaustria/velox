// Velox - player controller.

#include "GameModes/VeloxPlayerController.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "EnhancedInputComponent.h"
#include "Engine/World.h"
#include "GameModes/VeloxGameMode.h"
#include "InputAction.h"
#include "Weapons/VeloxCombatFeedbackComponent.h"
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

void AVeloxPlayerController::ClientConfirmHit_Implementation(uint8 ShotSeq, uint8 TotalDamage, uint8 Flags)
{
	// The feedback component keeps the time and count; the kill flag and damage are kept here for the HUD marker.
	LastHitConfirmTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	LastHitConfirmDamage = TotalDamage;
	bLastHitConfirmKill = (Flags & VeloxHitConfirmFlags::Kill) != 0;
	if (const APawn* MyPawn = GetPawn())
	{
		if (UVeloxCombatFeedbackComponent* Feedback = MyPawn->FindComponentByClass<UVeloxCombatFeedbackComponent>())
		{
			Feedback->OnHitConfirmed(ShotSeq, TotalDamage, Flags);
		}
	}
}

void AVeloxPlayerController::SetPawn(APawn* InPawn)
{
	// Server and owning client (OnRep_Pawn): the moment the controller loses its pawn starts the respawn wait.
	// (On the client OnRep_Pawn has already stored the new pawn when it calls this, so GetPawn() cannot tell.)
	if (!InPawn && LastPawn.IsValid() && GetWorld())
	{
		PawnLostTime = GetWorld()->GetTimeSeconds();
	}
	else if (InPawn)
	{
		PawnLostTime = -1.0;
	}
	LastPawn = InPawn;
	Super::SetPawn(InPawn);
}

void AVeloxPlayerController::ServerRequestRespawn_Implementation()
{
	if (AVeloxGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AVeloxGameMode>() : nullptr)
	{
		GameMode->TryRespawn(this, false);
	}
}

void AVeloxPlayerController::OnRespawnInput()
{
	// Fire/jump are also game input; only a controller without a pawn treats them as "respawn".
	UWorld* World = GetWorld();
	if (!World || GetPawn())
	{
		return;
	}
	const AVeloxPlayerState* VeloxPS = GetPlayerState<AVeloxPlayerState>();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(VeloxPS ? VeloxPS->GetCombatSettings() : nullptr);
	const double Now = World->GetRealTimeSeconds();
	if (Now - LastRespawnRequestTime < Settings.RespawnRequestIntervalSeconds)
	{
		return;
	}
	LastRespawnRequestTime = Now;
	ServerRequestRespawn();
}

void AVeloxPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Called once a local player is attached, so GetLocalPlayer() is valid here for local controllers.
	if (!IsLocalPlayerController())
	{
		return;
	}

	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		for (const TSoftObjectPtr<UInputAction>& Action : RespawnInputActions)
		{
			if (const UInputAction* LoadedAction = Action.LoadSynchronous())
			{
				EnhancedInput->BindAction(LoadedAction, ETriggerEvent::Triggered, this, &ThisClass::OnRespawnInput);
			}
		}
	}

	if (DefaultMappingContext.IsNull())
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

void AVeloxPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// ADR 0003 B6: after UpdateRotation; DeltaTime == 0 is the engine's paused convention (PlayerController::PlayerTick).
	if (const AVeloxPlayerState* VeloxPS = GetPlayerState<AVeloxPlayerState>())
	{
		if (UVeloxAbilitySystemComponent* ASC = VeloxPS->GetVeloxAbilitySystemComponent())
		{
			ASC->ProcessAbilityInput(DeltaTime, DeltaTime == 0.f);
		}
	}
}
