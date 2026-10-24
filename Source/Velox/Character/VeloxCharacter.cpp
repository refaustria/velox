// Velox - player/bot character.

#include "Character/VeloxCharacter.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Core/VeloxGameplayTags.h"
#include "Core/VeloxLog.h"
#include "Core/VeloxPawnData.h"
#include "GameFramework/Controller.h"
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
	// Crouch disabled in M1: engine crouch restores CDO capsule geometry, ours comes from pawn data (ADR 0002 A11).
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = false;

	// FPS: the capsule takes the controller yaw; pitch and roll are view-only (ADR 0002 section 1).
	bUseControllerRotationYaw = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	// One jump per ground contact, no hold force: UVeloxMovementComponent::DoJump sets the jump velocity once and
	// bPressedJump clears after one move (auto-hop uses the separately replicated jump-held state).
	JumpMaxHoldTime = 0.f;
	JumpMaxCount = 1;

	// ADR 0001 section 3 / ADR 0002 section 5.
	SetNetUpdateFrequency(60.f);
	SetMinNetUpdateFrequency(30.f);

	// DefaultInputComponentClass stays EnhancedInputComponent (ADR 0001); Velox pawns use the tag-binding subclass.
	OverrideInputComponentClass = UVeloxInputComponent::StaticClass();
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
	// Runs identically on server and clients, so geometry and movement settings match on both ends of the prediction.
	if (!PawnData && !DefaultPawnData.IsNull())
	{
		PawnData = DefaultPawnData.LoadSynchronous();
		if (!PawnData)
		{
			UE_LOG(LogVelox, Warning, TEXT("%s: failed to load pawn data [%s]."), *GetName(), *DefaultPawnData.ToString());
		}
	}

	// Before Super: ACharacter::PostInitializeComponents updates the nav agent from the capsule.
	ApplyPawnGeometry();

	Super::PostInitializeComponents();

	// Settings assigned on the component (e.g. in a Blueprint subclass) win over pawn data; either way they are applied
	// through SetMovementSettings so the engine fields and cached sim params are set.
	if (UVeloxMovementComponent* VeloxMovement = GetVeloxMovementComponent())
	{
		const UVeloxMovementSettings* Settings = VeloxMovement->GetMovementSettings();
		if (!Settings && PawnData)
		{
			Settings = PawnData->MovementSettings;
		}
		if (Settings)
		{
			VeloxMovement->SetMovementSettings(Settings);
		}
		else
		{
			UE_LOG(LogVelox, Warning, TEXT("%s: no movement settings; using stock character movement."), *GetName());
		}
	}
}

void AVeloxCharacter::ApplyPawnGeometry()
{
	if (!PawnData)
	{
		return;
	}

	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCapsuleSize(PawnData->CapsuleRadius, PawnData->CapsuleHalfHeight);
	}
	BaseEyeHeight = PawnData->EyeHeight;
}

void AVeloxCharacter::RecalculateBaseEyeHeight()
{
	if (PawnData && !IsCrouched())
	{
		BaseEyeHeight = PawnData->EyeHeight;
		return;
	}
	Super::RecalculateBaseEyeHeight();
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
	VeloxInput->BindNativeAction(InputConfig, InputTag_Move, ETriggerEvent::Completed, this, &ThisClass::Input_MoveCompleted, true);
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

	// IA_Move is X = right, Y = forward; the movement component wants (forward, right) presence for the CPM rules.
	if (UVeloxMovementComponent* VeloxMovement = GetVeloxMovementComponent())
	{
		VeloxMovement->SetLocalMoveInput(FVector2D(Value.Y, Value.X));
	}

	if (Value.X != 0.0)
	{
		AddMovementInput(YawRotation.RotateVector(FVector::RightVector), Value.X);
	}
	if (Value.Y != 0.0)
	{
		AddMovementInput(YawRotation.RotateVector(FVector::ForwardVector), Value.Y);
	}
}

void AVeloxCharacter::Input_MoveCompleted(const FInputActionValue& InputActionValue)
{
	// Triggered stops firing on release; clear the input presence explicitly.
	if (UVeloxMovementComponent* VeloxMovement = GetVeloxMovementComponent())
	{
		VeloxMovement->SetLocalMoveInput(FVector2D::ZeroVector);
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
