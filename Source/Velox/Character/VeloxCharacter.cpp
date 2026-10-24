// Velox - player/bot character.

#include "Character/VeloxCharacter.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxHealthComponent.h"
#include "Combat/VeloxHitboxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/VeloxCollisionChannels.h"
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
#include "Movement/VeloxScriptedInputComponent.h"

#include "DisplayDebugHelpers.h"
#include "Engine/Canvas.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxCharacter)

AVeloxCharacter::AVeloxCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVeloxMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Pawn geometry as class defaults (ADR 0003 section 6, crouch decision (c) of ADR 0002 A11): the engine's crouch code
	// restores and measures against the class default object, so this is the one source of geometry. A Blueprint
	// subclass may override these and PawnData->PawnClass points at it. Q3 at UnitScale 2.5: box 30 x 30 x 56 u
	// standing, crouch box -24..16 u (100 cm), viewheight 26 u standing (55 cm above the centre) and 12 u crouched (16 u
	// = 40 cm above the crouched centre).
	GetCapsuleComponent()->InitCapsuleSize(VeloxCharacterGeometry::CapsuleRadius, VeloxCharacterGeometry::CapsuleHalfHeight);
	BaseEyeHeight = VeloxCharacterGeometry::EyeHeight;
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	// SetCrouchedHalfHeight would derive CrouchedEyeHeight (0.8 x half height) through the owner; set it explicitly after.
	GetCharacterMovement()->SetCrouchedHalfHeight(VeloxCharacterGeometry::CrouchedHalfHeight);
	CrouchedEyeHeight = VeloxCharacterGeometry::CrouchedEyeHeight;

	// Hitscan never hits collision volumes of pawns: hits on pawns are resolved against UVeloxHitboxComponent poses by
	// lag compensation (ADR 0003 section 6). World geometry blocks the Weapon channel.
	GetCapsuleComponent()->SetCollisionResponseToChannel(Velox_TraceChannel_Weapon, ECR_Ignore);
	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->SetCollisionResponseToChannel(Velox_TraceChannel_Weapon, ECR_Ignore);
	}

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

#if !UE_BUILD_SHIPPING
	// Scripted input and the test knockback RPC (task 05). A default subobject so the RPC has a stable name on both ends.
	ScriptedInput = CreateDefaultSubobject<UVeloxScriptedInputComponent>(TEXT("ScriptedInput"));
#endif

	HealthComponent = CreateDefaultSubobject<UVeloxHealthComponent>(TEXT("HealthComponent"));
	HitboxComponent = CreateDefaultSubobject<UVeloxHitboxComponent>(TEXT("HitboxComponent"));

	// DefaultInputComponentClass stays EnhancedInputComponent (ADR 0001); Velox pawns use the tag-binding subclass.
	OverrideInputComponentClass = UVeloxInputComponent::StaticClass();
}

void AVeloxCharacter::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

#if !UE_BUILD_SHIPPING
	if (const UVeloxMovementComponent* VeloxMovement = GetVeloxMovementComponent(); VeloxMovement && DebugDisplay.IsDisplayOn(TEXT("VeloxMovement")))
	{
		VeloxMovement->DisplayVeloxDebug(Canvas, YL, YPos);
	}
#endif
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
	// Runs identically on server and clients, so movement settings match on both ends of the prediction.
	if (!PawnData && !DefaultPawnData.IsNull())
	{
		PawnData = DefaultPawnData.LoadSynchronous();
		if (!PawnData)
		{
			UE_LOG(LogVelox, Warning, TEXT("%s: failed to load pawn data [%s]."), *GetName(), *DefaultPawnData.ToString());
		}
	}

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

void AVeloxCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	LogCrouchState(TEXT("start"));
}

void AVeloxCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);
	LogCrouchState(TEXT("end"));
}

void AVeloxCharacter::LogCrouchState(const TCHAR* Event) const
{
	const UCapsuleComponent* Capsule = GetCapsuleComponent();
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	UE_LOG(LogVelox, Log, TEXT("VeloxCrouch: %s pawn=%s role=%s half_height=%.2f eye=%.2f z=%.2f onGround=%d"), Event, *GetName(),
		*UEnum::GetValueAsString(GetLocalRole()), Capsule ? Capsule->GetUnscaledCapsuleHalfHeight() : 0.f, BaseEyeHeight,
		GetActorLocation().Z, Movement ? Movement->IsMovingOnGround() : 0);
}

void AVeloxCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	InitAbilityActorInfo();

	if (AVeloxPlayerState* VeloxPS = GetPlayerState<AVeloxPlayerState>())
	{
		VeloxPS->OnPawnPossessed(PawnData);
	}
}

void AVeloxCharacter::UnPossessed()
{
	// Before Super, which clears the PlayerState pointer.
	AVeloxPlayerState* VeloxPS = GetPlayerState<AVeloxPlayerState>();
	UVeloxAbilitySystemComponent* ASC = VeloxPS ? VeloxPS->GetVeloxAbilitySystemComponent() : nullptr;
	if (ASC && ASC->GetAvatarActor() == this)
	{
		ASC->InitAbilityActorInfo(VeloxPS, nullptr);
	}
	if (HealthComponent)
	{
		HealthComponent->UninitializeFromAbilitySystem();
	}

	Super::UnPossessed();
}

void AVeloxCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	InitAbilityActorInfo();
}

void AVeloxCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();

	// The ASC caches the player controller in its actor info.
	if (UVeloxAbilitySystemComponent* ASC = GetVeloxAbilitySystemComponent(); ASC && ASC->GetAvatarActor() == this)
	{
		ASC->RefreshAbilityActorInfo();
	}
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
		if (HealthComponent)
		{
			HealthComponent->InitializeWithAbilitySystem(ASC);
		}
	}
}

void AVeloxCharacter::HandleGameplayCue(UObject* Self, FGameplayTag GameplayCueTag, EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters)
{
	UE_LOG(LogVeloxCombat, Verbose, TEXT("%s: gameplay cue %s event %d at %s"), *GetName(), *GameplayCueTag.ToString(), static_cast<int32>(EventType), *Parameters.Location.ToCompactString());
	IGameplayCueInterface::HandleGameplayCue(Self, GameplayCueTag, EventType, Parameters);
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
