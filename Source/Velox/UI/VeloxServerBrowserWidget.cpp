// Velox - server browser screen (ADR 0007 section 2, task M6-05).

#include "UI/VeloxServerBrowserWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/VeloxAudioSettings.h"
#include "Core/VeloxGameInstance.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Net/VeloxServerDirectorySubsystem.h"
#include "UI/VeloxUISettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxServerBrowserWidget)

namespace VeloxServerBrowserPrivate
{
	constexpr int32 ColumnCount = 6;

	float ColumnFill(const UVeloxUISettings& UI, int32 Column)
	{
		return UI.ServerBrowserColumnFill.IsValidIndex(Column) ? FMath::Max(UI.ServerBrowserColumnFill[Column], 0.1f) : 1.f;
	}

	FString ColumnHeader(const UVeloxUISettings& UI, int32 Column)
	{
		return UI.ServerBrowserColumnHeaders.IsValidIndex(Column) ? UI.ServerBrowserColumnHeaders[Column] : FString();
	}

	void AddCell(UHorizontalBox* Row, UWidget* Cell, float Fill)
	{
		if (UHorizontalBoxSlot* CellSlot = Row->AddChildToHorizontalBox(Cell))
		{
			FSlateChildSize Size(ESlateSizeRule::Fill);
			Size.Value = Fill;
			CellSlot->SetSize(Size);
			CellSlot->SetPadding(FMargin(4.f, 0.f));
		}
	}

	FString PlayersText(const FVeloxServerEntry& Entry)
	{
		FString Text = FString::Printf(TEXT("%d/%d"), Entry.Players, Entry.MaxPlayers);
		if (Entry.Bots > 0)
		{
			Text += FString::Printf(TEXT(" (+%d bots)"), Entry.Bots);
		}
		return Text;
	}

	FString NameText(const FVeloxServerEntry& Entry, EVeloxServerCompat Compat)
	{
		FString Text = Entry.Name;
		if (Entry.bPassword)
		{
			Text += UVeloxUISettings::Get().ServerBrowserLockedSuffix;
		}
		if (Compat != EVeloxServerCompat::Ok)
		{
			Text += FString::Printf(TEXT(" (%s)"), *UVeloxServerBrowserWidget::CompatLabel(Compat));
		}
		return Text;
	}
}

void UVeloxServerRowProxy::HandleClicked()
{
	UVeloxAudioSettings::PlayUIClick(Owner.Get());
	if (UVeloxServerBrowserWidget* Widget = Owner.Get())
	{
		Widget->SelectRow(RowIndex);
	}
}

UVeloxServerBrowserWidget::UVeloxServerBrowserWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(true);
}

FString UVeloxServerBrowserWidget::CompatLabel(EVeloxServerCompat Compat)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	switch (Compat)
	{
	case EVeloxServerCompat::VersionMismatch:
		return UI.ServerCompatVersionText;
	case EVeloxServerCompat::NetVersionMismatch:
		return UI.ServerCompatNetVersionText;
	case EVeloxServerCompat::Stale:
		return UI.ServerCompatStaleText;
	case EVeloxServerCompat::Full:
		return UI.ServerCompatFullText;
	default:
		return FString();
	}
}

