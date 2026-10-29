// Velox - main menu (ADR 0004 section 7, task M3-10).

#include "UI/VeloxMainMenuWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Core/VeloxGameInstance.h"
#include "Core/VeloxLog.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/VeloxGameUserSettings.h"
#include "UI/VeloxSettingsWidget.h"
#include "UI/VeloxUISettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxMainMenuWidget)

UVeloxMainMenuWidget::UVeloxMainMenuWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SettingsWidgetClass = UVeloxSettingsWidget::StaticClass();
	SetIsFocusable(true);
}

UWidget* UVeloxMainMenuWidget::BuildDefaultTree()
{
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

	UVerticalBox* Column = MakeWidget<UVerticalBox>(TEXT("Column"));
	if (UCanvasPanelSlot* ColumnSlot = Root->AddChildToCanvas(Column))
	{
		ColumnSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ColumnSlot->SetAlignment(FVector2D(0.5, 0.5));
		ColumnSlot->SetAutoSize(true);
		ColumnSlot->SetPosition(FVector2D::ZeroVector);
	}

	const FMargin RowPadding(0.f, UI.MenuRowPadding);
	auto AddToColumn = [&](UWidget* Child, const FMargin& Padding)
	{
		if (UVerticalBoxSlot* ChildSlot = Column->AddChildToVerticalBox(Child))
		{
			ChildSlot->SetPadding(Padding);
			ChildSlot->SetHorizontalAlignment(HAlign_Center);
		}
	};

	AddToColumn(MakeText(TEXT("Title"), UI.MenuTitleFontSize, UI.MenuTitleColor, TEXT("VELOX")), FMargin(0.f, 0.f, 0.f, UI.MenuRowPadding * 4.f));

	AddressBox = MakeWidget<UEditableTextBox>(TEXT("AddressBox"));
	AddressBox->SetHintText(FText::FromString(TEXT("server address, e.g. 127.0.0.1:7777")));
	USizeBox* AddressSize = MakeWidget<USizeBox>(NAME_None);
	AddressSize->SetWidthOverride(UI.MenuButtonWidth);
	AddressSize->AddChild(AddressBox);
	AddToColumn(AddressSize, RowPadding);

	auto AddButton = [&](TObjectPtr<UButton>& Out, const TCHAR* Name, const TCHAR* Label)
	{
		Out = MakeButton(Name, Label);
		USizeBox* Size = MakeWidget<USizeBox>(NAME_None);
		Size->SetWidthOverride(UI.MenuButtonWidth);
		Size->AddChild(Out);
		AddToColumn(Size, RowPadding);
	};
	AddButton(ConnectButton, TEXT("ConnectButton"), TEXT("Connect"));
	AddButton(HostButton, TEXT("HostButton"), TEXT("Host local game"));
	AddButton(SettingsButton, TEXT("SettingsButton"), TEXT("Settings"));
	AddButton(QuitButton, TEXT("QuitButton"), TEXT("Quit"));

	StatusText = MakeText(TEXT("StatusText"), UI.MenuFontSize, UI.MenuTextColor, FString());
	StatusText->SetJustification(ETextJustify::Center);
	StatusText->SetAutoWrapText(true);
	USizeBox* StatusSize = MakeWidget<USizeBox>(NAME_None);
	StatusSize->SetWidthOverride(UI.MenuButtonWidth * 2.f);
	StatusSize->AddChild(StatusText);
	AddToColumn(StatusSize, FMargin(0.f, UI.MenuRowPadding * 3.f, 0.f, 0.f));
	return Root;
}

void UVeloxMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ConnectButton)
	{
		ConnectButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConnect);
	}
	if (HostButton)
	{
		HostButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleHost);
	}
	if (SettingsButton)
	{
		SettingsButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleSettings);
	}
	if (QuitButton)
	{
		QuitButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleQuit);
	}
	if (AddressBox)
	{
		AddressBox->OnTextCommitted.AddUniqueDynamic(this, &ThisClass::HandleAddressCommitted);
		AddressBox->SetText(FText::FromString(UVeloxGameUserSettings::GetOrDefault().LastConnectAddress));
	}

	// The reason of the last failed connect / kick, left by the game instance before the engine brought us back here.
	if (UVeloxGameInstance* GameInstance = Cast<UVeloxGameInstance>(GetWorld() ? GetWorld()->GetGameInstance() : nullptr))
	{
		const FString Message = GameInstance->ConsumeMenuMessage();
		if (!Message.IsEmpty())
		{
			SetStatus(Message, true);
		}
	}
}

void UVeloxMainMenuWidget::SetStatus(const FString& Text, bool bError)
{
	if (StatusText)
	{
		const UVeloxUISettings& UI = UVeloxUISettings::Get();
		StatusText->SetText(FText::FromString(Text));
		StatusText->SetColorAndOpacity(FSlateColor(bError ? UI.MenuErrorColor : UI.MenuTextColor));
	}
}

FString UVeloxMainMenuWidget::GetStatusString() const
{
	return StatusText ? StatusText->GetText().ToString() : FString();
}

bool UVeloxMainMenuWidget::Connect()
{
	UVeloxGameInstance* GameInstance = Cast<UVeloxGameInstance>(GetWorld() ? GetWorld()->GetGameInstance() : nullptr);
	if (!GameInstance)
	{
		SetStatus(TEXT("No game instance."), true);
		return false;
	}
	FString Error;
	if (!GameInstance->ConnectToServer(AddressBox ? AddressBox->GetText().ToString() : FString(), Error))
	{
		SetStatus(Error, true);
		return false;
	}
	SetStatus(FString::Printf(TEXT("Connecting to %s ..."), *GameInstance->GetLastConnectTarget()), false);
	return true;
}

bool UVeloxMainMenuWidget::HostLocal()
{
	UVeloxGameInstance* GameInstance = Cast<UVeloxGameInstance>(GetWorld() ? GetWorld()->GetGameInstance() : nullptr);
	FString Error;
	if (!GameInstance || !GameInstance->HostLocal(Error))
	{
		SetStatus(Error.IsEmpty() ? FString(TEXT("No game instance.")) : Error, true);
		return false;
	}
	SetStatus(TEXT("Starting a local game ..."), false);
	return true;
}

void UVeloxMainMenuWidget::OpenSettings()
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

void UVeloxMainMenuWidget::HandleSettingsClosed()
{
	SettingsWidget = nullptr;
	SetKeyboardFocus();
}

void UVeloxMainMenuWidget::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UVeloxMainMenuWidget::HandleConnect()
{
	Connect();
}

void UVeloxMainMenuWidget::HandleHost()
{
	HostLocal();
}

void UVeloxMainMenuWidget::HandleSettings()
{
	OpenSettings();
}

void UVeloxMainMenuWidget::HandleQuit()
{
	QuitGame();
}

void UVeloxMainMenuWidget::HandleAddressCommitted(const FText& Text, ETextCommit::Type CommitMethod)
{
	if (CommitMethod == ETextCommit::OnEnter)
	{
		Connect();
	}
}
