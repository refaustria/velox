// Velox - settings screen (ADR 0004 section 7, task M3-10): sensitivity, FOV, invert, auto-switch, name, crosshair, audio,
// video, key bindings with rebind, apply / defaults / back. Programmatic widget tree on UVeloxUserWidget (a WBP_* subclass
// that supplies a tree and names the elements below replaces the look); the values are read from and written to
// UVeloxGameUserSettings (ReadFromSettings / WriteToSettings, usable without a screen for tests), keys go through
// UVeloxGameUserSettings::GetKeyBindings / RebindKey / ResetKeyBindings.

#pragma once

#include "InputCoreTypes.h"
#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxUserWidget.h"

#include "VeloxSettingsWidget.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;
class UEditableTextBox;
class USlider;
class UTextBlock;
class UVerticalBox;
class UVeloxSettingsWidget;

/** Receives the click of one key binding button (a dynamic delegate carries no payload, so each row has one proxy). */
UCLASS()
class VELOX_API UVeloxKeyRowProxy : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UVeloxSettingsWidget> Owner;
	int32 RowIndex = INDEX_NONE;

	UFUNCTION()
	void HandleClicked();
};

UCLASS(Blueprintable)
class VELOX_API UVeloxSettingsWidget : public UVeloxUserWidget
{
	GENERATED_BODY()

public:
	UVeloxSettingsWidget(const FObjectInitializer& ObjectInitializer);

	/** Fired when the screen closes itself (Back, Escape). The opener removes nothing: the widget removes itself first. */
	DECLARE_MULTICAST_DELEGATE(FOnSettingsClosed);
	FOnSettingsClosed OnClosed;

	/** Controls from Settings. bKeepIdentity leaves the player name field alone (Defaults must not wipe the name). */
	void ReadFromSettings(const UVeloxGameUserSettings& Settings, bool bKeepIdentity = false);
	/** Settings from the controls (values are clamped later by ValidateSettings). */
	void WriteToSettings(UVeloxGameUserSettings& Settings) const;

	/** Apply button: WriteToSettings into the live settings, apply (resolution, window, vsync, AA, audio...) and save. */
	UFUNCTION(BlueprintCallable, Category = "Velox|UI")
	void ApplyChanges();

	/** Defaults button: the controls show the defaults (identity kept) and the key bindings are reset; Apply commits the rest. */
	UFUNCTION(BlueprintCallable, Category = "Velox|UI")
	void RestoreDefaults();

	UFUNCTION(BlueprintCallable, Category = "Velox|UI")
	void Close();

	/** Rebuilds the key binding rows from the Enhanced Input user settings (empty without a local player). */
	void RebuildKeyRows();
	int32 GetKeyRowCount() const { return KeyRows.Num(); }
	/** Row text "Display name = Key" for tests and dumps. */
	FString DescribeKeyRow(int32 RowIndex) const;

	/** Rebind mode: the next key or mouse button press becomes the binding of row RowIndex (Escape cancels). */
	bool BeginRebind(int32 RowIndex);
	void CancelRebind();
	bool IsRebinding() const { return RebindRow != INDEX_NONE; }
	/** Commits Key as the binding of the row in rebind mode. False (status shows the reason) when refused. */
	bool CommitRebind(const FKey& Key);

	/** One line with every control's value, for logs. */
	FString DescribeValues() const;

	UFUNCTION(BlueprintCallable, Category = "Velox|UI")
	void SetStatus(const FString& Text, bool bError);

	// Elements. All optional (BindWidgetOptional): a Blueprint skin supplies the ones it wants.
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<USlider> SensitivitySlider;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SensitivityValue;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> InvertYCheck;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<USlider> FovSlider;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> FovValue;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> AutoSwitchCheck;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> PlayerNameBox;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CrosshairStyleCombo;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<USlider> CrosshairSizeSlider;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> CrosshairSizeValue;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<USlider> MasterVolumeSlider;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MasterVolumeValue;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<USlider> EffectsVolumeSlider;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> EffectsVolumeValue;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<USlider> MusicVolumeSlider;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> MusicVolumeValue;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> AntiAliasingCombo;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> WindowModeCombo;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> ResolutionCombo;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UCheckBox> VSyncCheck;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> FrameRateCombo;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> KeyList;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> ApplyButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> DefaultsButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> BackButton;

protected:
	//~UUserWidget interface
	virtual void NativeConstruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	//~End of UUserWidget interface

	//~UVeloxUserWidget interface
	virtual UWidget* BuildDefaultTree() override;
	//~End of UVeloxUserWidget interface

private:
	UFUNCTION()
	void HandleSliderChanged(float Value);
	UFUNCTION()
	void HandleApply();
	UFUNCTION()
	void HandleDefaults();
	UFUNCTION()
	void HandleBack();

	/** Binds the click / change delegates of whatever elements exist (also for a Blueprint-supplied tree). */
	void BindElements();
	void RefreshValueTexts();
	void FillResolutionOptions(const FIntPoint& Current);
	void FillFrameRateOptions(float CurrentLimit);

	struct FKeyRow
	{
		FName MappingName;
		FString DisplayName;
		FKey CurrentKey;
		TWeakObjectPtr<UTextBlock> KeyLabel;
	};
	TArray<FKeyRow> KeyRows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UVeloxKeyRowProxy>> KeyProxies;

	int32 RebindRow = INDEX_NONE;
};
