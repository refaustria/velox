// Velox - player/bot character.

#include "Character/VeloxCharacter.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Camera/CameraTypes.h"
#include "UI/VeloxGameUserSettings.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Combat/VeloxHealthSet.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/VeloxGameMode.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
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
#include "Weapons/VeloxCombatFeedbackComponent.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

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
	CombatFeedbackComponent = CreateDefaultSubobject<UVeloxCombatFeedbackComponent>(TEXT("CombatFeedbackComponent"));

	// ADR 0003 section 10: a bright, capsule-sized placeholder body, coloured per player. Attached to the skeletal mesh so it
	// receives the network smoothing offset; never collides, never casts a shadow, hidden from its own player's camera.
	PlaceholderBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaceholderBody"));
	PlaceholderBody->SetupAttachment(GetMesh());
	PlaceholderBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlaceholderBody->SetCollisionResponseToChannel(Velox_TraceChannel_Weapon, ECR_Ignore);
	PlaceholderBody->SetGenerateOverlapEvents(false);
	PlaceholderBody->SetCastShadow(false);
	PlaceholderBody->SetOwnerNoSee(true);
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		if (CylinderMesh.Succeeded())
		{
			PlaceholderBody->SetStaticMesh(CylinderMesh.Object);
		}
		// The engine cylinder is 100 cm across and 100 cm high.
		PlaceholderBody->SetRelativeScale3D(FVector(VeloxCharacterGeometry::CapsuleRadius * 2.0 / 100.0, VeloxCharacterGeometry::CapsuleRadius * 2.0 / 100.0, VeloxCharacterGeometry::CapsuleHalfHeight * 2.0 / 100.0));
	}
	PlayerBodyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_PlayerBody.M_PlayerBody")));

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

	if (PlaceholderBody && GetMesh())
	{
		// Centre of the capsule, whatever the mesh offset (the skeletal mesh sits below it).
		PlaceholderBody->SetRelativeLocation(FVector(0.0, 0.0, -GetMesh()->GetRelativeLocation().Z));
		if (!IsNetMode(NM_DedicatedServer) && !PlayerBodyMaterial.IsNull())
		{
			if (UMaterialInterface* Base = PlayerBodyMaterial.LoadSynchronous())
			{
				BodyMaterial = UMaterialInstanceDynamic::Create(Base, this);
				PlaceholderBody->SetMaterial(0, BodyMaterial);
				ApplyPlayerColor();
			}
			else
			{
				UE_LOG(LogVelox, Warning, TEXT("%s: failed to load player body material [%s]; the body stays white."), *GetName(), *PlayerBodyMaterial.ToString());
			}
		}
	}

	if (HasAuthority() && HealthComponent)
	{
		HealthComponent->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);
	}

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
	ApplyPlayerColor();
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
	ApplyPlayerColor();

	// Clients: the pawn lost its PlayerState (it died and was unpossessed). A corpse is never an avatar and its health
	// component must not show the next life's values (ADR 0003 B8). Only clear what is still ours: the new pawn's
	// OnRep_PlayerState may have arrived first.
	if (!GetPlayerState())
	{
		if (HealthComponent)
		{
			HealthComponent->UninitializeFromAbilitySystem();
		}
		if (UVeloxAbilitySystemComponent* ASC = LastAbilitySystem.Get(); ASC && ASC->GetAvatarActor() == this)
		{
			ASC->InitAbilityActorInfo(ASC->GetOwnerActor(), nullptr);
		}
	}
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

	CachedCombatSettings = VeloxPS->GetCombatSettings();
	if (UVeloxAbilitySystemComponent* ASC = VeloxPS->GetVeloxAbilitySystemComponent())
	{
		// ADR 0003 B25: a pawn is one life. On a client the shot counter restarts at this pawn's first init with its
		// PlayerState's ASC (the server restarts it in ResetToLoadout). Keyed on this pawn's own LastAbilitySystem, not on
		// the ASC's avatar: the ASC replicates its avatar itself and may already point here.
		if (!HasAuthority() && LastAbilitySystem.Get() != ASC)
		{
			if (UVeloxWeaponInventoryComponent* Inventory = VeloxPS->GetWeaponInventory())
			{
				Inventory->ResetShotSeq();
				// Every client resets every player's counter (only the owning client's is used): Log for the own pawn only.
				if (GetLocalRole() == ROLE_AutonomousProxy)
				{
					UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotSeqReset pawn=%s"), *GetName());
				}
			}
		}
		LastAbilitySystem = ASC;
		ASC->InitAbilityActorInfo(VeloxPS, this);
		if (HealthComponent)
		{
			HealthComponent->InitializeWithAbilitySystem(ASC);
		}
	}
}

