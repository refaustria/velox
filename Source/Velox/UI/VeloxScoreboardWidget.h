// Velox - scoreboard widget (ADR 0004 sections 5, 6 and 7): mode name, limits, clock, one row per player (colour, name,
// powerup letters, score, kills, deaths, ping, ready), the spectators, the winner line and a ready button. Shown while the
// scoreboard key is held and forced open in PostMatch (the view model decides, bScoreboardVisible). Same pattern as the HUD
// widget: programmatic tree, BindWidgetOptional elements, data only from UVeloxHUDViewModel.
//
// The widget itself always ticks (a collapsed widget would never wake up again); only its panel toggles visibility.

#pragma once

#include "UI/VeloxUserWidget.h"

#include "VeloxScoreboardWidget.generated.h"

class UBorder;
class UButton;
class UHorizontalBox;
class UVerticalBox;
class UVeloxHUDViewModel;

/** One scoreboard row: a tinted border around the six column texts (name, score, kills, deaths, ping, ready). */
USTRUCT()
struct FVeloxScoreRowWidgets
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UBorder> Border;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> Columns;
};

UCLASS(Blueprintable)
class VELOX_API UVeloxScoreboardWidget : public UVeloxUserWidget
{
	GENERATED_BODY()

public:
	/** The data source (owned by AVeloxHUD, shared with the HUD widget). Null shows nothing. */
	void SetViewModel(UVeloxHUDViewModel* InViewModel);

	/** Copies the view model into the elements. NativeTick calls it every frame; tests call it directly. */
	void RefreshFromViewModel();

	/** One line with the bound texts and row counts: the headless log and tests. */
	FString DescribeBoundValues() const;

	// ----- Test accessors.
	bool IsPanelVisible() const;
	int32 GetNumVisibleRows() const { return NumVisibleRows; }
	/** The row as the columns show it, joined by two spaces ("Anarki [Q]  5  5  1  23  READY"). */
	FString GetRowText(int32 Index) const;
	bool IsRowLocal(int32 Index) const;
	bool IsReadyButtonVisible() const;
	FString GetReadyButtonLabel() const;
	/** What clicking the button does, minus the RPC: toggles the local player's ready flag. */
	void ClickReadyButton() { HandleReadyClicked(); }

	// ----- Elements (all optional, bound by name in a Blueprint subclass).
	/** The whole scoreboard (toggled, never the widget itself). */
	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UBorder> ScoreboardPanel;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LimitsText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ClockText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WinnerText;

	/** Parent of the dynamic player rows. */
	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> RowsBox;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SpectatorsText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> ReadyButton;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ReadyButtonLabel;

protected:
	//~UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~End of UUserWidget interface

	//~UVeloxUserWidget interface
	virtual UWidget* BuildDefaultTree() override;
	//~End of UVeloxUserWidget interface

private:
	UFUNCTION()
	void HandleReadyClicked();

	/** The six column headers and a row's columns are laid out by the same weights. */
	UHorizontalBox* MakeColumnsBox(TArray<TObjectPtr<UTextBlock>>& OutColumns, int32 FontSize, const FLinearColor& Color);

	UPROPERTY(Transient)
	TObjectPtr<UVeloxHUDViewModel> ViewModel;

	UPROPERTY(Transient)
	TArray<FVeloxScoreRowWidgets> Rows;

	int32 NumVisibleRows = 0;
};
