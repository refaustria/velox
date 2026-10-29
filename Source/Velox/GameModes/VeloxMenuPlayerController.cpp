// Velox - front-end player controller.

#include "GameModes/VeloxMenuPlayerController.h"

#include "Core/VeloxLog.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "UI/VeloxMainMenuWidget.h"
#include "UserSettings/EnhancedInputUserSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxMenuPlayerController)

AVeloxMenuPlayerController::AVeloxMenuPlayerController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	MainMenuWidgetClass = UVeloxMainMenuWidget::StaticClass();
	bShowMouseCursor = true;
}

void AVeloxMenuPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (!IsLocalPlayerController() || !MainMenuWidgetClass)
	{
		return;
	}
	RegisterKeyBindingContext();

	MenuWidget = CreateWidget<UVeloxMainMenuWidget>(this, MainMenuWidgetClass);
	if (MenuWidget)
	{
		MenuWidget->AddToViewport(MenuWidgetZOrder);
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(MenuWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(InputMode);
	}
	bShowMouseCursor = true;
	UE_LOG(LogVelox, Log, TEXT("VeloxMenu widget class=%s created=%d in_viewport=%d status='%s'"), *GetNameSafe(MainMenuWidgetClass), MenuWidget ? 1 : 0,
		MenuWidget && MenuWidget->IsInViewport() ? 1 : 0, MenuWidget ? *MenuWidget->GetStatusString() : TEXT(""));
}

void AVeloxMenuPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MenuWidget)
	{
		MenuWidget->RemoveFromParent();
		MenuWidget = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AVeloxMenuPlayerController::RegisterKeyBindingContext()
{
	if (KeyBindingContext.IsNull())
	{
		return;
	}
	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!InputSubsystem)
	{
		return;
	}
	if (UEnhancedInputUserSettings* UserSettings = InputSubsystem->GetUserSettings())
	{
		if (const UInputMappingContext* Context = KeyBindingContext.LoadSynchronous())
		{
			UserSettings->RegisterInputMappingContext(Context);
		}
	}
	else
	{
		InputSubsystem->OnPostUserSettingsInitialized.AddUniqueDynamic(this, &ThisClass::OnInputUserSettingsInitialized);
	}
}

void AVeloxMenuPlayerController::OnInputUserSettingsInitialized(const UEnhancedInputUserSettings* Settings)
{
	RegisterKeyBindingContext();
}
