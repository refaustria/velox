// Velox - project-wide bot settings (ADR 0005 section 11). Config = Game, so DefaultGame.ini
// [/Script/Velox.VeloxBotSettings] overrides the defaults below. Per-preset values live in UVeloxBotDifficulty.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "VeloxBotSettings.generated.h"

class UVeloxBotDifficulty;

UCLASS(Config = Game, DefaultConfig, Meta = (DisplayName = "Velox Bots"))
class VELOX_API UVeloxBotSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UVeloxBotSettings();

	//~UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return TEXT("Velox"); }
	//~End of UDeveloperSettings interface

	/** The project's settings (class defaults are valid without a config file, so this never returns null). */
	static const UVeloxBotSettings& Get();

	/** Logs the UVeloxBotDifficulty assets under /Game/Bots found through the asset registry (velox.bots.ListPresets). Returns the count. */
	static int32 ListPresets();

	/** Preset used by AddBot without a difficulty and by ?Bots=N without ?BotDifficulty. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots")
	TSoftObjectPtr<UVeloxBotDifficulty> DefaultDifficulty;

	/** Names handed to new bots in order (a number suffix is added when exhausted). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots")
	TArray<FString> BotNames;

	/** Bots added at map start when the URL has no ?Bots=. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots", Meta = (ClampMin = "0"))
	int32 DefaultBotCount = 0;

	/** Item candidates that get a real path query per goal evaluation (ADR 0005 section 7; float per the ADR table). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "1.0"))
	float PathQueryCandidates = 4.f;

	/** Roam goal: radius around the bot where a random navigable point is picked (cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "0.0", Units = "cm"))
	float RoamRadiusCm = 3000.f;

	/** Fight score at full strength and the best weapon (item-score units, value per second; ADR 0005 D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "0.0"))
	float FightBaseScore = 12.f;

	/** Health + armor at which the fight score saturates (D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "1.0"))
	float FightFullStrength = 200.f;

	/** Fight score factor with the weakest weapon (WeaponStrength 0; D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Goals", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FightWeakWeaponScale = 0.25f;

	/** Pad / rocket jump: arrival radius at the link start (cm; ADR 0005 section 4, D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float LinkStartAcceptanceCm = 30.f;

	/** JumpFast: jump only at >= the link's RequiredSpeed x this (D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JumpFastSpeedFraction = 0.9f;

	/** JumpFast: back-up distance from the link start before a retry (cm; D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float JumpFastRunUpCm = 600.f;

	/** JumpFast: retries after a too-slow approach (D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0"))
	int32 JumpFastRetries = 1;

	/** RocketJump: abort when the explosion did not launch the pawn within this (s; D4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.01", Units = "s"))
	float RocketJumpImpulseTimeoutSeconds = 0.3f;

	/** Distance to a nav link end at which the traversal counts as done (cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float LinkEndAcceptanceCm = 150.f;

	/** Distance before a link start at which the takeoff maneuver begins (cm). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.0", Units = "cm"))
	float TakeoffLeadCm = 100.f;

	/** No progress for this long counts as stuck (s). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Navigation", Meta = (ClampMin = "0.1", Units = "s"))
	float StuckSeconds = 2.f;

	/** A bot tick above this controller time is logged once (microseconds). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Debug", Meta = (ClampMin = "0.0"))
	float BudgetWarnMicroseconds = 600.f;

	/** Master random seed; 0 seeds from the clock. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Bots|Debug")
	int32 DeterministicSeed = 0;
};
