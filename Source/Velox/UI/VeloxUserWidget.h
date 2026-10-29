// Velox - base of every C++ UMG screen (ADR 0004 section 7).
//
// Pattern: a subclass builds its widget tree programmatically in BuildDefaultTree() and exposes its elements as
// UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional)) pointers. RebuildWidget() builds the default tree only when
// the widget has none, so a Widget Blueprint subclass (WBP_*) that supplies its own tree replaces the look without a code
// change: the engine binds the BindWidgetOptional names in Initialize() and the widget fills whatever it finds. Every
// element pointer can therefore be null and every use is guarded.
//
// Engine facts (UE 5.8 UserWidget.cpp, verified): UUserWidget::Initialize() creates the WidgetTree and runs
// NativeOnInitialized() only when a player context is set (or a Blueprint class opts in), so nothing essential happens in
// NativeOnInitialized; RebuildWidget() calls Initialize() first and then wraps WidgetTree->RootWidget (an SSpacer when
// there is none). Both run without a renderer (NullRHI): they create Slate widgets, never measure or paint.

#pragma once

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"

#include "VeloxUserWidget.generated.h"

class AVeloxPlayerController;
class UCanvasPanel;
class UTextBlock;
class UWidget;

UCLASS(Abstract, Blueprintable)
class VELOX_API UVeloxUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The local player controller that owns this widget (null for a widget created without a player context). */
	UFUNCTION(BlueprintPure, Category = "Velox|UI")
	AVeloxPlayerController* GetVeloxPlayerController() const;

protected:
	//~UUserWidget interface
	virtual TSharedRef<SWidget> RebuildWidget() override;
	//~End of UUserWidget interface

	/** Builds the root of the default tree (called only when the widget has no tree). Null leaves the engine's empty root. */
	virtual UWidget* BuildDefaultTree() { return nullptr; }

	/** Constructs a widget of WidgetClass in this widget's tree. */
	template <typename WidgetT>
	WidgetT* MakeWidget(FName Name = NAME_None)
	{
		return WidgetTree ? WidgetTree->ConstructWidget<WidgetT>(WidgetT::StaticClass(), Name) : nullptr;
	}

	/** A text block with the engine's default font at Size, Color, and the text Text. */
	UTextBlock* MakeText(FName Name, int32 Size, const FLinearColor& Color, const FString& Text = FString());
};