UWidget* UVeloxServerBrowserWidget::BuildDefaultTree()
{
	using namespace VeloxServerBrowserPrivate;
	const UVeloxUISettings& UI = UVeloxUISettings::Get();

	UCanvasPanel* Root = MakeWidget<UCanvasPanel>(TEXT("Root"));
	UImage* Backdrop = MakeWidget<UImage>(TEXT("Backdrop"));
	Backdrop->SetBrush(FSlateColorBrush(FLinearColor::White));
	Backdrop->SetColorAndOpacity(UI.MenuBackgroundColor);
	if (UCanvasPanelSlot* BackdropSlot = Root->AddChildToCanvas(Backdrop))
	{
		BackdropSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		BackdropSlot->SetOffsets(FMargin(0.f));
	}

	UBorder* Panel = MakeWidget<UBorder>(TEXT("Panel"));
	Panel->SetBrush(FSlateColorBrush(FLinearColor::White));
	Panel->SetBrushColor(UI.MenuPanelColor);
	Panel->SetPadding(FMargin(UI.MenuRowPadding * 3.f));
	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		PanelSlot->SetAlignment(FVector2D(0.5, 0.5));
		PanelSlot->SetAutoSize(true);
		PanelSlot->SetPosition(FVector2D::ZeroVector);
	}

	USizeBox* PanelSize = MakeWidget<USizeBox>(TEXT("PanelSize"));
	PanelSize->SetWidthOverride(UI.ServerBrowserPanelWidth);
	Panel->SetContent(PanelSize);
	UVerticalBox* Column = MakeWidget<UVerticalBox>(TEXT("Column"));
	PanelSize->AddChild(Column);
	Column->AddChildToVerticalBox(MakeText(TEXT("Title"), UI.MenuHeadingFontSize, UI.MenuTitleColor, UI.ServerBrowserTitleText));

	// Header row.
	UHorizontalBox* Header = MakeWidget<UHorizontalBox>(TEXT("Header"));
	for (int32 C = 0; C < ColumnCount; ++C)
	{
		AddCell(Header, MakeText(NAME_None, UI.MenuFontSize, UI.MenuTitleColor, ColumnHeader(UI, C)), ColumnFill(UI, C));
	}
	if (UVerticalBoxSlot* HeaderSlot = Column->AddChildToVerticalBox(Header))
	{
		HeaderSlot->SetPadding(FMargin(UI.MenuRowPadding * 2.f, UI.MenuRowPadding * 2.f, UI.MenuRowPadding * 2.f, UI.MenuRowPadding));
	}

	USizeBox* ListSize = MakeWidget<USizeBox>(TEXT("ListSize"));
	ListSize->SetMaxDesiredHeight(UI.ServerBrowserListMaxHeight);
	UScrollBox* Scroll = MakeWidget<UScrollBox>(TEXT("Scroll"));
	ListSize->AddChild(Scroll);
	Column->AddChildToVerticalBox(ListSize);
	ListBox = MakeWidget<UVerticalBox>(TEXT("ListBox"));
	Scroll->AddChild(ListBox);

	StatusText = MakeText(TEXT("StatusText"), UI.MenuFontSize, UI.MenuTextColor, FString());
	StatusText->SetAutoWrapText(true);
	if (UVerticalBoxSlot* StatusSlot = Column->AddChildToVerticalBox(StatusText))
	{
		StatusSlot->SetPadding(FMargin(0.f, UI.MenuRowPadding * 2.f));
	}

	UHorizontalBox* Buttons = MakeWidget<UHorizontalBox>(TEXT("Buttons"));
	PasswordBox = MakeWidget<UEditableTextBox>(TEXT("PasswordBox"));
	PasswordBox->SetIsPassword(true);
	PasswordBox->SetHintText(FText::FromString(UI.ServerBrowserPasswordHint));
	USizeBox* PasswordSize = MakeWidget<USizeBox>(NAME_None);
	PasswordSize->SetWidthOverride(UI.MenuButtonWidth);
	PasswordSize->AddChild(PasswordBox);
	if (UHorizontalBoxSlot* PasswordSlot = Buttons->AddChildToHorizontalBox(PasswordSize))
	{
		PasswordSlot->SetPadding(FMargin(0.f, 0.f, UI.MenuRowPadding * 2.f, 0.f));
		PasswordSlot->SetVerticalAlignment(VAlign_Center);
	}
	auto AddButton = [&](TObjectPtr<UButton>& Out, const TCHAR* Name, const TCHAR* Label)
	{
		Out = MakeButton(Name, Label);
		if (UHorizontalBoxSlot* ButtonSlot = Buttons->AddChildToHorizontalBox(Out))
		{
			ButtonSlot->SetPadding(FMargin(UI.MenuRowPadding, 0.f));
		}
	};
	AddButton(RefreshButton, TEXT("RefreshButton"), TEXT("Refresh"));
	AddButton(ConnectButton, TEXT("ConnectButton"), TEXT("Connect"));
	AddButton(BackButton, TEXT("BackButton"), TEXT("Back"));
	Column->AddChildToVerticalBox(Buttons);
	return Root;
}

void UVeloxServerBrowserWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (RefreshButton)
	{
		RefreshButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleRefresh);
	}
	if (ConnectButton)
	{
		ConnectButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConnect);
	}
	if (BackButton)
	{
		BackButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleBack);
	}
	if (UVeloxServerDirectorySubsystem* Directory = GetDirectory())
	{
		UpdatedHandle = Directory->OnServersUpdatedNative.AddUObject(this, &ThisClass::HandleServersUpdated);
		RebuildRows();
		Directory->StartAutoRefresh();
		RefreshServers();
	}
	else
	{
		SetStatus(UVeloxUISettings::Get().ServerListUnreachableText, true);
	}
}

