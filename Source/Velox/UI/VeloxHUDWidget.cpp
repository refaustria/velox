// Velox - in-game HUD widget.

#include "UI/VeloxHUDWidget.h"

#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/VeloxHUDFormat.h"
#include "UI/VeloxHUDViewModel.h"
#include "UI/VeloxScoreboardFormat.h"
#include "UI/VeloxUISettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxHUDWidget)

namespace
{
	/** Elements are only touched when the value changes: a Slate invalidation per element per frame would defeat the point. */
	void VeloxHUDSetText(UTextBlock* Block, const FString& Text)
	{
		if (Block && !Block->GetText().ToString().Equals(Text, ESearchCase::CaseSensitive))
		{
			Block->SetText(FText::FromString(Text));
		}
	}

	void VeloxHUDSetTextColor(UTextBlock* Block, const FLinearColor& Color)
	{
		if (Block && !Block->GetColorAndOpacity().GetSpecifiedColor().Equals(Color, KINDA_SMALL_NUMBER))
		{
			Block->SetColorAndOpacity(FSlateColor(Color));
		}
	}

	void VeloxHUDSetVisible(UWidget* Widget, bool bVisible)
	{
		if (Widget)
		{
			const ESlateVisibility Wanted = bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
			if (Widget->GetVisibility() != Wanted)
			{
				Widget->SetVisibility(Wanted);
			}
		}
	}

	void VeloxHUDSetImageColor(UImage* Image, const FLinearColor& Color)
	{
		if (Image && !Image->GetColorAndOpacity().Equals(Color, KINDA_SMALL_NUMBER))
		{
			Image->SetColorAndOpacity(Color);
		}
	}

	void VeloxHUDSetSlotSize(UWidget* Widget, const FVector2D& Size, const FVector2D& Position)
	{
		if (UCanvasPanelSlot* CanvasSlot = Widget ? Cast<UCanvasPanelSlot>(Widget->Slot) : nullptr)
		{
			if (!CanvasSlot->GetSize().Equals(Size, 0.01) || !CanvasSlot->GetPosition().Equals(Position, 0.01))
			{
				CanvasSlot->SetSize(Size);
				CanvasSlot->SetPosition(Position);
			}
		}
	}

	/** A canvas child centred on its anchor (ax, ay) with the given pivot, at an offset, auto-sized off. */
	UCanvasPanelSlot* VeloxHUDPlace(UCanvasPanel& Canvas, UWidget* Widget, const FVector2D& Anchor, const FVector2D& Alignment, const FVector2D& Position, const FVector2D& Size = FVector2D::ZeroVector)
	{
		UCanvasPanelSlot* CanvasSlot = Canvas.AddChildToCanvas(Widget);
		if (CanvasSlot)
		{
			CanvasSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y));
			CanvasSlot->SetAlignment(Alignment);
			CanvasSlot->SetPosition(Position);
			if (Size.IsZero())
			{
				CanvasSlot->SetAutoSize(true);
			}
			else
			{
				CanvasSlot->SetSize(Size);
			}
		}
		return CanvasSlot;
	}

	FLinearColor VeloxHUDWithAlpha(FLinearColor Color, float Alpha)
	{
		Color.A *= Alpha;
		return Color;
	}
}

// ----------------------------------------------------------------------------------------------------------- tree

UImage* UVeloxHUDWidget::MakeBar(FName Name)
{
	UImage* Image = MakeWidget<UImage>(Name);
	if (Image)
	{
		Image->SetBrush(FSlateColorBrush(FLinearColor::White));
		Image->SetVisibility(ESlateVisibility::Collapsed);
	}
	return Image;
}

