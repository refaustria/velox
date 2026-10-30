// Velox - world-free bot goal choice (ADR 0005 section 2): fight / item / roam with item-switch hysteresis and Duel
// camping. Pure function over plain structs (ids instead of actor pointers; the controller maps them), unit-tested in
// Velox.Bots.Decision.

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "Bots/VeloxBotCoreTypes.h"
#include "Bots/VeloxBotTypes.h"

/** One scored item candidate (VeloxItemValue::Score already applied by the routing glue). */
struct FVeloxGoalItemCandidate
{
	int32 CandidateId = INDEX_NONE;
	float Score = 0.f;
	FVector Location = FVector::ZeroVector;
	bool bMajorItem = false;
	bool bAvailable = true;
	float TimeUntilRespawn = 0.f;
	float TravelSeconds = 0.f;
	/** Path query found a full path (partial / none: never chosen, never camped). */
	bool bReachable = true;
};

/**
 * World-free form of the ADR's FVeloxBotGoal: the controller keeps the weak pointers and SetTime, this keeps ids.
 * Item / Roam-camp: ItemId; Fight: EnemyId; Point = item location, enemy location or roam point.
 */
struct FVeloxBotGoalChoice
{
	EVeloxBotGoal Type = EVeloxBotGoal::None;
	int32 ItemId = INDEX_NONE;
	int32 EnemyId = INDEX_NONE;
	FVector Point = FVector::ZeroVector;
	float Score = 0.f;
};

/**
 * Task 02 refinement: ADR section 2 names a "fight score" without defining it. Units are item-score units (value per
 * second) so FightItemBias compares like with like. Not in the section 11 table: defaults here until UVeloxBotDifficulty
 * or UVeloxBotSettings surfaces them.
 */
struct FVeloxFightScoreParams
{
	float BaseScore = 20.f;
	/** Health + armor at which the fight score saturates. */
	float FullStrength = 100.f;
	/** Fight score factor with WeaponStrength 0. */
	float WeakWeaponScale = 0.5f;
};

struct FVeloxBotDecisionInput
{
	bool bPostMatch = false;
	bool bDuel = false;

	/** Best visible enemy (section 6). */
	bool bEnemyVisible = false;
	int32 EnemyId = INDEX_NONE;
	FVector EnemyLocation = FVector::ZeroVector;

	/** Own state. WeaponStrength 0..1: VeloxBotItemRouting::WeaponStrength (D13; best usable weight / table max). */
	int32 Health = 0;
	int32 Armor = 0;
	float WeaponStrength = 1.f;

	TArray<FVeloxGoalItemCandidate> Items;
	/** Random reachable nav points within RoamRadiusCm, sampled by the caller; one is drawn with the stream. */
	TArray<FVector> RoamPoints;

	FVeloxFightScoreParams Fight;

	/** The goal currently being pursued (hysteresis). */
	FVeloxBotGoalChoice Current;
};

namespace VeloxBotDecision
{
	/**
	 * Fight attractiveness in item-score units: Fight.BaseScore x clamp((Health + Armor) / Fight.FullStrength, 0, 1) x
	 * lerp(Fight.WeakWeaponScale, 1, WeaponStrength).
	 */
	VELOX_API float FightScore(const FVeloxBotDecisionInput& In);

	/**
	 * PostMatch -> None. Best item: the highest-scoring reachable candidate with Score > 0; a current Item goal still in
	 * the list with Score > 0 is kept unless the best scores > current x GoalSwitchHysteresis. Enemy visible -> Fight
	 * unless the best item's score > FightItemBias x FightScore (then Item); D13 hysteresis: with a current Item goal the
	 * fight wins only when FightItemBias x FightScore > Item.Score x GoalSwitchHysteresis, with a current Fight goal the
	 * item wins only when Item.Score > FightItemBias x FightScore x GoalSwitchHysteresis. No enemy -> the best item, else Roam: in Duel
	 * the unavailable reachable major item with the smallest TimeUntilRespawn - TravelSeconds (camping, ItemId set), else
	 * the current Roam point while it is still the goal, else a random RoamPoints entry (stream), else None.
	 */
	VELOX_API FVeloxBotGoalChoice ChooseGoal(const FVeloxBotDecisionInput& In, const FVeloxBotDifficultyParams& Params, FRandomStream& Stream);
}
