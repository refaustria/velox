// Velox - bot data types shared by the pure cores and the data assets (ADR 0005 sections 9 and 11).
// Task M4-03 created this header with the difficulty POD only; M4-02 (pure cores) adds its own types below.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

#include "VeloxBotTypes.generated.h"

/** One weapon's range preference (ADR 0005 section 9; Q3 fw_weights-shaped). Distances in cm. */
USTRUCT(BlueprintType)
struct VELOX_API FVeloxBotWeaponPreference
{
	GENERATED_BODY()

	/** Velox.Weapon.* tag of the weapon. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot")
	FGameplayTag Weapon;

	/** Relative desirability inside the best band. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot", Meta = (ClampMin = "0.0"))
	float Weight = 0.f;

	/** Below this range the weapon scores 0 (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot", Meta = (ClampMin = "0.0", Units = "cm"))
	float MinRange = 0.f;

	/** Best band start (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot", Meta = (ClampMin = "0.0", Units = "cm"))
	float BestMin = 0.f;

	/** Best band end (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot", Meta = (ClampMin = "0.0", Units = "cm"))
	float BestMax = 0.f;

	/** Above this range the weapon scores 0 (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Bot", Meta = (ClampMin = "0.0", Units = "cm"))
	float MaxRange = 0.f;
};

/**
 * POD copy of UVeloxBotDifficulty the pure bot functions take (ADR 0005 section 11). UVeloxBotDifficulty::BuildParams()
 * fills it, with the non-shipping velox.bots.<Field> cvar overrides applied. Defaults are the Medium preset.
 */
struct FVeloxBotDifficultyParams
{
	// --- Aim (section 8) ---
	float ReactionSeconds = 0.30f;
	float ReactionJitter = 0.3f;
	float AimErrorDeg = 4.f;
	float ErrorCorrelationSeconds = 0.3f;
	float TrackingSmoothingSeconds = 0.15f;
	float MaxTurnRateDegPerSec = 420.f;
	bool bLeadProjectiles = true;
	float LeadScale = 0.7f;
	float FireConeDeg = 7.f;

	// --- Perception (section 6) ---
	float PerceptionFovDeg = 130.f;
	float PerceptionRangeCm = 8000.f;
	float PerceptionIntervalSeconds = 0.1f;
	float EnemyMemorySeconds = 3.f;

	// --- Movement (sections 4, 5) ---
	bool bBunnyHop = true;
	bool bStrafeJump = false;
	bool bRocketJumps = false;
	float EstimatedTravelSpeed = 800.f;

	// --- Item routing and goals (sections 2, 7) ---
	float ItemTimingSkill = 0.5f;
	float MaxWaitSeconds = 4.f;
	float GoalReevaluateSeconds = 1.f;
	float GoalSwitchHysteresis = 1.3f;
	float FightItemBias = 1.5f;

	// --- Strafing and dodging (section 10) ---
	float StrafeAmplitude = 0.7f;
	float StrafeFlipSecondsMin = 0.5f;
	float StrafeFlipSecondsMax = 1.2f;
	float DodgeJumpChancePerSec = 0.3f;
	bool bDodgeProjectiles = false;
	float ProjectileDodgeReactionSeconds = 0.25f;

	// --- Weapons (section 9) ---
	TArray<FVeloxBotWeaponPreference> WeaponPreferences;
	float PreferredRange = 800.f;
};