UWidget* UVeloxHUDWidget::BuildDefaultTree()
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	UCanvasPanel* Root = MakeWidget<UCanvasPanel>(TEXT("HUDRoot"));
	if (!Root)
	{
		return nullptr;
	}
	const float M = UI.ScreenMargin;

	// Bottom left: health, armor.
	UHorizontalBox* Stats = MakeWidget<UHorizontalBox>(TEXT("StatsBox"));
	HealthText = MakeText(TEXT("HealthText"), UI.BigFontSize, UI.HealthHighColor, TEXT("0"));
	ArmorText = MakeText(TEXT("ArmorText"), UI.BigFontSize, UI.ArmorColor, TEXT("0"));
	Stats->AddChildToHorizontalBox(HealthText);
	if (UHorizontalBoxSlot* ArmorSlot = Stats->AddChildToHorizontalBox(ArmorText))
	{
		ArmorSlot->SetPadding(FMargin(UI.ScreenMargin, 0.f, 0.f, 0.f));
	}
	VeloxHUDPlace(*Root, Stats, FVector2D(0, 1), FVector2D(0, 1), FVector2D(M, -M));

	// Bottom right: ammo over the weapon name.
	UVerticalBox* AmmoBox = MakeWidget<UVerticalBox>(TEXT("AmmoBox"));
	AmmoText = MakeText(TEXT("AmmoText"), UI.BigFontSize, UI.TextColor);
	WeaponNameText = MakeText(TEXT("WeaponNameText"), UI.SmallFontSize, UI.TextColor);
	if (UVerticalBoxSlot* AmmoSlot = AmmoBox->AddChildToVerticalBox(AmmoText))
	{
		AmmoSlot->SetHorizontalAlignment(HAlign_Right);
	}
	if (UVerticalBoxSlot* NameSlot = AmmoBox->AddChildToVerticalBox(WeaponNameText))
	{
		NameSlot->SetHorizontalAlignment(HAlign_Right);
	}
	VeloxHUDPlace(*Root, AmmoBox, FVector2D(1, 1), FVector2D(1, 1), FVector2D(-M, -M));

	// Bottom centre: weapon bar. Right middle: powerups. Top right: kill feed.
	WeaponBar = MakeWidget<UHorizontalBox>(TEXT("WeaponBar"));
	VeloxHUDPlace(*Root, WeaponBar, FVector2D(0.5, 1), FVector2D(0.5, 1), FVector2D(0, -M));
	PowerupBox = MakeWidget<UVerticalBox>(TEXT("PowerupBox"));
	VeloxHUDPlace(*Root, PowerupBox, FVector2D(1, 0.5), FVector2D(1, 0.5), FVector2D(-M, 0));
	KillFeedBox = MakeWidget<UVerticalBox>(TEXT("KillFeedBox"));
	VeloxHUDPlace(*Root, KillFeedBox, FVector2D(1, 0), FVector2D(1, 0), FVector2D(-M, M));

	// Left middle: spectator item timers.
	ItemTimerBox = MakeWidget<UVerticalBox>(TEXT("ItemTimerBox"));
	VeloxHUDPlace(*Root, ItemTimerBox, FVector2D(0, 0.5), FVector2D(0, 0.5), FVector2D(M, 0));

	// Top centre: clock. Upper middle: banner. Death overlay.
	ClockText = MakeText(TEXT("ClockText"), UI.MediumFontSize, UI.TextColor);
	VeloxHUDPlace(*Root, ClockText, FVector2D(0.5, 0), FVector2D(0.5, 0), FVector2D(0, M));
	BannerText = MakeText(TEXT("BannerText"), UI.BannerFontSize, UI.BannerColor);
	VeloxHUDPlace(*Root, BannerText, FVector2D(0.5, 0.25), FVector2D(0.5, 0.5), FVector2D(0, 0));
	ReadyPromptText = MakeText(TEXT("ReadyPromptText"), UI.MediumFontSize, UI.ReadyPromptColor);
	VeloxHUDPlace(*Root, ReadyPromptText, FVector2D(0.5, 0.25), FVector2D(0.5, 0), FVector2D(0, UI.BannerFontSize));
	PickupMessageText = MakeText(TEXT("PickupMessageText"), UI.MediumFontSize, UI.PickupMessageColor);
	VeloxHUDPlace(*Root, PickupMessageText, FVector2D(0.5, 0.75), FVector2D(0.5, 0.5), FVector2D(0, 0));
	DeathTitleText = MakeText(TEXT("DeathTitleText"), UI.BannerFontSize, UI.DeathTitleColor);
	VeloxHUDPlace(*Root, DeathTitleText, FVector2D(0.5, 0.35), FVector2D(0.5, 0.5), FVector2D(0, 0));
	DeathPromptText = MakeText(TEXT("DeathPromptText"), UI.MediumFontSize, UI.TextColor);
	VeloxHUDPlace(*Root, DeathPromptText, FVector2D(0.5, 0.35), FVector2D(0.5, 0), FVector2D(0, UI.BannerFontSize));

	// Centre: crosshair bars, dot, hit marker ticks, damage indicators. Geometry is set every refresh (the size is a user setting).
	const FVector2D Centre(0.5, 0.5);
	CrosshairLeft = MakeBar(TEXT("CrosshairLeft"));
	CrosshairRight = MakeBar(TEXT("CrosshairRight"));
	CrosshairUp = MakeBar(TEXT("CrosshairUp"));
	CrosshairDown = MakeBar(TEXT("CrosshairDown"));
	CrosshairDot = MakeBar(TEXT("CrosshairDot"));
	VeloxHUDPlace(*Root, CrosshairLeft, Centre, FVector2D(1, 0.5), FVector2D::ZeroVector, FVector2D(1, 1));
	VeloxHUDPlace(*Root, CrosshairRight, Centre, FVector2D(0, 0.5), FVector2D::ZeroVector, FVector2D(1, 1));
	VeloxHUDPlace(*Root, CrosshairUp, Centre, FVector2D(0.5, 1), FVector2D::ZeroVector, FVector2D(1, 1));
	VeloxHUDPlace(*Root, CrosshairDown, Centre, FVector2D(0.5, 0), FVector2D::ZeroVector, FVector2D(1, 1));
	VeloxHUDPlace(*Root, CrosshairDot, Centre, FVector2D(0.5, 0.5), FVector2D::ZeroVector, FVector2D(1, 1));

	// Hit marker ticks: rotated rectangles on the four diagonals (45 degrees about their centre).
	HitMarkerTick0 = MakeBar(TEXT("HitMarkerTick0"));
	HitMarkerTick1 = MakeBar(TEXT("HitMarkerTick1"));
	HitMarkerTick2 = MakeBar(TEXT("HitMarkerTick2"));
	HitMarkerTick3 = MakeBar(TEXT("HitMarkerTick3"));
	for (UImage* Tick : { HitMarkerTick0.Get(), HitMarkerTick1.Get(), HitMarkerTick2.Get(), HitMarkerTick3.Get() })
	{
		VeloxHUDPlace(*Root, Tick, Centre, FVector2D(0.5, 0.5), FVector2D::ZeroVector, FVector2D(1, 1));
	}
	HitMarkerTick0->SetRenderTransformAngle(-45.f);
	HitMarkerTick1->SetRenderTransformAngle(45.f);
	HitMarkerTick2->SetRenderTransformAngle(-45.f);
	HitMarkerTick3->SetRenderTransformAngle(45.f);

	DamageFront = MakeBar(TEXT("DamageFront"));
	DamageRight = MakeBar(TEXT("DamageRight"));
	DamageBack = MakeBar(TEXT("DamageBack"));
	DamageLeft = MakeBar(TEXT("DamageLeft"));
	const float R = UI.DamageIndicatorRadius;
	const FVector2D Wide = UI.DamageIndicatorBarSize;
	const FVector2D Tall(Wide.Y, Wide.X);
	VeloxHUDPlace(*Root, DamageFront, Centre, FVector2D(0.5, 1), FVector2D(0, -R), Wide);
	VeloxHUDPlace(*Root, DamageRight, Centre, FVector2D(0, 0.5), FVector2D(R, 0), Tall);
	VeloxHUDPlace(*Root, DamageBack, Centre, FVector2D(0.5, 0), FVector2D(0, R), Wide);
	VeloxHUDPlace(*Root, DamageLeft, Centre, FVector2D(1, 0.5), FVector2D(-R, 0), Tall);
	return Root;
}

void UVeloxHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// The HUD never takes the mouse (the root and, by inheritance, everything under it is hit-test invisible).
	SetVisibility(ESlateVisibility::HitTestInvisible);
	RefreshFromViewModel();
}

void UVeloxHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshFromViewModel();
}

void UVeloxHUDWidget::SetViewModel(UVeloxHUDViewModel* InViewModel)
{
	ViewModel = InViewModel;
}

// -------------------------------------------------------------------------------------------------------- refresh

void UVeloxHUDWidget::RefreshFromViewModel()
{
	if (!ViewModel)
	{
		return;
	}
	const UVeloxHUDViewModel& Model = *ViewModel;
	RefreshStats(Model);
	RefreshWeaponBar(Model);
	RefreshPowerups(Model);
	RefreshFeed(Model);
	RefreshCrosshair(Model);
	RefreshHitMarker(Model);
	RefreshDamageIndicators(Model);
	RefreshMatch(Model);
	RefreshItemTimers(Model);
}

void UVeloxHUDWidget::RefreshStats(const UVeloxHUDViewModel& Model)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	VeloxHUDSetText(HealthText, FString::FromInt(Model.Health));
	switch (VeloxHUDFormat::HealthBand(Model.Health, UI.HealthHighMin, UI.HealthMidMin))
	{
	case VeloxHUDFormat::EHealthBand::High: VeloxHUDSetTextColor(HealthText, UI.HealthHighColor); break;
	case VeloxHUDFormat::EHealthBand::Mid: VeloxHUDSetTextColor(HealthText, UI.HealthMidColor); break;
	case VeloxHUDFormat::EHealthBand::Low: VeloxHUDSetTextColor(HealthText, UI.HealthLowColor); break;
	}
	VeloxHUDSetText(ArmorText, FString::FromInt(Model.Armor));

	const bool bShowWeapon = Model.bHasPlayer && Model.bAlive && Model.CurrentWeaponIndex != INDEX_NONE;
	VeloxHUDSetVisible(AmmoText, bShowWeapon && Model.Ammo >= 0);
	VeloxHUDSetVisible(WeaponNameText, bShowWeapon);
	VeloxHUDSetText(AmmoText, Model.Ammo >= 0 ? FString::FromInt(Model.Ammo) : FString());
	VeloxHUDSetTextColor(AmmoText, Model.Ammo >= 0 && Model.Ammo <= UI.LowAmmoThreshold ? UI.HealthLowColor : UI.TextColor);
	VeloxHUDSetText(WeaponNameText, Model.WeaponName);
}

