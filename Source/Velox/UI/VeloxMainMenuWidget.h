// Velox - main menu (ADR 0004 section 7, task M3-10): connect by IP (last address remembered), host local, settings, quit.
// Shown by AVeloxMenuPlayerController on the front-end map. Also shows the message a failed connect left in the game
// instance (UVeloxGameInstance::ConsumeMenuMessage).

#pragma once

#include "Templates/SubclassOf.h"
#include "UI/VeloxUserWidget.h"

#include "VeloxMainMenuWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UVeloxSettingsWidget;

UCLASS(Blueprintable, Config = Game)
class VELOX_API UVeloxMainMenuWidget : public UVeloxUserWidget
{
	GENERATED_BODY()

public:
	UVeloxMainMenuWidget(const FObjectInitializer& ObjectInitializer);

	/** Connect button / Enter in the address field: UVeloxGameInstance::ConnectToServer with the typed address. */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	bool Connect();

	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	bool HostLocal();

	/** Opens the settings screen on top; the menu is hidden to input until it closes. */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	void OpenSettings();

	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	void QuitGame();

	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	void SetStatus(const FString& Text, bool bError);

	/** Status line text and whether it is an error (tests, dumps). */
	FString GetStatusString() const;
	UVeloxSettingsWidget* GetSettingsWidget() const { return SettingsWidget; }

	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> AddressBox;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConnectButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> HostButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> SettingsButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitButton;

protected:
	//~UUserWidget interface
	virtual void NativeConstruct() override;
	//~End of UUserWidget interface

	//~UVeloxUserWidget interface
	virtual UWidget* BuildDefaultTree() override;
	//~End of UVeloxUserWidget interface

	/** The settings screen class (a WBP_Settings subclass once authored). Set in DefaultGame.ini. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Menu")
	TSubclassOf<UVeloxSettingsWidget> SettingsWidgetClass;

private:
	UFUNCTION()
	void HandleConnect();
	UFUNCTION()
	void HandleHost();
	UFUNCTION()
	void HandleSettings();
	UFUNCTION()
	void HandleQuit();
	UFUNCTION()
	void HandleAddressCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	void HandleSettingsClosed();

	UPROPERTY(Transient)
	TObjectPtr<UVeloxSettingsWidget> SettingsWidget;
};
