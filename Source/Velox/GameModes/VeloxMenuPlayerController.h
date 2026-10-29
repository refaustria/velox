// Velox - front-end player controller (ADR 0004 section 7, task M3-10): shows the main menu with the mouse cursor and UI-only
// input, and registers the gameplay mapping context with the Enhanced Input user settings so the settings screen can list
// and rebind the key bindings before a match is ever joined.

#pragma once

#include "GameFramework/PlayerController.h"
#include "Templates/SubclassOf.h"

#include "VeloxMenuPlayerController.generated.h"

class UEnhancedInputUserSettings;
class UInputMappingContext;
class UVeloxMainMenuWidget;

UCLASS(Config = Game)
class VELOX_API AVeloxMenuPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AVeloxMenuPlayerController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UVeloxMainMenuWidget* GetMainMenuWidget() const { return MenuWidget; }

	//~AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of AActor interface

protected:
	/** The main menu widget class (DefaultGame.ini [/Script/Velox.VeloxMenuPlayerController]): a WBP_MainMenu subclass once authored. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Menu")
	TSubclassOf<UVeloxMainMenuWidget> MainMenuWidgetClass;

	/** Registered with the user settings (not added as an active context): the rows of the key binding list. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Menu")
	TSoftObjectPtr<UInputMappingContext> KeyBindingContext;

	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Menu")
	int32 MenuWidgetZOrder = 0;

private:
	UFUNCTION()
	void OnInputUserSettingsInitialized(const UEnhancedInputUserSettings* Settings);

	void RegisterKeyBindingContext();

	UPROPERTY(Transient)
	TObjectPtr<UVeloxMainMenuWidget> MenuWidget;
};
