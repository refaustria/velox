// Velox - powerups (ADR 0004 section 3, task M3-02): native duration GEs on the PlayerState ASC, the regeneration
// execution and the grant/extend helper the pickup uses. Quad and Battle Suit act in the damage path (FVeloxDamageMath,
// UVeloxDamageExecution, the fire abilities, AVeloxProjectile); Haste acts in the move timeline (task 03).
//
// Replication: the ASC runs Mixed, so the owner holds the full spec (remaining time for the HUD) and everyone else gets
// the granted tag and the looping cue GameplayCue.Powerup.<Name> through the minimal replication path (body tint).
// Durations are never stored here: the item definition's PowerupSeconds reaches the GE as SetByCaller.Duration.

#pragma once

#include "GameplayEffect.h"
#include "GameplayEffectExecutionCalculation.h"
#include "NativeGameplayTags.h"
#include "Templates/SubclassOf.h"

#include "VeloxPowerupEffects.generated.h"

class UAbilitySystemComponent;
class UVeloxCombatSettings;

namespace VeloxPowerupTags
{
	/** Looping cues of the four powerups (added and removed with the GE; UVeloxCombatFeedbackComponent tints the body). */
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Powerup);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Powerup_Quad);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Powerup_Haste);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Powerup_Regen);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(GameplayCue_Powerup_BattleSuit);

	/** Velox.Powerup.<Name> for GameplayCue.Powerup.<Name> (invalid for any other tag). */
	VELOX_API FGameplayTag PowerupTagForCue(const FGameplayTag& CueTag);
}

/**
 * HasDuration (SetByCaller.Duration = the item's seconds, or the remaining seconds plus the item's on a fallback re-apply),
 * no stacking (a second pickup extends the active effect in place, VeloxPowerups::GrantOrExtend). Grants its
 * Velox.Powerup.<Name> tag and its looping cue. Abstract: the four subclasses below are the item effects.
 */
UCLASS(Abstract)
class VELOX_API UVeloxGE_Powerup : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVeloxGE_Powerup();

	/** Velox.Powerup.<Name> of this effect (class default). */
	const FGameplayTag& GetPowerupTag() const { return PowerupTag; }

protected:
	/** Subclass constructors: the granted tag and the looping cue. */
	void InitPowerup(const FGameplayTag& InPowerupTag, const FGameplayTag& CueTag);

	UPROPERTY(VisibleDefaultsOnly, Category = "Velox|Powerup")
	FGameplayTag PowerupTag;
};

/** Quad Damage: Velox.Powerup.Quad (x QuadDamageFactor outgoing damage at fire). */
UCLASS()
class VELOX_API UVeloxGE_Powerup_Quad : public UVeloxGE_Powerup
{
	GENERATED_BODY()
public:
	UVeloxGE_Powerup_Quad();
};

/** Haste: Velox.Powerup.Haste (speed and fire rate in the move timeline, task 03). */
UCLASS()
class VELOX_API UVeloxGE_Powerup_Haste : public UVeloxGE_Powerup
{
	GENERATED_BODY()
public:
	UVeloxGE_Powerup_Haste();
};

/** Regeneration: Velox.Powerup.Regen, period 1 s (Q3 ClientTimerActions), not on application, UVeloxRegenExecution. */
UCLASS()
class VELOX_API UVeloxGE_Powerup_Regen : public UVeloxGE_Powerup
{
	GENERATED_BODY()
public:
	UVeloxGE_Powerup_Regen();
};

/** Battle Suit: Velox.Powerup.BattleSuit (victim side of FVeloxDamageMath::Apply). */
UCLASS()
class VELOX_API UVeloxGE_Powerup_BattleSuit : public UVeloxGE_Powerup
{
	GENERATED_BODY()
public:
	UVeloxGE_Powerup_BattleSuit();
};

/**
 * One regeneration tick on the target (FVeloxDamageMath::RegenStep with UVeloxCombatSettings): +15 below max health
 * capped at max x 1.1, else +5 up to HealthHardCap. Nothing while the target is dead or match-frozen (Q3 runs the timer
 * actions only for a living client outside the intermission). Logs `VeloxRegen target= health a->b`.
 */
UCLASS()
class VELOX_API UVeloxRegenExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UVeloxRegenExecution();

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams, FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};

/** How VeloxPowerups::GrantOrExtend gave the powerup. */
enum class EVeloxPowerupGrant : uint8
{
	Failed,
	/** Not active: applied with the full duration. */
	Applied,
	/** Active: extended in place (UAbilitySystemComponent::ModifyActiveEffectStartTime, no removal, no cue restart). */
	Extended,
	/** Active, but the in-place extension did not take (fallback): removed and re-applied with the summed duration. */
	Reapplied,
};

namespace VeloxPowerups
{
	/** Remaining seconds of the longest active effect of EffectClass on ASC; 0 when none is active. */
	VELOX_API float GetRemainingSeconds(const UAbilitySystemComponent& ASC, TSubclassOf<UGameplayEffect> EffectClass);

	/**
	 * Server: gives EffectClass so that TotalSeconds remain afterwards (the pickup rule's result: remaining + item seconds,
	 * Q3 Pickup_Powerup). Not active: apply with SetByCaller.Duration = TotalSeconds. Active: shift its start time by
	 * TotalSeconds - remaining (StartServerWorldTime replicates to the owner, CheckDuration re-arms the expiry timer);
	 * if the shifted effect does not report TotalSeconds, fall back to remove + re-apply with TotalSeconds.
	 */
	VELOX_API EVeloxPowerupGrant GrantOrExtend(UAbilitySystemComponent& ASC, TSubclassOf<UVeloxGE_Powerup> EffectClass, float TotalSeconds);

	/** Outgoing damage scale of a shooter at fire: QuadDamageFactor while it has Velox.Powerup.Quad, else 1. */
	VELOX_API float GetOutgoingDamageScale(const UAbilitySystemComponent* ShooterASC, const UVeloxCombatSettings& Settings);

	/** Quad state of a shooter (Velox.Powerup.Quad). */
	VELOX_API bool HasQuad(const UAbilitySystemComponent* ShooterASC);
}
