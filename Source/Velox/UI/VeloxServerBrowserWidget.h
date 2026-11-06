// Velox - server browser screen (ADR 0007 section 2, task M6-05; ADR 0004 section 7 pattern). Opened from the main menu
// ("Servers"). Rows come from UVeloxServerDirectorySubsystem (servers.json); a row is a button, the selected one is
// highlighted, Connect / Enter goes through UVeloxGameInstance::ConnectToServerWithPassword (password sent only when the
// box is non-empty), Escape / Back closes. Rows that are not Ok are greyed and carry the reason as a text suffix.
//
//   | Server                      | Map        | Mode | Players       | Phase | Version |
//   | Velox FFA [password]        | Praesidium | ffa  | 3/8 (+3 bots) | match | 0.6.0   |
//   | Velox Duel (full - join ...)| Praesidium | duel | 2/2           | match | 0.6.0   |
//   | Old (other game version)    | ...                                                   (greyed)
//   status line                                             [password box] [Refresh] [Connect] [Back]

#pragma once

#include "Net/VeloxServerList.h"
#include "UI/VeloxUserWidget.h"

#include "VeloxServerBrowserWidget.generated.h"

class UButton;
class UEditableTextBox;
class UTextBlock;
class UVerticalBox;
class UVeloxServerBrowserWidget;
class UVeloxServerDirectorySubsystem;

/** Receives the click of one server row (a dynamic delegate carries no payload, so each row has one proxy). */
UCLASS()
class VELOX_API UVeloxServerRowProxy : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UVeloxServerBrowserWidget> Owner;
	int32 RowIndex = INDEX_NONE;

	UFUNCTION()
	void HandleClicked();
};

UCLASS(Blueprintable)
class VELOX_API UVeloxServerBrowserWidget : public UVeloxUserWidget
{
	GENERATED_BODY()

public:
	UVeloxServerBrowserWidget(const FObjectInitializer& ObjectInitializer);

	/** Fired when the screen closes itself (Back, Escape); the widget has removed itself from the viewport first. */
	DECLARE_MULTICAST_DELEGATE(FOnServerBrowserClosed);
	FOnServerBrowserClosed OnClosed;

	/** Starts a fetch now (Refresh button). */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	void RefreshServers();

	/** Connects to the selected row. False (status line shows why) when nothing is selected, the row is incompatible or the travel was refused. */
	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	bool ConnectSelected();

	UFUNCTION(BlueprintCallable, Category = "Velox|Menu")
	void Close();

	/** Selects row Index (clamped; INDEX_NONE clears) and highlights it. */
	void SelectRow(int32 Index);
	int32 GetSelectedIndex() const { return SelectedIndex; }
	int32 GetRowCount() const { return RowCount; }
	/** The directory entry behind row Index (null when out of range). */
	const FVeloxServerEntry* GetRowEntry(int32 Index) const;
	/** Plain text of row Index as shown ("name | map | mode | players | phase | version"), tests and dumps. */
	FString DescribeRow(int32 Index) const;

	/** Rebuilds the rows from the subsystem's current list and refreshes the status line. */
	void RebuildRows();

	void SetStatus(const FString& Text, bool bError);
	FString GetStatusString() const;

	/** The text of a classification from UVeloxUISettings. */
	static FString CompatLabel(EVeloxServerCompat Compat);

	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ListBox;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> PasswordBox;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> RefreshButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> ConnectButton;
	UPROPERTY(BlueprintReadOnly, Transient, Meta = (BindWidgetOptional))
	TObjectPtr<UButton> BackButton;

protected:
	//~UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	//~End of UUserWidget interface

	//~UVeloxUserWidget interface
	virtual UWidget* BuildDefaultTree() override;
	//~End of UVeloxUserWidget interface

private:
	UFUNCTION()
	void HandleRefresh();
	UFUNCTION()
	void HandleConnect();
	UFUNCTION()
	void HandleBack();

	void HandleServersUpdated();
	UVeloxServerDirectorySubsystem* GetDirectory() const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UButton>> RowButtons;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UVeloxServerRowProxy>> RowProxies;

	/** Snapshot of the entries the rows were built from (the subsystem may update while a row is clicked). */
	TArray<FVeloxServerEntry> RowEntries;
	int32 RowCount = 0;
	int32 SelectedIndex = INDEX_NONE;
	FDelegateHandle UpdatedHandle;
};
