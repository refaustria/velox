// Velox - world-free item value for bot routing (ADR 0005 section 7): Q3-style need x base value / travel time with
// respawn timing. Pure functions, unit-tested in Velox.Bots.ItemValue (the ADR 0001 section 4 "item-value scoring" test).

#pragma once

#include "CoreMinimal.h"
#include "Items/VeloxItemTypes.h"

struct FVeloxBotDifficultyParams;

/** One pickup as seen by one bot. */
struct FVeloxItemScoringInput
{
	/** Task 02 refinement: stable tie-break key for Rank (e.g. the pickup's registry index or unique id). */
	int32 CandidateId = INDEX_NONE;
	EVeloxItemType Type = EVeloxItemType::Health;
	/** Amount the pickup grants (informational; the need formula of section 7 does not use it). */
	int32 Quantity = 0;
	/** UVeloxItemDefinition::BotBaseValue (Q3 tier weight). */
	float BotBaseValue = 0.f;
	bool bMajorItem = false;
	EVeloxHealthCap HealthCap = EVeloxHealthCap::MaxHealth;
	int32 Health = 0;
	int32 MaxHealth = 0;
	int32 HealthHardCap = 0;
	int32 Armor = 0;
	int32 MaxArmor = 0;
	/** Ammo / weapon items: the bot's current count and cap of the ammo type involved. */
	int32 Ammo = 0;
	int32 AmmoMax = 0;
	/** Weapon items: the bot owns this weapon. */
	bool bOwnsWeapon = false;
	/** Ammo items: the bot owns a weapon that uses this ammo. */
	bool bOwnsWeaponUsingAmmo = false;
	bool bAvailable = true;
	float TimeUntilRespawn = 0.f;
	float TravelSeconds = 0.f;
};

struct FVeloxItemScoringParams
{
	/** 0 = ignore unavailable items, 1 = go early up to MaxWaitSeconds. */
	float ItemTimingSkill = 0.f;
	float MaxWaitSeconds = 0.f;
	/** Keeps an item under the bot's feet finite. */
	float TravelOffsetSeconds = 1.0f;
};

/** One entry of a ranking. */
struct FVeloxRankedItem
{
	int32 CandidateId = INDEX_NONE;
	float Score = 0.f;
};

namespace VeloxItemValue
{
	/**
	 * 0..1. Health: (Cap - Health) / Cap, Cap = MaxHealth or HealthHardCap per HealthCap. Armor: (MaxArmor - Armor) /
	 * MaxArmor. Ammo: (AmmoMax - Ammo) / AmmoMax, x 0.1 when no owned weapon uses it. Weapon: 1 when not owned, else 0.3 x
	 * the ammo need. Powerup: 1.
	 */
	VELOX_API float Need(const FVeloxItemScoringInput& In);

	/**
	 * Value / (TravelSeconds + Wait + TravelOffsetSeconds), Value = BotBaseValue x Need, Wait = 0 when available else
	 * max(0, TimeUntilRespawn - TravelSeconds); 0 when Wait > MaxWaitSeconds x ItemTimingSkill or the value is 0.
	 */
	VELOX_API float Score(const FVeloxItemScoringInput& In, const FVeloxItemScoringParams& Params);

	/** Scores every candidate, best first; equal scores ordered by CandidateId ascending (deterministic). */
	VELOX_API void Rank(TConstArrayView<FVeloxItemScoringInput> Candidates, const FVeloxItemScoringParams& Params, TArray<FVeloxRankedItem>& OutRanking);

	/** Scoring params of a difficulty preset. */
	VELOX_API FVeloxItemScoringParams MakeParams(const FVeloxBotDifficultyParams& Difficulty);
}