void AVeloxCharacter::HandleGameplayCue(UObject* Self, FGameplayTag GameplayCueTag, EGameplayCueEvent::Type EventType, const FGameplayCueParameters& Parameters)
{
	UE_LOG(LogVeloxCombat, VeryVerbose, TEXT("%s: gameplay cue %s event %d at %s"), *GetName(), *GameplayCueTag.ToString(), static_cast<int32>(EventType), *Parameters.Location.ToCompactString());
	if (CombatFeedbackComponent)
	{
		CombatFeedbackComponent->HandleCue(GameplayCueTag, EventType, Parameters);
	}
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
	// Raw mouse delta, no smoothing or acceleration, scaled the Q3 way: degrees = counts x 0.022 x sensitivity (ADR 0004 section 7).
	const UVeloxGameUserSettings& Settings = UVeloxGameUserSettings::GetOrDefault();
	const FVector2D Degrees = VeloxSettingsMath::MouseDeltaToDegrees(InputActionValue.Get<FVector2D>(), Settings.MouseSensitivity, Settings.bInvertMouseY);

	AddControllerYawInput(Degrees.X);
	AddControllerPitchInput(Degrees.Y);
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

// ---------------------------------------------------------------- Readability: player colour

void AVeloxCharacter::ApplyPlayerColor()
{
	if (!BodyMaterial)
	{
		return;
	}
	if (const AVeloxPlayerState* VeloxPS = GetPlayerState<AVeloxPlayerState>(); VeloxPS && VeloxPS->HasPlayerColor())
	{
		LastBodyColor = VeloxPS->GetPlayerColor();
	}
	FLinearColor Color = FLinearColor::FromSRGBColor(LastBodyColor);
	if (bIsCorpse)
	{
		Color *= UVeloxCombatSettings::GetOrDefault(CachedCombatSettings).CorpseColorScale;
		Color.A = 1.f;
	}
	BodyMaterial->SetVectorParameterValue(BodyColorParameter, Color);
}

FLinearColor AVeloxCharacter::GetBodyColor() const
{
	FLinearColor Color(0.f, 0.f, 0.f, 0.f);
	if (BodyMaterial)
	{
		BodyMaterial->GetVectorParameterValue(BodyColorParameter, Color);
	}
	return Color;
}

// ---------------------------------------------------------------- Death (ADR 0003 section 8, B8, B17)

void AVeloxCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVeloxCharacter, bIsCorpse);
}

void AVeloxCharacter::HandleOutOfHealth(const FVeloxDamageEvent& Event)
{
	HandleDeath(Event);
}

