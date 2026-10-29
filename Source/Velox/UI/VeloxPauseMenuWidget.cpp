// Velox - in-game pause menu (ADR 0004 section 7, task M3-10).

#include "UI/VeloxPauseMenuWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/VeloxGameInstance.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "UI/VeloxSettingsWidget.h"
#include "UI/VeloxUISettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxPauseMenuWidget)

UVeloxPauseMenuWidget::UVeloxPauseMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SettingsWidgetClass = UVeloxSettingsWidget::StaticClass();
	SetIsFocusable(true);
}

UWidget* UVeloxPauseMenuWidget::BuildDefaultTree()
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();

	UCanvasPanel* Root = MakeWidget<UCanvasPanel>(TEXT("Root"));
	UImage* Dim = MakeWidget<UImage>(TEXT("Dim"));
	Dim->SetBrush(FSlateColorBrush(FLinearColor::White));
	Dim->SetColorAndOpacity(UI.PauseDimColor);
	if (UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(Dim))
	{
		DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		DimSlot->SetOffsets(FMargin(0.f));
	}

	UVerticalBox* Column = MakeWidget<UVerticalBox>(TEXT("Column"));
	if (UCanvasPanelSlot* ColumnSlot = Root->AddChildToCanvas(Column))
	{
		ColumnSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ColumnSlot->SetAlignment(FVector2D(0.5, 0.5));
		ColumnSlot->SetAutoSize(true);
		ColumnSlot->SetPosition(FVector2D::ZeroVector);
	}
	auto AddToColumn = [&](UWidget* Child, const FMargin& Padding)
	{
		if (UVerticalBoxSlot* ChildSlot = Column->AddChildToVerticalBox(Child))
		{
			ChildSlot->SetPadding(Padding);
			ChildSlot->SetHorizontalAlignment(HAlign_Center);
		}
	};
	AddToColumn(MakeText(TEXT("Title"), UI.MenuHeadingFontSize + 10, UI.MenuTitleColor, TEXT("MENU")), FMargin(0.f, 0.f, 0.f, UI.MenuRowPadding * 3.f));
	auto AddButton = [&](TObjectPtr<UButton>& Out, const TCHAR* Name, const TCHAR* Label)
	{
		Out = MakeButton(Name, Label);
		USizeBox* Size = MakeWidget<USizeBox>(NAME_None);
		Size->SetWidthOverride(UI.MenuButtonWidth);
		Size->AddChild(Out);
		AddToColumn(Size, FMargin(0.f, UI.MenuRowPadding));
	};
	AddButton(ResumeButton, TEXT("ResumeButton"), TEXT("Resume"));
	AddButton(SettingsButton, TEXT("SettingsButton"), TEXT("Settings"));
	AddButton(DisconnectButton, TEXT("DisconnectButton"), TEXT("Disconnect"));
	StatusText = MakeText(TEXT("StatusText"), UI.MenuFontSize, UI.MenuTextColor, FString());
	AddToColumn(StatusText, FMargin(0.f, UI.MenuRowPadding * 2.f, 0.f, 0.f));
	return Root;
}

void UVeloxPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ResumeButton)
	{
		ResumeButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleResume);
	}
	if (SettingsButton)
	{
		SettingsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSettings);
	}
	if (DisconnectButton)
	{
		DisconnectButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleDisconnect);
	}
}

FReply UVeloxPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		// The settings screen handles its own Escape (it has focus while open); this one closes the menu.
		if (!SettingsWidget)
		{
			Resume();
		}
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UVeloxPauseMenuWidget::Resume()
{
	if (SettingsWidget)
	{
		SettingsWidget->Close();
	}
	OnResume.Broadcast();
}

void UVeloxPauseMenuWidget::OpenSettings()
{
	if (SettingsWidget || !SettingsWidgetClass)
	{
		return;
	}
	SettingsWidget = CreateWidget<UVeloxSettingsWidget>(GetOwningPlayer(), SettingsWidgetClass);
	if (!SettingsWidget)
	{
		return;
	}
	SettingsWidget->OnClosed.AddUObject(this, &ThisClass::HandleSettingsClosed);
	SettingsWidget->AddToViewport(UVeloxUISettings::Get().SettingsZOrder);
	SettingsWidget->SetKeyboardFocus();
}

void UVeloxPauseMenuWidget::HandleSettingsClosed()
{
	SettingsWidget = nullptr;
	SetKeyboardFocus();
}

bool UVeloxPauseMenuWidget::Disconnect()
{
	UVeloxGameInstance* GameInstance = Cast<UVeloxGameInstance>(GetWorld() ? GetWorld()->GetGameInstance() : nullptr);
	if (!GameInstance)
	{
		return false;
	}
	return GameInstance->ReturnToFrontEnd(FString());
}

void UVeloxPauseMenuWidget::HandleResume()
{
	Resume();
}

void UVeloxPauseMenuWidget::HandleSettings()
{
	OpenSettings();
}

void UVeloxPauseMenuWidget::HandleDisconnect()
{
	Disconnect();
}
