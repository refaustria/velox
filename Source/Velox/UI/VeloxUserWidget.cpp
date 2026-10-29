// Velox - base of every C++ UMG screen.

#include "UI/VeloxUserWidget.h"

#include "Components/TextBlock.h"
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