void AVeloxCharacter::HandleDeath(const FVeloxDamageEvent& Event)
{
	UWorld* World = GetWorld();
	if (!HasAuthority() || bIsCorpse || !World)
	{
		return;
	}
	AVeloxPlayerState* VictimPS = GetPlayerState<AVeloxPlayerState>();
	UVeloxAbilitySystemComponent* ASC = GetVeloxAbilitySystemComponent();
	AController* DeadController = GetController();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(CachedCombatSettings);
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDeath begin pawn=%s victim=%s instigator=%s tags=[%s]"), *GetName(), VictimPS ? *AVeloxPlayerState::VeloxPlayerLabel(VictimPS) : TEXT("-"),
		*GetNameSafe(Event.Instigator.Get()), *Event.DamageTags.ToStringSimple());

	// B8 1: GE_Dead first, so a second damage GE of this frame sees Velox.State.Dead and yields nothing.
	if (ASC)
	{
		ASC->ApplyGameplayEffectToSelf(GetDefault<UVeloxGE_Dead>(), 1.f, ASC->MakeEffectContext());
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDeath step=1 death tag applied dead=%d"), ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead) ? 1 : 0);
		// B8 2-4: abilities, queued input and WhileActive cues (the LG beam) end before the avatar is cleared.
		ASC->CancelAbilities();
		ASC->ClearAbilityInput();
		ASC->RemoveAllGameplayCues();
		// Task 04 review: the inventory's keep-alive timer and beam tag must not outlive the avatar either.
		if (VictimPS && VictimPS->GetWeaponInventory())
		{
			VictimPS->GetWeaponInventory()->StopBeam();
		}
		// B25/B27: pending move-timed events (own self splashes) die with the pawn; projectiles keep a Dead-tag guard.
		const int32 CancelledEvents = GetVeloxMovementComponent() ? GetVeloxMovementComponent()->CancelAllMoveTimedEvents() : 0;
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDeath step=2 abilities cancelled, input cleared, cues removed, move_timed_events_cancelled=%d"), CancelledEvents);
	}

	// B8 5: movement, collision, hitbox, corpse. The hitbox leaves the lag-compensation subsystem now (B17: corpses are
	// transparent to hitscan); a respawned pawn registers its own in BeginPlay.
	bIsCorpse = true;
	if (HealthComponent)
	{
		HealthComponent->StartDeath();
	}
	if (UVeloxLagCompensationSubsystem* LagComp = UVeloxLagCompensationSubsystem::Get(this))
	{
		LagComp->UnregisterHitbox(HitboxComponent);
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDeath step=3 hitbox unregistered=%d"), LagComp->IsRegistered(this) ? 0 : 1);
	}
	DyingController = DeadController;

	// Death cue (colour flash; no ragdoll, ADR 0003 section 8).
	if (ASC)
	{
		FGameplayCueParameters CueParams;
		CueParams.Location = GetActorLocation();
		CueParams.Instigator = Event.Instigator.Get();
		CueParams.EffectCauser = this;
		ASC->ExecuteGameplayCue(VeloxGameplayTags::GameplayCue_Character_Death, CueParams);
	}

	AVeloxPlayerState* KillerPS = Cast<AVeloxPlayerState>(Event.Instigator.Get());
	if (!KillerPS)
	{
		if (const APawn* KillerPawn = Cast<APawn>(Event.Instigator.Get()))
		{
			KillerPS = KillerPawn->GetPlayerState<AVeloxPlayerState>();
		}
	}
	AVeloxGameMode* GameMode = World->GetAuthGameMode<AVeloxGameMode>();
	if (GameMode)
	{
		GameMode->PlayerDied(VictimPS, KillerPS, Event.DamageTags);
	}

	World->GetTimerManager().SetTimer(CorpseTimer, FTimerDelegate::CreateUObject(this, &ThisClass::FinishCorpse), FMath::Max(Settings.CorpseLifetime, 0.01f), false);

	// The movement component, collision and the possession are torn down on the next tick: a lethal self-splash (rocket
	// jump) applies its damage inside the owner's PerformMovement, which must not lose its controller or movement mode
	// underneath itself. Everything gameplay-relevant above is already synchronous and Dead gates the rest.
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &ThisClass::CompleteDeath));
	ForceNetUpdate();
}

void AVeloxCharacter::CompleteDeath()
{
	UWorld* World = GetWorld();
	if (!World || !bIsCorpse)
	{
		return;
	}
	UVeloxAbilitySystemComponent* ASC = LastAbilitySystem.Get();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(CachedCombatSettings);
	if (UVeloxMovementComponent* Movement = GetVeloxMovementComponent(); Movement && !Movement->IsMovingOnGround() && Settings.CorpseDropMaxCm > 0.f)
	{
		// Died in the air: lie on the floor instead of hanging (collision is still on for this sweep).
		// Pawns are ignored for this sweep: a corpse dying above another player must not come to rest on its head.
		UCapsuleComponent* Capsule = GetCapsuleComponent();
		const ECollisionResponse PawnResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		AddActorWorldOffset(FVector(0.0, 0.0, -Settings.CorpseDropMaxCm), true);
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, PawnResponse);
	}
	ApplyCorpseState();
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDeath step=4 corpse collision=%d movement_mode=%d lifetime=%.2f s"), GetCapsuleComponent()->GetCollisionEnabled() != ECollisionEnabled::NoCollision ? 1 : 0,
		GetCharacterMovement() ? static_cast<int32>(GetCharacterMovement()->MovementMode.GetValue()) : -1, Settings.CorpseLifetime);

	// Last: the controller lets go (UnPossessed clears the ASC avatar), then the death camera shows the corpse.
	AController* DeadController = DyingController.Get();
	if (DeadController && DeadController->GetPawn() == this)
	{
		DeadController->UnPossess();
		if (APlayerController* PC = Cast<APlayerController>(DeadController))
		{
			PC->SetViewTarget(this);
		}
		if (AVeloxGameMode* GameMode = World->GetAuthGameMode<AVeloxGameMode>())
		{
			GameMode->NotifyControllerDied(DeadController);
		}
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDeath step=5 unpossessed controller=%s avatar_cleared=%d"), *GetNameSafe(DeadController),
		(ASC && ASC->GetAvatarActor() != this) ? 1 : 0);
	ForceNetUpdate();
}

