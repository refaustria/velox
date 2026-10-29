// Velox - in-game pause menu (ADR 0004 section 7, task M3-10): resume, settings, disconnect to the main menu. Opened by
// AVeloxPlayerController on IA_Menu (InputTag.Menu, Escape). The game keeps running (online); the controller switches to
// UI input while the menu is up.

#pragma once

#include "Templates/SubclassOf.h"
#include "UI/VeloxUserWidget.h"

#include "VeloxPauseMenuWidget.generated.h"

class UButton;
class UTextBlock;
class UVeloxSettingsWidget;

UCLASS(Blueprintable, Config = Game)
class VELOX_API UVeloxPauseMenuWidget : public UVeloxUserWidget
{
	GENERATED_BODY()

public:
	UVeloxPauseMenuWidget(const FObjectInitializer& ObjectInitializer);

	/** Fired when the menu wants to close (Resume, Escape). The owner (the controller) removes it and restores game input. */
	DECLARE_MULTICAST_DELEGATE(FOnPauseMenuResume);
	FOnPauseMenuResume OnResume;

	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	void OpenSettings();

	/** Leaves the match: UVeloxGameInstance::ReturnToFrontEnd. */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	bool Disconnect();

	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	void Resume();

	UVeloxSettingsWidget* GetSettingsWidget() const { return SettingsWidget; }

	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> ResumeButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> SettingsButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> DisconnectButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

protected:
	//~UUserWidget interface
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	//~End of UUserWidget interface

	//~UVeloxUserWidget interface
	virtual UWidget* BuildDefaultTree() override;
	//~End of UVeloxUserWidget interface

	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Menu")
	TSubclassOf<UVeloxSettingsWidget> SettingsWidgetClass;

private:
	UFUNCTION()
	void HandleResume();
	UFUNCTION()
	void HandleSettings();
	UFUNCTION()
	void HandleDisconnect();

	void HandleSettingsClosed();

	UPROPERTY(Transient)
	TObjectPtr<UVeloxSettingsWidget> SettingsWidget;
};
