// Velox - spectator pawn (ADR 0004 C11).

#include "GameModes/VeloxSpectatorPawn.h"

#include "Combat/VeloxCombatSettings.h"
#include "Core/VeloxGameplayTags.h"
#include "Core/VeloxLog.h"
#include "GameFramework/SpectatorPawnMovement.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "Input/VeloxInputComponent.h"
#include "Input/VeloxInputConfig.h"
#include "InputActionValue.h"
#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxSettingsMath.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxSpectatorPawn)

AVeloxSpectatorPawn::AVeloxSpectatorPawn(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The engine's legacy axis bindings (DefaultPawn_MoveForward ...) are replaced by the Enhanced Input ones below
	// (SetupPlayerInputComponent turns them back on only for a non-Velox controller).
	bAddDefaultMovementBindings = false;
	OverrideInputComponentClass = UVeloxInputComponent::StaticClass();
	PrimaryActorTick.bCanEverTick = true;
	// Speed/acceleration/deceleration come from the combat settings in PossessedBy (SpawnSpectatorPawn possesses at once).
}

void AVeloxSpectatorPawn::ApplyMovementSettings(const UVeloxCombatSettings& Settings)
{
	if (USpectatorPawnMovement* Movement = Cast<USpectatorPawnMovement>(GetMovementComponent()))
	{
		Movement->MaxSpeed = Settings.DeathSpectateSpeedCmS;
		Movement->Acceleration = Settings.DeathSpectateAccelerationCmS2;
		Movement->Deceleration = Settings.DeathSpectateDecelerationCmS2;
	}
}

void AVeloxSpectatorPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	const AVeloxPlayerState* VeloxPS = NewController ? NewController->GetPlayerState<AVeloxPlayerState>() : nullptr;
	ApplyMovementSettings(UVeloxCombatSettings::GetOrDefault(VeloxPS ? VeloxPS->GetCombatSettings() : nullptr));
}

void AVeloxSpectatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	UVeloxInputComponent* VeloxInput = Cast<UVeloxInputComponent>(PlayerInputComponent);
	const AVeloxPlayerController* PC = Cast<AVeloxPlayerController>(GetController());
	const UVeloxInputConfig* InputConfig = PC ? PC->GetInputConfig() : nullptr;
	// A controller that is no AVeloxPlayerController (?SpectatorOnly=1 logins get the engine's
	// ReplaySpectatorPlayerControllerClass, a plain APlayerController) keeps the engine's default spectator bindings.
	bAddDefaultMovementBindings = !VeloxInput || !InputConfig;
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (bAddDefaultMovementBindings)
	{
		UE_LOG(LogVelox, Log, TEXT("%s: engine default spectator bindings (controller %s, input config %s)."), *GetName(), *GetNameSafe(GetController()), *GetNameSafe(InputConfig));
		return;
	}
	using namespace VeloxGameplayTags;
	VeloxInput->BindNativeAction(InputConfig, InputTag_Move, ETriggerEvent::Triggered, this, &ThisClass::Input_Move, true);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Look_Mouse, ETriggerEvent::Triggered, this, &ThisClass::Input_LookMouse, true);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Jump, ETriggerEvent::Started, this, &ThisClass::Input_UpStarted, true);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Jump, ETriggerEvent::Completed, this, &ThisClass::Input_UpCompleted, true);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Crouch, ETriggerEvent::Started, this, &ThisClass::Input_DownStarted, false);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Crouch, ETriggerEvent::Completed, this, &ThisClass::Input_DownCompleted, false);
}

void AVeloxSpectatorPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bUpHeld != bDownHeld)
	{
		AddMovementInput(FVector::UpVector, bUpHeld ? 1.f : -1.f);
	}
}

void AVeloxSpectatorPawn::Input_Move(const FInputActionValue& Value)
{
	// IA_Move is X = right, Y = forward (as AVeloxCharacter::Input_Move), in the plane of the view yaw.
	const FVector2D Axis = Value.Get<FVector2D>();
	const FRotator YawRotation(0.0, GetControlRotation().Yaw, 0.0);
	if (Axis.Y != 0.0)
	{
		AddMovementInput(YawRotation.RotateVector(FVector::ForwardVector), static_cast<float>(Axis.Y));
	}
	if (Axis.X != 0.0)
	{
		AddMovementInput(YawRotation.RotateVector(FVector::RightVector), static_cast<float>(Axis.X));
	}
}

void AVeloxSpectatorPawn::Input_LookMouse(const FInputActionValue& Value)
{
	// The character's arithmetic (Q3: degrees = counts x 0.022 x sensitivity), shared through VeloxSettingsMath.
	const UVeloxGameUserSettings& Settings = UVeloxGameUserSettings::GetOrDefault();
	const FVector2D Degrees = VeloxSettingsMath::MouseDeltaToDegrees(Value.Get<FVector2D>(), Settings.MouseSensitivity, Settings.bInvertMouseY);
	AddControllerYawInput(Degrees.X);
	AddControllerPitchInput(Degrees.Y);
}

void AVeloxSpectatorPawn::Input_UpStarted(const FInputActionValue&)
{
	bUpHeld = true;
}

void AVeloxSpectatorPawn::Input_UpCompleted(const FInputActionValue&)
{
	bUpHeld = false;
}

void AVeloxSpectatorPawn::Input_DownStarted(const FInputActionValue&)
{
	bDownHeld = true;
}

void AVeloxSpectatorPawn::Input_DownCompleted(const FInputActionValue&)
{
	bDownHeld = false;
}
