// Velox - the M2 gameplay effects, all native and configured in their constructors (ADR 0003 section 1; the build host
// is headless, no GE assets). Tunables are never stored in a GE: callers pass them as SetByCaller magnitudes, and the
// VeloxEffects helpers below build complete specs (every SetByCaller a GE reads is set).

#pragma once

#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"

#include "VeloxGameplayEffects.generated.h"

class UAbilitySystemComponent;
class UVeloxCombatSettings;
struct FVeloxGameplayEffectContext;

/** Instant; execution UVeloxDamageExecution (SetByCaller.Damage, optional SetByCaller.PelletHits, Velox context). */
UCLASS()
class VELOX_API UVeloxGE_Damage : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVeloxGE_Damage();
};

/** Instant; one Additive modifier per ammo attribute, magnitude SetByCaller.<ammo tag> (negative = cost). */
UCLASS()
class VELOX_API UVeloxGE_AmmoCost : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVeloxGE_AmmoCost();
};

/**
 * Instant; Override MaxHealth, MaxArmor, Health, Armor (SetByCaller.<Name>) and the six ammo attributes
 * (SetByCaller.<ammo tag>). Removes GE_Dead, GE_SpawnProtection, every powerup (Velox.Powerup.*: lost on death, ADR 0004
 * section 3) and a previous GE_HealthDecay, then applies GE_HealthDecay.
 */
UCLASS()
class VELOX_API UVeloxGE_SpawnInit : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVeloxGE_SpawnInit();
};

/** Infinite, period 1 s (Q3), not executed on application; execution UVeloxDecayExecution. */
UCLASS()
class VELOX_API UVeloxGE_HealthDecay : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVeloxGE_HealthDecay();
};

/** HasDuration (SetByCaller.Duration = drop + raise time); grants Velox.State.WeaponSwitching. */
UCLASS()
class VELOX_API UVeloxGE_WeaponSwitching : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVeloxGE_WeaponSwitching();
};

/** HasDuration (SetByCaller.Duration = SpawnProtectionSeconds); grants Velox.State.SpawnProtected. */
UCLASS()
class VELOX_API UVeloxGE_SpawnProtection : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVeloxGE_SpawnProtection();
};

/** Infinite; grants Velox.State.Dead; removed by GE_SpawnInit. */
UCLASS()
class VELOX_API UVeloxGE_Dead : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVeloxGE_Dead();
};

/**
 * Instant; Additive on Health, magnitude SetByCaller.Health (ADR 0004 section 2: health pickups). The pickup rule computes
 * the gain against the item's cap; the GE never knows the cap (UVeloxHealthSet still clamps to HealthHardCap).
 */
UCLASS()
class VELOX_API UVeloxGE_Heal : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVeloxGE_Heal();
};

/** Instant; Additive on Armor, magnitude SetByCaller.Armor (armor pickups; the gain is computed by the pickup rule). */
UCLASS()
class VELOX_API UVeloxGE_ArmorGain : public UGameplayEffect
{
	GENERATED_BODY()
public:
	UVeloxGE_ArmorGain();
};

/** Parameters of one damage application (hitscan hit, projectile direct/splash, world damage). */
struct FVeloxDamageSpecParams
{
	/** Raw damage per hit / pellet. */
	int32 Damage = 0;
	int32 PelletHits = 1;
	/** Damage type, Velox.Damage.<Weapon>. Velox.Damage.Self is added when bSelf. */
	FGameplayTag DamageTag;
	/** Knockback direction (normalised by the context); zero = no knockback. */
	FVector Direction = FVector::ZeroVector;
	FVector HitLocation = FVector::ZeroVector;
	float KnockbackScale = 1.f;
	/**
	 * Outgoing damage scale applied by MakeDamageSpec to Damage (Q3 `damage *= s_quadFactor`, truncated): the shooter's
	 * Quad factor for hitscan (ADR 0004 section 3). Projectiles scale their damage at the explosion and pass 1.
	 */
	float DamageScale = 1.f;
	uint8 ShotSeq = 0;
	bool bSelf = false;
	bool bRadius = false;
	bool bNoArmor = false;
	bool bNoKnockback = false;
	/** Projectile / weapon actor; defaults to the source avatar. */
	AActor* Causer = nullptr;
};

namespace VeloxEffects
{
	/** GE_Damage from SourceASC (the attacker; for world damage the victim's own ASC with bSelf = false). */
	VELOX_API FGameplayEffectSpecHandle MakeDamageSpec(const UAbilitySystemComponent* SourceASC, const FVeloxDamageSpecParams& Params);

	/** GE_SpawnInit with health/armor/ammo from Settings (asset or class defaults). */
	VELOX_API FGameplayEffectSpecHandle MakeSpawnInitSpec(const UAbilitySystemComponent* ASC, const UVeloxCombatSettings& Settings);

	/** GE_AmmoCost: -Cost on AmmoTag's attribute, 0 on the others. */
	VELOX_API FGameplayEffectSpecHandle MakeAmmoCostSpec(const UAbilitySystemComponent* ASC, const FGameplayTag& AmmoTag, int32 Cost);

	/** GE_AmmoCost with a positive magnitude: +Amount on AmmoTag's attribute (ammo / weapon pickups, ADR 0004 section 2). */
	VELOX_API FGameplayEffectSpecHandle MakeAmmoGainSpec(const UAbilitySystemComponent* ASC, const FGameplayTag& AmmoTag, int32 Amount);

	/** GE_Heal: +Amount health (the pickup rule's gain). */
	VELOX_API FGameplayEffectSpecHandle MakeHealSpec(const UAbilitySystemComponent* ASC, int32 Amount);

	/** GE_ArmorGain: +Amount armor (the pickup rule's gain). */
	VELOX_API FGameplayEffectSpecHandle MakeArmorGainSpec(const UAbilitySystemComponent* ASC, int32 Amount);

	/** GE_WeaponSwitching / GE_SpawnProtection (any HasDuration GE reading SetByCaller.Duration). */
	VELOX_API FGameplayEffectSpecHandle MakeDurationSpec(const UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> EffectClass, float Duration);

	/**
	 * Velox.State.MatchFrozen (intermission, ADR 0004 section 5). UVeloxGE_MatchFrozen and the native tag are owned by task
	 * 05 (VeloxMatchTags::State_MatchFrozen, lead decision 2026-10-29); resolved by name so the damage execution, the
	 * regeneration and the fire abilities compile before 05 is merged. Invalid (nothing frozen) while the tag is not
	 * registered. After the merge this may return VeloxMatchTags::State_MatchFrozen directly.
	 */
	VELOX_API FGameplayTag GetMatchFrozenTag();
}