void UVeloxServerBrowserWidget::NativeDestruct()
{
	if (UVeloxServerDirectorySubsystem* Directory = GetDirectory())
	{
		Directory->StopAutoRefresh();
		Directory->OnServersUpdatedNative.Remove(UpdatedHandle);
	}
	UpdatedHandle.Reset();
	Super::NativeDestruct();
}

UVeloxServerDirectorySubsystem* UVeloxServerBrowserWidget::GetDirectory() const
{
	const UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UVeloxServerDirectorySubsystem>() : nullptr;
}

void UVeloxServerBrowserWidget::RefreshServers()
{
	UVeloxServerDirectorySubsystem* Directory = GetDirectory();
	if (!Directory)
	{
		return;
	}
	if (Directory->Refresh() || Directory->IsRefreshing())
	{
		SetStatus(UVeloxUISettings::Get().ServerBrowserFetchingText, false);
	}
}

void UVeloxServerBrowserWidget::HandleServersUpdated()
{
	RebuildRows();
}

void UVeloxServerBrowserWidget::RebuildRows()
{
	using namespace VeloxServerBrowserPrivate;
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const UVeloxServerDirectorySubsystem* Directory = GetDirectory();

	FString SelectedAddress;
	if (RowEntries.IsValidIndex(SelectedIndex))
	{
		SelectedAddress = RowEntries[SelectedIndex].Address;
	}

	RowEntries.Reset();
	if (Directory)
	{
		RowEntries = Directory->GetServers();
	}
	RowCount = RowEntries.Num();
	RowButtons.Reset();
	RowProxies.Reset();
	SelectedIndex = INDEX_NONE;
	if (ListBox)
	{
		ListBox->ClearChildren();
	}

	for (int32 Index = 0; Index < RowCount; ++Index)
	{
		const FVeloxServerEntry& Entry = RowEntries[Index];
		if (Entry.Address == SelectedAddress)
		{
			SelectedIndex = Index;
		}
		if (!ListBox)
		{
			continue;
		}
		const EVeloxServerCompat Compat = Directory->Classify(Entry);
		const FLinearColor Color = (Compat == EVeloxServerCompat::Ok || Compat == EVeloxServerCompat::Full) ? UI.MenuTextColor : UI.MenuMutedTextColor;

		UHorizontalBox* Cells = MakeWidget<UHorizontalBox>(NAME_None);
		const FString Texts[ColumnCount] = {NameText(Entry, Compat), Entry.MapDisplayName, Entry.Mode, PlayersText(Entry), Entry.Phase, Entry.Version};
		for (int32 C = 0; C < ColumnCount; ++C)
		{
			AddCell(Cells, MakeText(NAME_None, UI.MenuFontSize, Color, Texts[C]), ColumnFill(UI, C));
		}
		UButton* RowButton = MakeButton(NAME_None, FString());
		RowButton->ClearChildren();
		RowButton->AddChild(Cells);
		UVeloxServerRowProxy* Proxy = NewObject<UVeloxServerRowProxy>(this);
		Proxy->Owner = this;
		Proxy->RowIndex = Index;
		RowButton->OnClicked.AddDynamic(Proxy, &UVeloxServerRowProxy::HandleClicked);
		RowButtons.Add(RowButton);
		RowProxies.Add(Proxy);
		if (UVerticalBoxSlot* RowSlot = ListBox->AddChildToVerticalBox(RowButton))
		{
			RowSlot->SetPadding(FMargin(0.f, UI.MenuRowPadding * 0.5f));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
	SelectRow(SelectedIndex);

	if (Directory)
	{
		if (!Directory->GetLastError().IsEmpty())
		{
			SetStatus(Directory->GetLastError(), true);
		}
		else if (RowCount == 0)
		{
			SetStatus(UI.ServerBrowserEmptyText, false);
		}
		else
		{
			SetStatus(UI.ServerBrowserCountText.Replace(TEXT("%d"), *FString::FromInt(RowCount)), false);
		}
	}
}

void UVeloxServerBrowserWidget::SelectRow(int32 Index)
{
	SelectedIndex = (Index >= 0 && Index < RowCount) ? Index : INDEX_NONE;
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	for (int32 Row = 0; Row < RowButtons.Num(); ++Row)
	{
		if (RowButtons[Row])
		{
			RowButtons[Row]->SetBackgroundColor(Row == SelectedIndex ? UI.ServerBrowserSelectedTint : FLinearColor::White);
		}
	}
}

const FVeloxServerEntry* UVeloxServerBrowserWidget::GetRowEntry(int32 Index) const
{
	return RowEntries.IsValidIndex(Index) ? &RowEntries[Index] : nullptr;
}

FString UVeloxServerBrowserWidget::DescribeRow(int32 Index) const
{
	using namespace VeloxServerBrowserPrivate;
	const FVeloxServerEntry* Entry = GetRowEntry(Index);
	const UVeloxServerDirectorySubsystem* Directory = GetDirectory();
	if (!Entry || !Directory)
	{
		return FString();
	}
	return FString::Printf(TEXT("%s | %s | %s | %s | %s | %s"), *NameText(*Entry, Directory->Classify(*Entry)), *Entry->MapDisplayName, *Entry->Mode, *PlayersText(*Entry), *Entry->Phase, *Entry->Version);
}

bool UVeloxServerBrowserWidget::ConnectSelected()
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const FVeloxServerEntry* Entry = GetRowEntry(SelectedIndex);
	const UVeloxServerDirectorySubsystem* Directory = GetDirectory();
	if (!Entry || !Directory)
	{
		SetStatus(UI.ServerBrowserSelectText, true);
		return false;
	}
	const EVeloxServerCompat Compat = Directory->Classify(*Entry);
	if (!VeloxServerList::IsConnectable(Compat))
	{
		SetStatus(FString::Printf(TEXT("%s: %s"), *Entry->Name, *CompatLabel(Compat)), true);
		return false;
	}
	UVeloxGameInstance* GameInstance = Cast<UVeloxGameInstance>(GetWorld() ? GetWorld()->GetGameInstance() : nullptr);
	if (!GameInstance)
	{
		SetStatus(TEXT("No game instance."), true);
		return false;
	}
	FString Error;
	const FString Password = PasswordBox ? PasswordBox->GetText().ToString() : FString();
	if (!GameInstance->ConnectToServerWithPassword(Entry->Address, Password, Error))
	{
		SetStatus(Error, true);
		return false;
	}
	SetStatus(FString::Printf(TEXT("Connecting to %s ..."), *GameInstance->GetLastConnectTarget()), false);
	return true;
}

void UVeloxServerBrowserWidget::Close()
{
	RemoveFromParent();
	OnClosed.Broadcast();
}

void UVeloxServerBrowserWidget::SetStatus(const FString& Text, bool bError)
{
	if (StatusText)
	{
		const UVeloxUISettings& UI = UVeloxUISettings::Get();
		StatusText->SetText(FText::FromString(Text));
		StatusText->SetColorAndOpacity(FSlateColor(bError ? UI.MenuErrorColor : UI.MenuTextColor));
	}
}

FString UVeloxServerBrowserWidget::GetStatusString() const
{
	return StatusText ? StatusText->GetText().ToString() : FString();
}

void UVeloxServerBrowserWidget::HandleRefresh()
{
	UVeloxAudioSettings::PlayUIClick(this);
	RefreshServers();
}

void UVeloxServerBrowserWidget::HandleConnect()
{
	UVeloxAudioSettings::PlayUIClick(this);
	ConnectSelected();
}

void UVeloxServerBrowserWidget::HandleBack()
{
	UVeloxAudioSettings::PlayUIClick(this);
	Close();
}

FReply UVeloxServerBrowserWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Escape)
	{
		Close();
		return FReply::Handled();
	}
	if (Key == EKeys::Enter || Key == EKeys::Virtual_Accept)
	{
		ConnectSelected();
		return FReply::Handled();
	}
	if (Key == EKeys::Down || Key == EKeys::Up)
	{
		if (RowCount > 0)
		{
			const int32 Step = Key == EKeys::Down ? 1 : -1;
			SelectRow(SelectedIndex == INDEX_NONE ? (Step > 0 ? 0 : RowCount - 1) : FMath::Clamp(SelectedIndex + Step, 0, RowCount - 1));
		}
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}
