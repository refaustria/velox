// Velox - base of every C++ UMG screen.

#include "UI/VeloxUserWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UI/VeloxUISettings.h"
#include "GameModes/VeloxPlayerController.h"
#include "Styling/CoreStyle.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxUserWidget)

AVeloxPlayerController* UVeloxUserWidget::GetVeloxPlayerController() const
{
	return Cast<AVeloxPlayerController>(GetOwningPlayer());
}

TSharedRef<SWidget> UVeloxUserWidget::RebuildWidget()
{
	// Initialize() creates the WidgetTree (and binds a Blueprint's named widgets); it is idempotent, and the engine's own
	// RebuildWidget would call it too, but the tree must exist before we look at it.
	Initialize();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		WidgetTree->RootWidget = BuildDefaultTree();
	}
	return Super::RebuildWidget();
}

UButton* UVeloxUserWidget::MakeButton(FName Name, const FString& Label)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	UButton* Button = MakeWidget<UButton>(Name);
	if (!Button)
	{
		return nullptr;
	}
	FButtonStyle Style;
	Style.SetNormal(FSlateColorBrush(UI.MenuButtonColor));
	Style.SetHovered(FSlateColorBrush(UI.MenuButtonHoverColor));
	Style.SetPressed(FSlateColorBrush(UI.MenuButtonColor));
	Style.SetDisabled(FSlateColorBrush(UI.MenuPanelColor));
	Style.SetNormalPadding(FMargin(UI.MenuRowPadding * 2.f, UI.MenuRowPadding));
	Style.SetPressedPadding(FMargin(UI.MenuRowPadding * 2.f, UI.MenuRowPadding));
	Button->SetStyle(Style);
	if (UTextBlock* Text = MakeText(FName(*(Name.ToString() + TEXT("_Label"))), UI.MenuFontSize, UI.MenuTextColor, Label))
	{
		Text->SetJustification(ETextJustify::Center);
		Button->AddChild(Text);
	}
	return Button;
}

UTextBlock* UVeloxUserWidget::GetButtonLabel(const UButton* Button)
{
	return Button ? Cast<UTextBlock>(Button->GetContent()) : nullptr;
}

UTextBlock* UVeloxUserWidget::MakeText(FName Name, int32 Size, const FLinearColor& Color, const FString& Text)
{
	UTextBlock* Block = MakeWidget<UTextBlock>(Name);
	if (Block)
	{
		Block->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
		Block->SetColorAndOpacity(FSlateColor(Color));
		Block->SetText(FText::FromString(Text));
	}
	return Block;
}