void UVeloxHUDWidget::RefreshWeaponBar(const UVeloxHUDViewModel& Model)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const int32 NumRows = Model.bAlive ? Model.WeaponRows.Num() : 0;
	while (WeaponBar && WeaponCells.Num() < NumRows)
	{
		FVeloxHUDWeaponCellWidgets& Cell = WeaponCells.AddDefaulted_GetRef();
		Cell.Border = MakeWidget<UBorder>();
		Cell.Label = MakeText(NAME_None, UI.SmallFontSize, UI.TextColor);
		if (!Cell.Border || !Cell.Label)
		{
			WeaponCells.Pop();
			break;
		}
		Cell.Border->SetBrush(FSlateColorBrush(FLinearColor::White));
		Cell.Border->SetPadding(FMargin(8.f, 2.f));
		Cell.Border->SetContent(Cell.Label);
		if (UHorizontalBoxSlot* BoxSlot = WeaponBar->AddChildToHorizontalBox(Cell.Border))
		{
			BoxSlot->SetPadding(FMargin(2.f, 0.f));
		}
	}
	NumVisibleWeaponCells = WeaponBar ? FMath::Min(NumRows, WeaponCells.Num()) : 0;
	for (int32 Index = 0; Index < WeaponCells.Num(); ++Index)
	{
		FVeloxHUDWeaponCellWidgets& Cell = WeaponCells[Index];
		const bool bVisible = Index < NumVisibleWeaponCells;
		VeloxHUDSetVisible(Cell.Border, bVisible);
		if (!bVisible)
		{
			continue;
		}
		const VeloxHUDFormat::FWeaponBarRow& Row = Model.WeaponRows[Index];
		VeloxHUDSetText(Cell.Label, Row.Label);
		VeloxHUDSetTextColor(Cell.Label, Row.Ammo >= 0 && Row.Ammo <= UI.LowAmmoThreshold ? UI.HealthLowColor : UI.TextColor);
		const FLinearColor Tint = Row.bCurrent ? UI.WeaponCurrentColor : UI.WeaponOtherColor;
		if (!Cell.Border->GetBrushColor().Equals(Tint, KINDA_SMALL_NUMBER))
		{
			Cell.Border->SetBrushColor(Tint);
		}
	}
}

