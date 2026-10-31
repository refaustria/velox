// Velox - bot difficulty preset (ADR 0005 section 11). One data asset per preset: Content/Bots/DA_Bot_Easy/Medium/Hard/Expert,
// written by Tools/editor/create_bot_content.py with the section 11 table. No bot tunable lives in code.

#pragma once

#include "Bots/VeloxBotTypes.h"
#include "Engine/DataAsset.h"

#include "VeloxBotDifficulty.generated.h"

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Bot Difficulty"))
class VELOX_API UVeloxBotDifficulty : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** POD copy for the pure bot functions, with the non-shipping velox.bots.<Field> cvar overrides applied. */
	FVeloxBotDifficultyParams BuildParams() const;

	/** The same copy without cvar overrides (asset values only; tests). */
	FVeloxBotDifficultyParams BuildBaseParams() const;

	/** Primary asset id: type "VeloxBotDifficulty", the asset name. */
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	// --- Aim (section 8) ---

	/** Delay between a target becoming visible and the first aim change (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Aim", Meta = (ClampMin = "0.0", Units = "s"))
	float ReactionSeconds = 0.30f;

	/** Reaction drawn uniformly in ReactionSeconds x [1 - J, 1 + J] (ADR 0005 D4). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Aim", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ReactionJitter = 0.3f;

	/** Aim error standard deviation per axis (degrees). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Aim", Meta = (ClampMin = "0.0", Units = "deg"))
	float AimErrorDeg = 4.f;

	/** Correlation time of the aim error noise (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Aim", Meta = (ClampMin = "0.01", Units = "s"))
	float ErrorCorrelationSeconds = 0.3f;

	/** Smoothing time constant of target tracking (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Aim", Meta = (ClampMin = "0.0", Units = "s"))
	float TrackingSmoothingSeconds = 0.15f;

	/** Maximum view turn rate (degrees per second). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Aim", Meta = (ClampMin = "1.0", Units = "deg/s"))
	float MaxTurnRateDegPerSec = 420.f;

	/** Leads projectile weapons at moving targets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Aim")
	bool bLeadProjectiles = true;

	/** Fraction of the ideal lead applied. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Aim", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LeadScale = 0.7f;

	/** The bot fires when the aim is within this cone around the target (degrees). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Aim", Meta = (ClampMin = "0.0", ClampMax = "90.0", Units = "deg"))
	float FireConeDeg = 7.f;

	/**
	 * ADR 0005 D50: scale of the direct-hit gate (UVeloxBotCombatSettings::DirectHitRadiusCm x this) for direct-hit weapons;
	 * 0 = the cone alone (the bot fires through its aim error: spray).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Combat", Meta = (ClampMin = "0.0"))
	float DirectHitRadiusScale = 2.0f;

	/** D50 trigger hysteresis: while the trigger is held the bot keeps firing inside FireConeDeg x this, without the gate (<= 1 = none). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Combat", Meta = (ClampMin = "1.0"))
	float FireReleaseConeScale = 1.2f;

	/** D50: a held trigger stays down this long after the target stopped being visible (fires through the corner; s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Combat", Meta = (ClampMin = "0.0", Units = "s"))
	float FireHoldAfterLostSeconds = 0.15f;

	// --- Perception (section 6) ---

	/** Total field of view for seeing enemies (degrees). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Perception", Meta = (ClampMin = "1.0", ClampMax = "360.0", Units = "deg"))
	float PerceptionFovDeg = 130.f;

	/** Maximum sight distance (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Perception", Meta = (ClampMin = "0.0", Units = "cm"))
	float PerceptionRangeCm = 8000.f;

	/** Time between perception passes (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Perception", Meta = (ClampMin = "0.016", Units = "s"))
	float PerceptionIntervalSeconds = 0.1f;

	/** How long an enemy that left sight stays known (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Perception", Meta = (ClampMin = "0.0", Units = "s"))
	float EnemyMemorySeconds = 3.f;

	// --- Movement (sections 4, 5) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Movement")
	bool bBunnyHop = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Movement")
	bool bStrafeJump = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Movement")
	bool bRocketJumps = false;

	/** Average speed used to turn path length into travel time (cm/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Movement", Meta = (ClampMin = "1.0", Units = "cm/s"))
	float EstimatedTravelSpeed = 800.f;

	// --- Item routing and goals (sections 2, 7) ---

	/** 0 ignores respawn timers, 1 plans arrival at the respawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Goals", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ItemTimingSkill = 0.5f;

	/** Longest the bot waits for an item respawn (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Goals", Meta = (ClampMin = "0.0", Units = "s"))
	float MaxWaitSeconds = 4.f;

	/** Time between goal re-evaluations (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Goals", Meta = (ClampMin = "0.1", Units = "s"))
	float GoalReevaluateSeconds = 1.f;

	/** A new goal must beat the current one by this factor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Goals", Meta = (ClampMin = "1.0"))
	float GoalSwitchHysteresis = 1.3f;

	/** Item score multiplier that competes with fighting an enemy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Goals", Meta = (ClampMin = "0.0"))
	float FightItemBias = 1.5f;

	// --- Strafing and dodging (section 10) ---

	/** Lateral share of the steering wish while fighting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Combat", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StrafeAmplitude = 0.7f;

	/** Shortest time between strafe direction flips (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Combat", Meta = (ClampMin = "0.05", Units = "s"))
	float StrafeFlipSecondsMin = 0.5f;

	/** Longest time between strafe direction flips (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Combat", Meta = (ClampMin = "0.05", Units = "s"))
	float StrafeFlipSecondsMax = 1.2f;

	/** Probability per second of a random jump while fighting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Combat", Meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float DodgeJumpChancePerSec = 0.3f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Combat")
	bool bDodgeProjectiles = false;

	/** Delay before dodging a projectile that was noticed (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Combat", Meta = (ClampMin = "0.0", Units = "s"))
	float ProjectileDodgeReactionSeconds = 0.25f;

	// --- Weapons (section 9) ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Weapons")
	TArray<FVeloxBotWeaponPreference> WeaponPreferences;

	/** Range the bot pre-selects a weapon for when it has no target (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot|Weapons", Meta = (ClampMin = "0.0", Units = "cm"))
	float PreferredRange = 800.f;
};