void AVeloxCharacter::ApplyCorpseState()
{
	// B25/B27: no own self splash may run on a corpse (the owning client predicts its own; OnRep_IsCorpse lands here).
	if (UVeloxMovementComponent* CorpseMovement = GetVeloxMovementComponent())
	{
		CorpseMovement->CancelAllMoveTimedEvents();
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (USkeletalMeshComponent* MeshComponent = GetMesh())
	{
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	// Every role: a client corpse must not keep sliding until the server's MOVE_None arrives (the server is already there).
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	if (!HasAuthority() && HealthComponent)
	{
		HealthComponent->UninitializeFromAbilitySystem();
	}
	// The owner (controller) is gone or going: the body becomes visible to the dying player's death camera.
	if (PlaceholderBody)
	{
		PlaceholderBody->SetOwnerNoSee(false);
	}
	ApplyPlayerColor();
}

void AVeloxCharacter::OnRep_IsCorpse()
{
	if (bIsCorpse)
	{
		ApplyCorpseState();
	}
}

void AVeloxCharacter::FinishCorpse()
{
	if (HealthComponent)
	{
		HealthComponent->FinishDeath();
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDeath corpse removed pawn=%s"), *GetName());
	Destroy();
}

void AVeloxCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	if (!HasAuthority() || bIsCorpse)
	{
		return; // a corpse that fell is removed by its timer
	}
	UVeloxAbilitySystemComponent* ASC = GetVeloxAbilitySystemComponent();
	if (!ASC || ASC->GetAvatarActor() != this)
	{
		Super::FellOutOfWorld(DmgType);
		return;
	}
	FVeloxDamageSpecParams Params;
	Params.Damage = UVeloxCombatSettings::GetOrDefault(CachedCombatSettings).EnvironmentKillDamage;
	Params.DamageTag = VeloxGameplayTags::Damage_Environment;
	Params.HitLocation = GetActorLocation();
	Params.bNoArmor = true;
	Params.bNoKnockback = true;
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxDeath %s fell out of the world at %s"), *GetName(), *GetActorLocation().ToCompactString());
	// World damage is built on the victim's own ASC with bSelf = false (no half damage, not a self hit).
	const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(ASC, Params);
	if (Spec.IsValid())
	{
		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}

void AVeloxCharacter::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (!bIsCorpse)
	{
		Super::CalcCamera(DeltaTime, OutResult);
		ApplyViewSettings(OutResult);
		return;
	}
	// Death camera (Q3 shows the body): in front of the corpse at DeathCamDistanceCm, looking down at its eyes.
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(CachedCombatSettings);
	const FVector Pivot = GetActorLocation() + FVector(0.0, 0.0, BaseEyeHeight);
	const FRotator Looking(-Settings.DeathCamPitchDegrees, GetActorRotation().Yaw + 180.0, 0.0);
	FVector CameraLocation = Pivot - Looking.Vector() * Settings.DeathCamDistanceCm;
	if (const UWorld* World = GetWorld())
	{
		FHitResult Hit;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(VeloxDeathCam), false, this);
		if (World->LineTraceSingleByChannel(Hit, Pivot, CameraLocation, ECC_Camera, Query))
		{
			CameraLocation = Hit.Location + Hit.ImpactNormal * Settings.DeathCamWallOffsetCm;
		}
	}
	OutResult.Location = CameraLocation;
	OutResult.Rotation = (Pivot - CameraLocation).Rotation();
	ApplyViewSettings(OutResult);
}

void AVeloxCharacter::ApplyViewSettings(FMinimalViewInfo& OutResult) const
{
	// The viewer's own setting, also while spectating another pawn: horizontal FOV, whatever the aspect ratio.
	OutResult.FOV = VeloxSettingsMath::ClampFieldOfView(UVeloxGameUserSettings::GetOrDefault().FieldOfView);
	OutResult.AspectRatioAxisConstraint = AspectRatio_MaintainXFOV;
}