void UVeloxHUDWidget::RefreshPowerups(const UVeloxHUDViewModel& Model)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const int32 NumRows = Model.Powerups.Num();
	while (PowerupBox && PowerupRows.Num() < NumRows)
	{
		UTextBlock* Row = MakeText(NAME_None, UI.MediumFontSize, UI.TextColor);
		if (!Row)
		{
			break;
		}
		PowerupRows.Add(Row);
		if (UVerticalBoxSlot* BoxSlot = PowerupBox->AddChildToVerticalBox(Row))
		{
			BoxSlot->SetHorizontalAlignment(HAlign_Right);
		}
	}
	NumVisiblePowerupRows = PowerupBox ? FMath::Min(NumRows, PowerupRows.Num()) : 0;
	for (int32 Index = 0; Index < PowerupRows.Num(); ++Index)
	{
		const bool bVisible = Index < NumVisiblePowerupRows;
		VeloxHUDSetVisible(PowerupRows[Index], bVisible);
		if (bVisible)
		{
			// The remaining seconds, not a ratio (ADR 0004).
			VeloxHUDSetText(PowerupRows[Index], FString::Printf(TEXT("%s %d"), *Model.Powerups[Index].Name, Model.Powerups[Index].Seconds));
		}
	}
}

void UVeloxHUDWidget::RefreshFeed(const UVeloxHUDViewModel& Model)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const int32 NumLines = Model.Feed.Num();
	while (KillFeedBox && FeedRows.Num() < NumLines)
	{
		FVeloxHUDFeedRowWidgets& Row = FeedRows.AddDefaulted_GetRef();
		Row.Row = MakeWidget<UHorizontalBox>();
		Row.Killer = MakeText(NAME_None, UI.SmallFontSize, UI.TextColor);
		Row.Middle = MakeText(NAME_None, UI.SmallFontSize, UI.TextColor);
		Row.Victim = MakeText(NAME_None, UI.SmallFontSize, UI.TextColor);
		if (!Row.Row || !Row.Killer || !Row.Middle || !Row.Victim)
		{
			FeedRows.Pop();
			break;
		}
		Row.Row->AddChildToHorizontalBox(Row.Killer);
		Row.Row->AddChildToHorizontalBox(Row.Middle);
		Row.Row->AddChildToHorizontalBox(Row.Victim);
		if (UVerticalBoxSlot* BoxSlot = KillFeedBox->AddChildToVerticalBox(Row.Row))
		{
			BoxSlot->SetHorizontalAlignment(HAlign_Right);
		}
	}
	NumVisibleFeedRows = KillFeedBox ? FMath::Min(NumLines, FeedRows.Num()) : 0;
	for (int32 Index = 0; Index < FeedRows.Num(); ++Index)
	{
		FVeloxHUDFeedRowWidgets& Row = FeedRows[Index];
		const bool bVisible = Index < NumVisibleFeedRows;
		VeloxHUDSetVisible(Row.Row, bVisible);
		if (bVisible)
		{
			const FVeloxHUDFeedLine& Line = Model.Feed[Index];
			VeloxHUDSetText(Row.Killer, Line.Killer);
			VeloxHUDSetText(Row.Middle, Line.Middle);
			VeloxHUDSetText(Row.Victim, Line.Victim);
			VeloxHUDSetTextColor(Row.Killer, FLinearColor::FromSRGBColor(Line.KillerColor));
			VeloxHUDSetTextColor(Row.Victim, FLinearColor::FromSRGBColor(Line.VictimColor));
		}
	}
}

void UVeloxHUDWidget::RefreshCrosshair(const UVeloxHUDViewModel& Model)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const bool bShow = Model.bHasPlayer && Model.bAlive;
	const bool bBars = bShow && Model.CrosshairStyle != EVeloxCrosshairStyle::Dot;
	const bool bDot = bShow && Model.CrosshairStyle != EVeloxCrosshairStyle::Cross;
	const float Length = FMath::Max(Model.CrosshairSize, 1.f);
	const float Gap = UI.CrosshairGap;
	const float Thickness = UI.CrosshairThickness;
	const FLinearColor Color = FLinearColor::FromSRGBColor(Model.CrosshairColor);

	const FVector2D Horizontal(Length, Thickness);
	const FVector2D Vertical(Thickness, Length);
	VeloxHUDSetSlotSize(CrosshairLeft, Horizontal, FVector2D(-Gap, 0));
	VeloxHUDSetSlotSize(CrosshairRight, Horizontal, FVector2D(Gap, 0));
	VeloxHUDSetSlotSize(CrosshairUp, Vertical, FVector2D(0, -Gap));
	VeloxHUDSetSlotSize(CrosshairDown, Vertical, FVector2D(0, Gap));
	VeloxHUDSetSlotSize(CrosshairDot, FVector2D(Thickness + 2.f, Thickness + 2.f), FVector2D::ZeroVector);
	for (UImage* Bar : { CrosshairLeft.Get(), CrosshairRight.Get(), CrosshairUp.Get(), CrosshairDown.Get() })
	{
		VeloxHUDSetVisible(Bar, bBars);
		VeloxHUDSetImageColor(Bar, Color);
	}
	VeloxHUDSetVisible(CrosshairDot, bDot);
	VeloxHUDSetImageColor(CrosshairDot, Color);
}

