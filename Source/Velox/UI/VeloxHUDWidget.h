// Velox - in-game HUD widget (ADR 0004 section 7): health, armor, ammo, weapon bar, powerup timers, crosshair, hit marker,
// damage direction indicators, match clock and phase banner, kill feed, death overlay. It builds its tree in
// BuildDefaultTree() (no textures, no materials; solid-colour brushes only), reads UVeloxHUDViewModel in NativeTick and
// never touches a gameplay object. Every element is UPROPERTY(BindWidgetOptional): a WBP_HUD subclass that supplies its
// own tree gets the same bindings (missing elements are skipped), AVeloxHUD::HUDWidgetClass selects the class.

#pragma once

#include "UI/VeloxUserWidget.h"

#include "VeloxHUDWidget.generated.h"

class UBorder;
class UHorizontalBox;
class UImage;
class UVerticalBox;
class UVeloxHUDViewModel;

/** One kill feed line: three text blocks (coloured killer, connector, coloured victim). */
USTRUCT()
struct FVeloxHUDFeedRowWidgets
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UHorizontalBox> Row;

	UPROPERTY()
	TObjectPtr<UTextBlock> Killer;

	UPROPERTY()
	TObjectPtr<UTextBlock> Middle;

	UPROPERTY()
	TObjectPtr<UTextBlock> Victim;
};

/** One weapon bar cell: a tinted border around the label. */
USTRUCT()
struct FVeloxHUDWeaponCellWidgets
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UBorder> Border;

	UPROPERTY()
	TObjectPtr<UTextBlock> Label;
};

UCLASS(Blueprintable)
class VELOX_API UVeloxHUDWidget : public UVeloxUserWidget
{
	GENERATED_BODY()

public:
	/** The data source (owned by AVeloxHUD). Null shows an empty HUD. */
	void SetViewModel(UVeloxHUDViewModel* InViewModel);
	UVeloxHUDViewModel* GetViewModel() const { return ViewModel; }

	/** Copies the view model into the elements. NativeTick calls it every frame; tests call it directly. */
	void RefreshFromViewModel();

	/** One line with the bound texts (health, armor, ammo, clock, banner, row counts): the headless log and tests. */
	FString DescribeBoundValues() const;

	// ----- Test accessors (the dynamic rows).
	int32 GetNumVisibleFeedRows() const;
	FString GetFeedRowText(int32 Index) const;
	int32 GetNumVisibleWeaponCells() const;
	FString GetWeaponCellText(int32 Index) const;
	bool IsWeaponCellCurrent(int32 Index) const;
	int32 GetNumVisiblePowerupRows() const;
	FString GetPowerupRowText(int32 Index) const;
	/** Intensity-driven visibility of the damage indicator of a VeloxHUDFormat::EDamageQuadrant. */
	bool IsDamageIndicatorVisible(int32 Quadrant) const;
	bool IsHitMarkerVisible() const;

	// ----- Elements (all optional, bound by name in a Blueprint subclass).
	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ArmorText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AmmoText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WeaponNameText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ClockText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BannerText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DeathTitleText;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> DeathPromptText;

	/** Parents of the dynamic rows (the widget adds/removes children as the data changes). */
	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UHorizontalBox> WeaponBar;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> PowerupBox;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> KillFeedBox;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairLeft;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairRight;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairUp;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairDown;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> CrosshairDot;

	/** The four ticks of the hit marker, upper-right, lower-right, lower-left, upper-left. */
	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> HitMarkerTick0;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> HitMarkerTick1;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> HitMarkerTick2;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> HitMarkerTick3;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> DamageFront;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> DamageRight;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> DamageBack;

	UPROPERTY(BlueprintReadOnly, Meta = (BindWidgetOptional))
	TObjectPtr<UImage> DamageLeft;

protected:
	//~UUserWidget interface
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	//~End of UUserWidget interface

	//~UVeloxUserWidget interface
	virtual UWidget* BuildDefaultTree() override;
	//~End of UVeloxUserWidget interface

private:
	void RefreshStats(const UVeloxHUDViewModel& Model);
	void RefreshWeaponBar(const UVeloxHUDViewModel& Model);
	void RefreshPowerups(const UVeloxHUDViewModel& Model);
	void RefreshFeed(const UVeloxHUDViewModel& Model);
	void RefreshCrosshair(const UVeloxHUDViewModel& Model);
	void RefreshHitMarker(const UVeloxHUDViewModel& Model);
	void RefreshDamageIndicators(const UVeloxHUDViewModel& Model);
	void RefreshMatch(const UVeloxHUDViewModel& Model);

	UImage* MakeBar(FName Name);

	UPROPERTY(Transient)
	TObjectPtr<UVeloxHUDViewModel> ViewModel;

	UPROPERTY(Transient)
	TArray<FVeloxHUDFeedRowWidgets> FeedRows;

	UPROPERTY(Transient)
	TArray<FVeloxHUDWeaponCellWidgets> WeaponCells;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> PowerupRows;

	int32 NumVisibleFeedRows = 0;
	int32 NumVisibleWeaponCells = 0;
	int32 NumVisiblePowerupRows = 0;
};
