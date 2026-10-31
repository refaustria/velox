// Velox - world-free bot weapon choice (ADR 0005 section 9): data-driven range bands, ammo, splash safety, switch
// hysteresis. Pure functions, unit-tested in Velox.Bots.WeaponChoice.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Bots/VeloxBotTypes.h"

/** One weapon the bot could hold, from its inventory and the weapon definition. */
struct FVeloxWeaponChoiceOption
{
	FGameplayTag Weapon;
	bool bOwned = false;
	int32 Ammo = 0;
	/** 0 for weapons without ammo (Gauntlet). */
	int32 AmmoPerShot = 1;
	/** UVeloxWeaponDefinition::SplashRadiusCm; > 0 enables the self-splash range rule. */
	float SplashRadiusCm = 0.f;
};

struct FVeloxWeaponChoiceInput
{
	TArray<FVeloxWeaponChoiceOption> Weapons;
	bool bHasTarget = false;
	/** Distance to the target, cm (used when bHasTarget). */
	float RangeCm = 0.f;
	/** Pre-selection range without a target, cm (preset PreferredRange). */
	float PreferredRangeCm = 0.f;
	/** The weapon currently held (or being switched to). */
	FGameplayTag CurrentWeapon;
	/** Score multiplier of the current weapon (a switch costs 0.45 s). */
	float SwitchHysteresis = 1.25f;
	/** Splash weapons score 0 below SplashRadiusCm + this. */
	float SplashSafetyMarginCm = 100.f;
};

namespace VeloxWeaponChoice
{
	/** 1 inside [BestMin, BestMax], linear to 0.3 at MinRange / MaxRange, 0 outside [MinRange, MaxRange]. */
	VELOX_API float Band(const FVeloxBotWeaponPreference& Pref, float Range);

	/** Weight x Band, 0 when not owned, without ammo, or a splash weapon inside its safety range; no hysteresis. */
	VELOX_API float ScoreOption(const FVeloxWeaponChoiceOption& Option, const FVeloxBotWeaponPreference& Pref, float Range, float SplashSafetyMarginCm);

	/**
	 * Index into Preferences of the weapon to hold, INDEX_NONE when nothing scores > 0. Range = RangeCm with a target,
	 * PreferredRangeCm without. The current weapon's score is multiplied by SwitchHysteresis; ties keep the earlier row.
	 * Preferences without a matching option are skipped.
	 */
	VELOX_API int32 Pick(const FVeloxWeaponChoiceInput& In, TConstArrayView<FVeloxBotWeaponPreference> Preferences);
}