void UVeloxHUDWidget::RefreshHitMarker(const UVeloxHUDViewModel& Model)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const bool bShow = Model.HitMarkerAlpha > 0.f;
	const float Arm = Model.HitMarkerSize * (Model.bHitMarkerKill ? UI.HitMarkerKillScale : 1.f);
	const float Gap = Model.HitMarkerSize * UI.HitMarkerGapFraction;
	// Tick centre on the diagonal at (gap + arm / 2) from the crosshair centre.
	const float Offset = (Gap + Arm * 0.5f) * UE_INV_SQRT_2;
	const FLinearColor Color = VeloxHUDWithAlpha(Model.bHitMarkerKill ? UI.HitMarkerKillColor : UI.HitMarkerColor, Model.HitMarkerAlpha);
	const FVector2D Size(Arm, UI.HitMarkerThickness);
	const FVector2D Signs[4] = { FVector2D(1, -1), FVector2D(1, 1), FVector2D(-1, 1), FVector2D(-1, -1) };
	UImage* Ticks[4] = { HitMarkerTick0, HitMarkerTick1, HitMarkerTick2, HitMarkerTick3 };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		VeloxHUDSetVisible(Ticks[Index], bShow);
		if (bShow)
		{
			VeloxHUDSetSlotSize(Ticks[Index], Size, Signs[Index] * Offset);
			VeloxHUDSetImageColor(Ticks[Index], Color);
		}
	}
}

void UVeloxHUDWidget::RefreshDamageIndicators(const UVeloxHUDViewModel& Model)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	UImage* Bars[4] = { DamageFront, DamageRight, DamageBack, DamageLeft };
	for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
	{
		const float Intensity = Model.DamageIntensity[Quadrant];
		VeloxHUDSetVisible(Bars[Quadrant], Intensity > 0.f);
		if (Intensity > 0.f)
		{
			VeloxHUDSetImageColor(Bars[Quadrant], VeloxHUDWithAlpha(UI.DamageIndicatorColor, Intensity));
		}
	}
}

void UVeloxHUDWidget::RefreshMatch(const UVeloxHUDViewModel& Model)
{
	VeloxHUDSetText(ClockText, Model.ClockText);
	VeloxHUDSetText(BannerText, Model.BannerText);
	VeloxHUDSetVisible(DeathTitleText, Model.bShowDeath);
	VeloxHUDSetVisible(DeathPromptText, Model.bShowDeath);
	VeloxHUDSetText(DeathTitleText, Model.DeathTitle);
	VeloxHUDSetText(DeathPromptText, Model.DeathPrompt);

	// Ready prompt (hidden while the player is dead: the respawn prompt owns the screen) and the pickup message.
	VeloxHUDSetText(ReadyPromptText, Model.ReadyPrompt);
	VeloxHUDSetVisible(ReadyPromptText, !Model.ReadyPrompt.IsEmpty() && !Model.bShowDeath);
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	VeloxHUDSetText(PickupMessageText, Model.PickupMessage);
	VeloxHUDSetVisible(PickupMessageText, Model.PickupMessageAlpha > 0.f && !Model.PickupMessage.IsEmpty());
	VeloxHUDSetTextColor(PickupMessageText, VeloxHUDWithAlpha(UI.PickupMessageColor, Model.PickupMessageAlpha));
}

void UVeloxHUDWidget::RefreshItemTimers(const UVeloxHUDViewModel& Model)
{
	const UVeloxUISettings& UI = UVeloxUISettings::Get();
	const int32 NumRows = Model.bShowItemTimers ? Model.ItemTimers.Num() : 0;
	while (ItemTimerBox && ItemTimerRows.Num() < NumRows)
	{
		UTextBlock* Row = MakeText(NAME_None, UI.SmallFontSize, UI.ItemTimerDownColor);
		if (!Row)
		{
			break;
		}
		ItemTimerRows.Add(Row);
		ItemTimerBox->AddChildToVerticalBox(Row);
	}
	NumVisibleItemTimerRows = ItemTimerBox ? FMath::Min(NumRows, ItemTimerRows.Num()) : 0;
	for (int32 Index = 0; Index < ItemTimerRows.Num(); ++Index)
	{
		const bool bVisible = Index < NumVisibleItemTimerRows;
		VeloxHUDSetVisible(ItemTimerRows[Index], bVisible);
		if (bVisible)
		{
			const VeloxScoreboardFormat::FItemTimer& Timer = Model.ItemTimers[Index];
			VeloxHUDSetText(ItemTimerRows[Index], Timer.Text);
			VeloxHUDSetTextColor(ItemTimerRows[Index], Timer.bAvailable ? UI.ItemTimerUpColor : UI.ItemTimerDownColor);
		}
	}
}

