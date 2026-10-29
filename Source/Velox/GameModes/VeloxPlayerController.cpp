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
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameModes/VeloxMatchRules.h"
#include "InputMappingContext.h"
#include "Core/VeloxGameplayTags.h"
#include "Input/VeloxInputConfig.h"
#include "Net/VeloxLagCompensationSubsystem.h"
#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxPauseMenuWidget.h"
#include "UI/VeloxUISettings.h"
#include "UserSettings/EnhancedInputUserSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxPlayerController)

AVeloxPlayerController::AVeloxPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PauseMenuWidgetClass = UVeloxPauseMenuWidget::StaticClass();
}

FVeloxShotTiming AVeloxPlayerController::BuildShotTiming(const FVector& Origin, const FVector& Dir, float RangeCm) const
{
	return VeloxLagComp::BuildShotTiming(*this, Origin, Dir, RangeCm, ShotDelayMemory);
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

void AVeloxPlayerController::ServerSetReady_Implementation(bool bInReady)
{
	if (AVeloxGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AVeloxGameMode>() : nullptr)
	{
		GameMode->SetPlayerReady(this, bInReady);
	}
}

void AVeloxPlayerController::ServerRequestJoin_Implementation()
{
	if (AVeloxGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AVeloxGameMode>() : nullptr)
	{
		GameMode->RequestJoin(this);
	}
}

void AVeloxPlayerController::Ready()
{
	const AVeloxPlayerState* VeloxPS = GetPlayerState<AVeloxPlayerState>();
	ServerSetReady(!(VeloxPS && VeloxPS->IsReady()));
}

void AVeloxPlayerController::Join()
{
	ServerRequestJoin();
}

void AVeloxPlayerController::EnterIntermission(AActor* ViewTarget)
{
	bInIntermission = true;
	SetIgnoreMoveInput(true);
	if (ViewTarget)
	{
		SetViewTarget(ViewTarget);
	}
}

void AVeloxPlayerController::ClientEnterIntermission_Implementation(AActor* ViewTarget)
{
	EnterIntermission(ViewTarget);
	// The server has disabled the pawn's movement; doing the same locally keeps the predicted client from running into a
	// correction while it still holds a move key.
	if (const ACharacter* MyCharacter = Cast<ACharacter>(GetPawn()))
	{
		if (UCharacterMovementComponent* Movement = MyCharacter->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
		}
	}
	ApplyUIInputMode();
	UE_LOG(LogVeloxMatch, Log, TEXT("VeloxMatch intermission player=%s view=%s"), *AVeloxPlayerState::VeloxPlayerLabel(PlayerState), *GetNameSafe(ViewTarget));
}

void AVeloxPlayerController::Reset()
{
	bInIntermission = false;
	ResetIgnoreMoveInput();
	Super::Reset();
}

void AVeloxPlayerController::ClientReset_Implementation()
{
	bInIntermission = false;
	ResetIgnoreMoveInput();
	Super::ClientReset_Implementation();
	ApplyUIInputMode();
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
		BindUIInput(*EnhancedInput);
		BindMenuInput(EnhancedInput);
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

	// bNotifyUserSettings registers the context with the Enhanced Input user settings, so saved rebinds replace the
	// default keys (ADR 0004 section 7); the settings screen rebinds through UVeloxGameUserSettings::RebindKey.
	FModifyContextOptions Options;
	Options.bNotifyUserSettings = true;
	InputSubsystem->AddMappingContext(MappingContext, DefaultMappingContextPriority, Options);
	// The subsystem creates the user settings in PlayerControllerChanged, which runs after this function on a fresh
	// controller: the notify above then finds none, so register when they come up.
	if (!InputSubsystem->GetUserSettings())
	{
		InputSubsystem->OnPostUserSettingsInitialized.AddUniqueDynamic(this, &ThisClass::OnInputUserSettingsInitialized);
	}
}

void AVeloxPlayerController::BindUIInput(UEnhancedInputComponent& EnhancedInput)
{
	const UVeloxInputConfig* Config = UIInputConfig.IsNull() ? nullptr : UIInputConfig.LoadSynchronous();
	if (!Config)
	{
		return;
	}
	using namespace VeloxGameplayTags;
	if (const UInputAction* Ready = Config->FindNativeInputActionForTag(InputTag_Ready, true))
	{
		EnhancedInput.BindAction(Ready, ETriggerEvent::Started, this, &ThisClass::OnReadyInput);
	}
	if (const UInputAction* Scoreboard = Config->FindNativeInputActionForTag(InputTag_Scoreboard, true))
	{
		EnhancedInput.BindAction(Scoreboard, ETriggerEvent::Started, this, &ThisClass::OnScoreboardPressed);
		EnhancedInput.BindAction(Scoreboard, ETriggerEvent::Completed, this, &ThisClass::OnScoreboardReleased);
		EnhancedInput.BindAction(Scoreboard, ETriggerEvent::Canceled, this, &ThisClass::OnScoreboardReleased);
	}
}

void AVeloxPlayerController::OnReadyInput()
{
	// Same toggle as the `Ready` console command; the server ignores it where ready means nothing (spectators, FFA live).
	Ready();
}

void AVeloxPlayerController::OnScoreboardPressed()
{
	bScoreboardHeld = true;
}

void AVeloxPlayerController::OnScoreboardReleased()
{
	bScoreboardHeld = false;
}

void AVeloxPlayerController::OnInputUserSettingsInitialized(const UEnhancedInputUserSettings* Settings)
{
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	UEnhancedInputUserSettings* UserSettings = InputSubsystem ? InputSubsystem->GetUserSettings() : nullptr;
	if (UserSettings && !DefaultMappingContext.IsNull())
	{
		UserSettings->RegisterInputMappingContext(DefaultMappingContext.LoadSynchronous());
	}
}

bool AVeloxPlayerController::ShouldAutoSwitchOnPickup() const
{
	return UVeloxGameUserSettings::GetOrDefault().bAutoSwitchOnPickup;
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

// ---- In-game menu (task M3-10) ----

void AVeloxPlayerController::BindMenuInput(UEnhancedInputComponent* EnhancedInput)
{
	// InputTag.Menu: IA_Menu (Escape) toggles the in-game menu. Bound on the controller so it works while dead or spectating.
	if (const UInputAction* MenuAction = MenuInputAction.LoadSynchronous())
	{
		EnhancedInput->BindAction(MenuAction, ETriggerEvent::Started, this, &ThisClass::OnMenuInput);
	}
}

bool AVeloxPlayerController::IsMenuOpen() const
{
	return PauseMenu && PauseMenu->IsInViewport();
}

void AVeloxPlayerController::OnMenuInput()
{
	ToggleMenu();
}

void AVeloxPlayerController::ToggleMenu()
{
	if (IsMenuOpen())
	{
		CloseMenu();
	}
	else
	{
		OpenMenu();
	}
}

void AVeloxPlayerController::OpenMenu()
{
	if (!IsLocalPlayerController() || !PauseMenuWidgetClass)
	{
		return;
	}
	if (!PauseMenu)
	{
		PauseMenu = CreateWidget<UVeloxPauseMenuWidget>(this, PauseMenuWidgetClass);
		if (!PauseMenu)
		{
			return;
		}
		PauseMenu->OnResume.AddUObject(this, &ThisClass::CloseMenu);
	}
	PauseMenu->AddToViewport(UVeloxUISettings::Get().PauseMenuZOrder);
	// UI-only input stops the game actions (and flushes held keys); Escape then reaches the widget, which resumes.
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(PauseMenu->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;
	UE_LOG(LogVelox, Log, TEXT("VeloxMenu pause menu opened"));
}

void AVeloxPlayerController::CloseMenu()
{
	if (PauseMenu)
	{
		PauseMenu->RemoveFromParent();
	}
	ApplyUIInputMode();
	UE_LOG(LogVelox, Log, TEXT("VeloxMenu pause menu closed"));
}

void AVeloxPlayerController::ApplyUIInputMode()
{
	if (!IsLocalPlayerController() || IsMenuOpen())
	{
		return;
	}
	// The scoreboard's Ready button needs the cursor at the intermission; otherwise pure game input.
	bShowMouseCursor = bInIntermission;
	if (bInIntermission)
	{
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
	}
}
