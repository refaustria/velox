// Velox - player/bot character.

#include "Character/VeloxCharacter.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Core/VeloxGameplayTags.h"
#include "Core/VeloxLog.h"
#include "Core/VeloxPawnData.h"
#include "GameModes/VeloxPlayerState.h"
#include "Input/VeloxInputComponent.h"
#include "Input/VeloxInputConfig.h"
#include "InputActionValue.h"
#include "Movement/VeloxMovementComponent.h"
#include "Movement/VeloxMovementSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxCharacter)

AVeloxCharacter::AVeloxCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVeloxMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
}

UAbilitySystemComponent* AVeloxCharacter::GetAbilitySystemComponent() const
{
	return GetVeloxAbilitySystemComponent();
}

UVeloxAbilitySystemComponent* AVeloxCharacter::GetVeloxAbilitySystemComponent() const
{
	const AVeloxPlayerState* VeloxPS = GetPlayerState<AVeloxPlayerState>();
	return VeloxPS ? VeloxPS->GetVeloxAbilitySystemComponent() : nullptr;
}

UVeloxMovementComponent* AVeloxCharacter::GetVeloxMovementComponent() const
{
	return Cast<UVeloxMovementComponent>(GetCharacterMovement());
}

const UVeloxPawnData* AVeloxCharacter::GetPawnData() const
{
	return PawnData;
}

void AVeloxCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Runs identically on server and clients, so the movement settings match on both ends of the prediction.
	if (!PawnData && !DefaultPawnData.IsNull())
	{
		PawnData = DefaultPawnData.LoadSynchronous();
		if (!PawnData)
		{
			UE_LOG(LogVelox, Warning, TEXT("%s: failed to load pawn data [%s]."), *GetName(), *DefaultPawnData.ToString());
		}
	}

	UVeloxMovementComponent* VeloxMovement = GetVeloxMovementComponent();
	if (PawnData && PawnData->MovementSettings && VeloxMovement && !VeloxMovement->GetMovementSettings())
	{
		VeloxMovement->SetMovementSettings(PawnData->MovementSettings);
	}
}

void AVeloxCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitAbilityActorInfo();
}

void AVeloxCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	InitAbilityActorInfo();
}

void AVeloxCharacter::InitAbilityActorInfo()
{
	AVeloxPlayerState* VeloxPS = GetPlayerState<AVeloxPlayerState>();
	if (!VeloxPS)
	{
		return;
	}

	if (UVeloxAbilitySystemComponent* ASC = VeloxPS->GetVeloxAbilitySystemComponent())
	{
		ASC->InitAbilityActorInfo(VeloxPS, this);
	}
	// TODO(M2): clear the avatar on death/unpossess and apply GE_SpawnInit on respawn.
}

UInputComponent* AVeloxCharacter::CreatePlayerInputComponent()
{
	// DefaultInputComponentClass stays EnhancedInputComponent (ADR 0001); Velox pawns use the tag-binding subclass.
	static const FName InputComponentName(TEXT("PawnInputComponent0"));
	return NewObject<UVeloxInputComponent>(this, UVeloxInputComponent::StaticClass(), InputComponentName);
}

void AVeloxCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UVeloxInputComponent* VeloxInput = Cast<UVeloxInputComponent>(PlayerInputComponent);
	const UVeloxInputConfig* InputConfig = PawnData ? PawnData->InputConfig.Get() : nullptr;
	if (!VeloxInput || !InputConfig)
	{
		UE_LOG(LogVelox, Warning, TEXT("%s: no Velox input component or input config; input is not bound."), *GetName());
		return;
	}

	using namespace VeloxGameplayTags;
	VeloxInput->BindNativeAction(InputConfig, InputTag_Move, ETriggerEvent::Triggered, this, &ThisClass::Input_Move, true);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Look_Mouse, ETriggerEvent::Triggered, this, &ThisClass::Input_LookMouse, true);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Jump, ETriggerEvent::Started, this, &ThisClass::Input_JumpStarted, true);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Jump, ETriggerEvent::Completed, this, &ThisClass::Input_JumpCompleted, true);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Crouch, ETriggerEvent::Started, this, &ThisClass::Input_CrouchStarted, false);
	VeloxInput->BindNativeAction(InputConfig, InputTag_Crouch, ETriggerEvent::Completed, this, &ThisClass::Input_CrouchCompleted, false);

	VeloxInput->RemoveBinds(AbilityInputBindHandles);
	VeloxInput->BindAbilityActions(InputConfig, this, &ThisClass::Input_AbilityInputTagPressed, &ThisClass::Input_AbilityInputTagReleased, AbilityInputBindHandles);
}

void AVeloxCharacter::Input_Move(const FInputActionValue& InputActionValue)
{
	if (!Controller)
	{
		return;
	}

	const FVector2D Value = InputActionValue.Get<FVector2D>();
	const FRotator YawRotation(0.0, Controller->GetControlRotation().Yaw, 0.0);

	if (Value.X != 0.0)
	{
		AddMovementInput(YawRotation.RotateVector(FVector::RightVector), Value.X);
	}
	if (Value.Y != 0.0)
	{
		AddMovementInput(YawRotation.RotateVector(FVector::ForwardVector), Value.Y);
	}
}

void AVeloxCharacter::Input_LookMouse(const FInputActionValue& InputActionValue)
{
	// Raw mouse delta, no smoothing or acceleration. Sensitivity and invert-Y come from user settings in M3.
	const FVector2D Value = InputActionValue.Get<FVector2D>();

	AddControllerYawInput(Value.X);
	AddControllerPitchInput(Value.Y);
}

void AVeloxCharacter::Input_JumpStarted(const FInputActionValue& InputActionValue)
{
	if (UVeloxMovementComponent* VeloxMovement = GetVeloxMovementComponent())
	{
		VeloxMovement->SetJumpHeld(true);
	}
	Jump();
}

void AVeloxCharacter::Input_JumpCompleted(const FInputActionValue& InputActionValue)
{
	if (UVeloxMovementComponent* VeloxMovement = GetVeloxMovementComponent())
	{
		VeloxMovement->SetJumpHeld(false);
	}
	StopJumping();
}

void AVeloxCharacter::Input_CrouchStarted(const FInputActionValue& InputActionValue)
{
	Crouch();
}

void AVeloxCharacter::Input_CrouchCompleted(const FInputActionValue& InputActionValue)
{
	UnCrouch();
}

void AVeloxCharacter::Input_AbilityInputTagPressed(FGameplayTag InputTag)
{
	if (UVeloxAbilitySystemComponent* ASC = GetVeloxAbilitySystemComponent())
	{
		ASC->AbilityInputTagPressed(InputTag);
	}
}

void AVeloxCharacter::Input_AbilityInputTagReleased(FGameplayTag InputTag)
{
	if (UVeloxAbilitySystemComponent* ASC = GetVeloxAbilitySystemComponent())
	{
		ASC->AbilityInputTagReleased(InputTag);
	}
}