// ------------------------------------------------------------------------------------------------ test accessors

FString UVeloxHUDWidget::DescribeBoundValues() const
{
	auto TextOf = [](const UTextBlock* Block) { return Block ? Block->GetText().ToString() : FString(TEXT("<none>")); };
	return FString::Printf(TEXT("health=%s armor=%s ammo=%s weapon=%s clock=%s banner=%s weapon_cells=%d powerups=%d feed=%d ready_prompt='%s' pickup='%s' item_timers=%d crosshair_bars=%d dot=%d"),
		*TextOf(HealthText), *TextOf(ArmorText), *TextOf(AmmoText), *TextOf(WeaponNameText), *TextOf(ClockText), *TextOf(BannerText),
		NumVisibleWeaponCells, NumVisiblePowerupRows, NumVisibleFeedRows, *TextOf(ReadyPromptText), *TextOf(PickupMessageText), NumVisibleItemTimerRows,
		CrosshairLeft && CrosshairLeft->GetVisibility() != ESlateVisibility::Collapsed ? 1 : 0, CrosshairDot && CrosshairDot->GetVisibility() != ESlateVisibility::Collapsed ? 1 : 0);
}

int32 UVeloxHUDWidget::GetNumVisibleFeedRows() const
{
	return NumVisibleFeedRows;
}

FString UVeloxHUDWidget::GetFeedRowText(int32 Index) const
{
	if (!FeedRows.IsValidIndex(Index))
	{
		return FString();
	}
	const FVeloxHUDFeedRowWidgets& Row = FeedRows[Index];
	return Row.Killer->GetText().ToString() + Row.Middle->GetText().ToString() + Row.Victim->GetText().ToString();
}

int32 UVeloxHUDWidget::GetNumVisibleWeaponCells() const
{
	return NumVisibleWeaponCells;
}

FString UVeloxHUDWidget::GetWeaponCellText(int32 Index) const
{
	return WeaponCells.IsValidIndex(Index) ? WeaponCells[Index].Label->GetText().ToString() : FString();
}

bool UVeloxHUDWidget::IsWeaponCellCurrent(int32 Index) const
{
	return WeaponCells.IsValidIndex(Index) && WeaponCells[Index].Border->GetBrushColor().Equals(UVeloxUISettings::Get().WeaponCurrentColor, KINDA_SMALL_NUMBER);
}

int32 UVeloxHUDWidget::GetNumVisiblePowerupRows() const
{
	return NumVisiblePowerupRows;
}

FString UVeloxHUDWidget::GetPowerupRowText(int32 Index) const
{
	return PowerupRows.IsValidIndex(Index) ? PowerupRows[Index]->GetText().ToString() : FString();
}

bool UVeloxHUDWidget::IsDamageIndicatorVisible(int32 Quadrant) const
{
	const UImage* Bars[4] = { DamageFront, DamageRight, DamageBack, DamageLeft };
	return Quadrant >= 0 && Quadrant < 4 && Bars[Quadrant] && Bars[Quadrant]->GetVisibility() != ESlateVisibility::Collapsed;
}

FString UVeloxHUDWidget::GetItemTimerRowText(int32 Index) const
{
	return ItemTimerRows.IsValidIndex(Index) ? ItemTimerRows[Index]->GetText().ToString() : FString();
}

bool UVeloxHUDWidget::IsPickupMessageVisible() const
{
	return PickupMessageText && PickupMessageText->GetVisibility() != ESlateVisibility::Collapsed;
}

bool UVeloxHUDWidget::IsHitMarkerVisible() const
{
	return HitMarkerTick0 && HitMarkerTick0->GetVisibility() != ESlateVisibility::Collapsed;
}
