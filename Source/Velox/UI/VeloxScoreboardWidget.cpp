// Velox - scoreboard widget.

#include "UI/VeloxScoreboardWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameModes/VeloxPlayerController.h"
#include "UI/VeloxHUDViewModel.h"
#include "UI/VeloxScoreboardFormat.h"
#include "UI/VeloxUISettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxScoreboardWidget)

namespace
{
	/** Columns of the table: name, score, kills, deaths, ping, ready. */
	constexpr int32 VeloxScoreboardNumColumns = 6;

	/** Only touched when the value changes (a Slate invalidation per element per frame would defeat the point). */
	void VeloxScoreboardSetText(UTextBlock* Block, const FString& Text)
	{
		if (Block && !Block->GetText().ToString().Equals(Text, ESearchCase::CaseSensitive))
		{
			Block->SetText(FText::FromString(Text));
		}
	}

	void VeloxScoreboardSetTextColor(UTextBlock* Block, const FLinearColor& Color)
	{
		if (Block && !Block->GetColorAndOpacity().GetSpecifiedColor().Equals(Color, KINDA_SMALL_NUMBER))
		{
			Block->SetColorAndOpacity(FSlateColor(Color));
		}
	}

	void VeloxScoreboardSetVisibility(UWidget* Widget, ESlateVisibility Wanted)
	{
		if (Widget && Widget->GetVisibility() != Wanted)
		{
			Widget->SetVisibility(Wanted);
		}
	}

	/** Visible and showing text, or collapsed when the text is empty. */
	void VeloxScoreboardSetOptionalText(UTextBlock* Block, const FString& Text)
	{
		VeloxScoreboardSetText(Block, Text);
		VeloxScoreboardSetVisibility(Block, Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

UHorizontalBox* UVeloxScoreboardWidget::MakeColumnsBox(TArray<TObjectPtr<UTextBlock>>& OutColumns, int32 FontSize, const FLinearColor& Color)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	UHorizontalBox* Box = MakeWidget<UHorizontalBox>();
	if (!Box)
	{
		return nullptr;
	}
	for (int32 Column = 0; Column < VeloxScoreboardNumColumns; ++Column)
	{
		UTextBlock* Text = MakeText(NAME_None, FontSize, Color);
		if (!Text)
		{
			continue;
		}
		OutColumns.Add(Text);
		if (UHorizontalBoxSlot* BoxSlot = Box->AddChildToHorizontalBox(Text))
		{
			BoxSlot->SetHorizontalAlignment(Column == 0 ? HAlign_Left : HAlign_Right);
			// The fill value is the column weight: the name column is wider.
			FSlateChildSize Size(ESlateSizeRule::Fill);
			Size.Value = Column == 0 ? UI.ScoreboardNameColumnFill : 1.f;
			BoxSlot->SetSize(Size);
		}
	}
	return Box;
}

UWidget* UVeloxScoreboardWidget::BuildDefaultTree()
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	UCanvasPanel* Root = MakeWidget<UCanvasPanel>(TEXT("ScoreboardRoot"));
	ScoreboardPanel = MakeWidget<UBorder>(TEXT("ScoreboardPanel"));
	USizeBox* Size = MakeWidget<USizeBox>(TEXT("ScoreboardSize"));
	UVerticalBox* Column = MakeWidget<UVerticalBox>(TEXT("ScoreboardColumn"));
	if (!Root || !ScoreboardPanel || !Size || !Column)
	{
		return nullptr;
	}
	ScoreboardPanel->SetBrush(FSlateColorBrush(FLinearColor::White));
	ScoreboardPanel->SetBrushColor(UI.ScoreboardBackgroundColor);
	ScoreboardPanel->SetPadding(FMargin(UI.ScreenMargin));
	ScoreboardPanel->SetVisibility(ESlateVisibility::Collapsed);
	Size->SetWidthOverride(UI.ScoreboardWidth);
	Size->AddChild(Column);
	ScoreboardPanel->SetContent(Size);

	// Title line: mode name, limits, clock.
	UHorizontalBox* TitleLine = MakeWidget<UHorizontalBox>(TEXT("ScoreboardTitleLine"));
	TitleText = MakeText(TEXT("ScoreboardTitleText"), UI.MediumFontSize, UI.TextColor);
	LimitsText = MakeText(TEXT("ScoreboardLimitsText"), UI.SmallFontSize, UI.ScoreboardHeaderColor);
	ClockText = MakeText(TEXT("ScoreboardClockText"), UI.MediumFontSize, UI.TextColor);
	if (UHorizontalBoxSlot* TitleSlot = TitleLine->AddChildToHorizontalBox(TitleText))
	{
		TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	if (UHorizontalBoxSlot* LimitsSlot = TitleLine->AddChildToHorizontalBox(LimitsText))
	{
		LimitsSlot->SetVerticalAlignment(VAlign_Center);
		LimitsSlot->SetPadding(FMargin(UI.ScreenMargin, 0.f));
	}
	TitleLine->AddChildToHorizontalBox(ClockText);
	Column->AddChildToVerticalBox(TitleLine);

	WinnerText = MakeText(TEXT("ScoreboardWinnerText"), UI.BannerFontSize, UI.BannerColor);
	WinnerText->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* WinnerSlot = Column->AddChildToVerticalBox(WinnerText))
	{
		WinnerSlot->SetHorizontalAlignment(HAlign_Center);
	}

	// Header row.
	TArray<TObjectPtr<UTextBlock>> HeaderColumns;
	if (UHorizontalBox* Header = MakeColumnsBox(HeaderColumns, UI.SmallFontSize, UI.ScoreboardHeaderColor))
	{
		const TCHAR* Titles[VeloxScoreboardNumColumns] = { TEXT("Player"), TEXT("Score"), TEXT("Kills"), TEXT("Deaths"), TEXT("Ping"), TEXT("Ready") };
		for (int32 Index = 0; Index < HeaderColumns.Num() && Index < VeloxScoreboardNumColumns; ++Index)
		{
			HeaderColumns[Index]->SetText(FText::FromString(Titles[Index]));
		}
		if (UVerticalBoxSlot* HeaderSlot = Column->AddChildToVerticalBox(Header))
		{
			HeaderSlot->SetPadding(FMargin(0.f, UI.ScreenMargin * 0.5f, 0.f, 4.f));
		}
	}

	RowsBox = MakeWidget<UVerticalBox>(TEXT("ScoreboardRows"));
	Column->AddChildToVerticalBox(RowsBox);

	SpectatorsText = MakeText(TEXT("ScoreboardSpectatorsText"), UI.SmallFontSize, UI.ScoreboardHeaderColor);
	if (UVerticalBoxSlot* SpectatorSlot = Column->AddChildToVerticalBox(SpectatorsText))
	{
		SpectatorSlot->SetPadding(FMargin(0.f, UI.ScreenMargin * 0.5f, 0.f, 0.f));
	}

	// Ready button.
	ReadyButton = MakeWidget<UButton>(TEXT("ScoreboardReadyButton"));
	ReadyButtonLabel = MakeText(TEXT("ScoreboardReadyLabel"), UI.MediumFontSize, UI.ScoreboardReadyColor);
	ReadyButton->SetContent(ReadyButtonLabel);
	ReadyButton->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* ButtonSlot = Column->AddChildToVerticalBox(ReadyButton))
	{
		ButtonSlot->SetHorizontalAlignment(HAlign_Center);
		ButtonSlot->SetPadding(FMargin(0.f, UI.ScreenMargin, 0.f, 0.f));
	}

