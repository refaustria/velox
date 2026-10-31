// Velox - UI tunables (ADR 0004 section 7): HUD colours, font sizes, thresholds and timings. Config = Game, so
// DefaultGame.ini [/Script/Velox.VeloxUISettings] overrides the defaults below. Crosshair style/size/colour are NOT here:
// they are the player's choice (UVeloxGameUserSettings).

#pragma once

#include "Engine/DeveloperSettings.h"

#include "VeloxUISettings.generated.h"

UCLASS(Config = Game, DefaultConfig, Meta = (DisplayName = "Velox UI"))
class VELOX_API UVeloxUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UVeloxUISettings();

	//~UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Velox"); }
	//~End of UDeveloperSettings interface

	/** The project's settings (class defaults are valid without a config file, so this never returns null). */
	static const UVeloxUISettings& Get();

	/** Q3 health colour steps: Health >= HealthHighMin white, >= HealthMidMin yellow, else red. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Health")
	int32 HealthHighMin = 100;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Health")
	int32 HealthMidMin = 30;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Health")
	FLinearColor HealthHighColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Health")
	FLinearColor HealthMidColor = FLinearColor(1.f, 0.85f, 0.f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Health")
	FLinearColor HealthLowColor = FLinearColor(1.f, 0.1f, 0.1f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Health")
	FLinearColor ArmorColor = FLinearColor(0.9f, 0.8f, 0.2f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Text")
	FLinearColor TextColor = FLinearColor::White;

	/** Weapon bar: the current weapon's cell tint and the others'. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|WeaponBar")
	FLinearColor WeaponCurrentColor = FLinearColor(1.f, 0.85f, 0.f, 0.55f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|WeaponBar")
	FLinearColor WeaponOtherColor = FLinearColor(0.f, 0.f, 0.f, 0.35f);

	/** Ammo at or below this (and above 0) is drawn in HealthLowColor. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|WeaponBar", Meta = (ClampMin = "0"))
	int32 LowAmmoThreshold = 5;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Text", Meta = (ClampMin = "6"))
	int32 BigFontSize = 40;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Text", Meta = (ClampMin = "6"))
	int32 MediumFontSize = 24;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Text", Meta = (ClampMin = "6"))
	int32 SmallFontSize = 16;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Text", Meta = (ClampMin = "6"))
	int32 BannerFontSize = 48;

	/** Screen margin of the corner blocks (Slate units). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Layout", Meta = (ClampMin = "0"))
	float ScreenMargin = 24.f;

	/** Damage direction indicators fade over this many seconds (ADR 0004 section 7). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Damage", Meta = (ClampMin = "0.05", Units = "s"))
	float DamageIndicatorSeconds = 0.6f;

	/** Damage from a source closer than this (horizontal, cm; rocket jumps, point-blank splash) shows no direction. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Damage", Meta = (ClampMin = "0", Units = "cm"))
	float DamageIndicatorMinDistance = 50.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Damage")
	FLinearColor DamageIndicatorColor = FLinearColor(1.f, 0.05f, 0.05f, 0.8f);

	/** Distance from the screen centre and size of the four indicator bars, Slate units. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Damage", Meta = (ClampMin = "0"))
	float DamageIndicatorRadius = 140.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Damage")
	FVector2D DamageIndicatorBarSize = FVector2D(90.0, 12.0);

	/** Hit marker tick colours (UVeloxCombatSettings keeps HitMarkerSeconds/Size) and the tick thickness. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|HitMarker")
	FLinearColor HitMarkerColor = FLinearColor::White;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|HitMarker")
	FLinearColor HitMarkerKillColor = FLinearColor(1.f, 0.1f, 0.1f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|HitMarker", Meta = (ClampMin = "1"))
	float HitMarkerThickness = 2.f;

	/** Kill marker length scale, and the gap from the centre as a fraction of HitMarkerSize. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|HitMarker", Meta = (ClampMin = "1"))
	float HitMarkerKillScale = 1.6f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|HitMarker", Meta = (ClampMin = "0"))
	float HitMarkerGapFraction = 0.5f;

	/** Crosshair bar thickness and the gap from the centre (Slate units; the length is the user's CrosshairSize). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", Meta = (ClampMin = "1"))
	float CrosshairThickness = 2.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Crosshair", Meta = (ClampMin = "0"))
	float CrosshairGap = 4.f;

	/** Kill feed lines older than this (server seconds) disappear; at most the replicated feed length is shown. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Feed", Meta = (ClampMin = "1", Units = "s"))
	float FeedEntrySeconds = 6.f;

	/** "FIGHT" is shown this long after Live starts. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Banner", Meta = (ClampMin = "0", Units = "s"))
	float FightBannerSeconds = 1.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Banner")
	FLinearColor BannerColor = FLinearColor(1.f, 0.85f, 0.f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Death")
	FLinearColor DeathTitleColor = FLinearColor(1.f, 0.1f, 0.1f, 1.f);

	/** Death overlay prompt while a dead player free-flies before MinRespawnDelay (ADR 0004 C11); the respawn prompt follows. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Death")
	FString DeathSpectatePrompt = TEXT("free look \u2014 FIRE to respawn");

	/** Key shown in the ready prompt and the scoreboard button (the default binding of IA_Ready; rebinds do not change the text). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Ready")
	FString ReadyKeyLabel = TEXT("F3");

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Ready")
	FLinearColor ReadyPromptColor = FLinearColor(0.4f, 1.f, 0.4f, 1.f);

	/** The pickup message ("Mega Health", "Anarki took Mega Health") stays this long, fading over the last 40 %. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Pickup", Meta = (ClampMin = "0.1", Units = "s"))
	float PickupMessageSeconds = 2.5f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|Pickup")
	FLinearColor PickupMessageColor = FLinearColor(1.f, 1.f, 0.6f, 1.f);

	/** Spectator item timers: colour of the lines of items that are up, and of the ones still down. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|ItemTimers")
	FLinearColor ItemTimerUpColor = FLinearColor(0.4f, 1.f, 0.4f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "HUD|ItemTimers")
	FLinearColor ItemTimerDownColor = FLinearColor::White;

	/** Scoreboard (Tab, forced in PostMatch): panel width in Slate units, background, header, local-player row and ready colours. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Scoreboard", Meta = (ClampMin = "200"))
	float ScoreboardWidth = 760.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Scoreboard")
	FLinearColor ScoreboardBackgroundColor = FLinearColor(0.f, 0.f, 0.f, 0.7f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Scoreboard")
	FLinearColor ScoreboardHeaderColor = FLinearColor(0.7f, 0.7f, 0.7f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Scoreboard")
	FLinearColor ScoreboardLocalRowColor = FLinearColor(1.f, 1.f, 1.f, 0.18f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Scoreboard")
	FLinearColor ScoreboardReadyColor = FLinearColor(0.4f, 1.f, 0.4f, 1.f);

	/** Fill weight of the name column against the five numeric columns (weight 1 each). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Scoreboard", Meta = (ClampMin = "1"))
	float ScoreboardNameColumnFill = 4.f;

	// ---- Menus (task M3-10): main menu, settings, pause menu ----

	/** Viewport z-order of the pause menu (above the HUD at 0 and the scoreboard), and of the settings screen opened from a menu. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu")
	int32 PauseMenuZOrder = 50;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu")
	int32 SettingsZOrder = 60;

	/** Full-screen backdrop of the main menu and settings screen; the pause menu dims the game with PauseDimColor. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Colors")
	FLinearColor MenuBackgroundColor = FLinearColor(0.02f, 0.025f, 0.04f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Colors")
	FLinearColor PauseDimColor = FLinearColor(0.f, 0.f, 0.f, 0.7f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Colors")
	FLinearColor MenuTitleColor = FLinearColor(1.f, 0.85f, 0.f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Colors")
	FLinearColor MenuTextColor = FLinearColor(0.9f, 0.92f, 0.95f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Colors")
	FLinearColor MenuMutedTextColor = FLinearColor(0.55f, 0.58f, 0.65f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Colors")
	FLinearColor MenuErrorColor = FLinearColor(1.f, 0.3f, 0.25f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Colors")
	FLinearColor MenuButtonColor = FLinearColor(0.12f, 0.15f, 0.24f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Colors")
	FLinearColor MenuButtonHoverColor = FLinearColor(0.25f, 0.32f, 0.55f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Colors")
	FLinearColor MenuPanelColor = FLinearColor(0.06f, 0.07f, 0.11f, 1.f);

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Fonts", Meta = (ClampMin = "8"))
	int32 MenuTitleFontSize = 64;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Fonts", Meta = (ClampMin = "8"))
	int32 MenuHeadingFontSize = 26;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Fonts", Meta = (ClampMin = "8"))
	int32 MenuFontSize = 18;

	/** Button width and the padding around menu rows (Slate units). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Layout", Meta = (ClampMin = "40"))
	float MenuButtonWidth = 320.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Layout", Meta = (ClampMin = "0"))
	float MenuRowPadding = 6.f;

	/** Width of the settings screen's scrolling panel and of its label / key button columns. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Layout", Meta = (ClampMin = "200"))
	float SettingsPanelWidth = 760.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Layout", Meta = (ClampMin = "100"))
	float SettingsPanelMaxHeight = 640.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Layout", Meta = (ClampMin = "50"))
	float SettingsLabelWidth = 260.f;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Layout", Meta = (ClampMin = "50"))
	float SettingsControlWidth = 280.f;

	/** Frame limits offered by the video section (0 = unlimited). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Menu|Video")
	TArray<int32> FrameRateLimitChoices = {0, 60, 125, 144, 165, 240, 333};
};