	if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(ScoreboardPanel))
	{
		PanelSlot->SetAnchors(FAnchors(0.5f, 0.4f));
		PanelSlot->SetAlignment(FVector2D(0.5, 0.5));
		PanelSlot->SetAutoSize(true);
	}
	return Root;
}

void UVeloxScoreboardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// The widget never takes the mouse itself; only the ready button is hit-testable.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (ReadyButton)
	{
		ReadyButton->OnClicked.AddUniqueDynamic(this, &UVeloxScoreboardWidget::HandleReadyClicked);
	}
	RefreshFromViewModel();
}

void UVeloxScoreboardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshFromViewModel();
}

void UVeloxScoreboardWidget::SetViewModel(UVeloxHUDViewModel* InViewModel)
{
	ViewModel = InViewModel;
}

void UVeloxScoreboardWidget::HandleReadyClicked()
{
	if (AVeloxPlayerController* Controller = GetVeloxPlayerController())
	{
		Controller->Ready();
	}
}

void UVeloxScoreboardWidget::RefreshFromViewModel()
{
	if (!ViewModel)
	{
		return;
	}
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const UVeloxHUDViewModel& Model = *ViewModel;
	const bool bShow = Model.bHasPlayer && Model.bScoreboardVisible;
	VeloxScoreboardSetVisibility(ScoreboardPanel, bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (!bShow)
	{
		NumVisibleRows = 0;
		return;
	}

	VeloxScoreboardSetText(TitleText, Model.ScoreboardTitle);
	VeloxScoreboardSetText(LimitsText, Model.ScoreboardLimits);
	VeloxScoreboardSetText(ClockText, Model.ScoreboardClock);
	VeloxScoreboardSetOptionalText(WinnerText, Model.ScoreboardWinner);
	VeloxScoreboardSetOptionalText(SpectatorsText, Model.SpectatorNames.IsEmpty() ? FString() : FString::Printf(TEXT("Spectators: %s"), *Model.SpectatorNames));

	// Player rows.
	const int32 NumRows = Model.ScoreRows.Num();
	while (RowsBox && Rows.Num() < NumRows)
	{
		FVeloxScoreRowWidgets& Row = Rows.AddDefaulted_GetRef();
		Row.Border = MakeWidget<UBorder>();
		UHorizontalBox* Columns = Row.Border ? MakeColumnsBox(Row.Columns, UI.SmallFontSize, UI.TextColor) : nullptr;
		if (!Row.Border || !Columns || Row.Columns.Num() != VeloxScoreboardNumColumns)
		{
			Rows.Pop();
			break;
		}
		Row.Border->SetBrush(FSlateColorBrush(FLinearColor::White));
		Row.Border->SetBrushColor(FLinearColor::Transparent);
		Row.Border->SetPadding(FMargin(4.f, 2.f));
		Row.Border->SetContent(Columns);
		RowsBox->AddChildToVerticalBox(Row.Border);
	}
	NumVisibleRows = RowsBox ? FMath::Min(NumRows, Rows.Num()) : 0;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		FVeloxScoreRowWidgets& Row = Rows[Index];
		const bool bVisible = Index < NumVisibleRows;
		VeloxScoreboardSetVisibility(Row.Border, bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (!bVisible)
		{
			continue;
		}
		const VeloxScoreboardFormat::FRow& Source = Model.ScoreRows[Index];
		const FLinearColor Tint = Source.bLocal ? UI.ScoreboardLocalRowColor : FLinearColor::Transparent;
		if (!Row.Border->GetBrushColor().Equals(Tint, KINDA_SMALL_NUMBER))
		{
			Row.Border->SetBrushColor(Tint);
		}
		VeloxScoreboardSetText(Row.Columns[0], Source.Powerups.IsEmpty() ? Source.Name : FString::Printf(TEXT("%s [%s]"), *Source.Name, *Source.Powerups));
		VeloxScoreboardSetTextColor(Row.Columns[0], FLinearColor::FromSRGBColor(Source.Color));
		VeloxScoreboardSetText(Row.Columns[1], FString::FromInt(Source.Score));
		VeloxScoreboardSetText(Row.Columns[2], FString::FromInt(Source.Kills));
		VeloxScoreboardSetText(Row.Columns[3], FString::FromInt(Source.Deaths));
		VeloxScoreboardSetText(Row.Columns[4], FString::FromInt(Source.PingMs));
		VeloxScoreboardSetText(Row.Columns[5], Source.bReady ? TEXT("READY") : TEXT("-"));
		VeloxScoreboardSetTextColor(Row.Columns[5], Source.bReady ? UI.ScoreboardReadyColor : UI.TextColor);
	}

	// Ready button: only where the toggle means something (the F3 prompt on the HUD is the keyboard path).
	VeloxScoreboardSetVisibility(ReadyButton, Model.bShowReadyButton ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	VeloxScoreboardSetText(ReadyButtonLabel, Model.ReadyButtonText);
}

// ------------------------------------------------------------------------------------------------ test accessors

bool UVeloxScoreboardWidget::IsPanelVisible() const
{
	return ScoreboardPanel && ScoreboardPanel->GetVisibility() != ESlateVisibility::Collapsed;
}

FString UVeloxScoreboardWidget::GetRowText(int32 Index) const
{
	if (!Rows.IsValidIndex(Index))
	{
		return FString();
	}
	TArray<FString> Parts;
	for (const UTextBlock* Column : Rows[Index].Columns)
	{
		Parts.Add(Column ? Column->GetText().ToString() : FString());
	}
	return FString::Join(Parts, TEXT("  "));
}

bool UVeloxScoreboardWidget::IsRowLocal(int32 Index) const
{
	return Rows.IsValidIndex(Index) && Rows[Index].Border && Rows[Index].Border->GetBrushColor().Equals(UVeloxUISettings::Get().ScoreboardLocalRowColor, KINDA_SMALL_NUMBER);
}

bool UVeloxScoreboardWidget::IsReadyButtonVisible() const
{
	return ReadyButton && ReadyButton->GetVisibility() != ESlateVisibility::Collapsed;
}

FString UVeloxScoreboardWidget::GetReadyButtonLabel() const
{
	return ReadyButtonLabel ? ReadyButtonLabel->GetText().ToString() : FString();
}

FString UVeloxScoreboardWidget::DescribeBoundValues() const
{
	auto TextOf = [](const UTextBlock* Block) { return Block ? Block->GetText().ToString() : FString(TEXT("<none>")); };
	FString RowTexts;
	for (int32 Index = 0; Index < NumVisibleRows; ++Index)
	{
		RowTexts += FString::Printf(TEXT("%s[%s]"), Index > 0 ? TEXT(" ") : TEXT(""), *GetRowText(Index));
	}
	return FString::Printf(TEXT("visible=%d title='%s' limits='%s' clock=%s winner='%s' rows=%d %s spectators='%s' ready_button=%d label='%s'"),
		IsPanelVisible() ? 1 : 0, *TextOf(TitleText), *TextOf(LimitsText), *TextOf(ClockText), *TextOf(WinnerText), NumVisibleRows, *RowTexts,
		*TextOf(SpectatorsText), IsReadyButtonVisible() ? 1 : 0, *GetReadyButtonLabel());
}
