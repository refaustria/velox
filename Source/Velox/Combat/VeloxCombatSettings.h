// Velox - combat tunables (ADR 0003 section 1). Asset: /Game/Data/DA_CombatSettings_Default (bootstrap script).
// Tunables never live in gameplay effects: they reach the GEs as SetByCaller magnitudes at apply time.

#pragma once

#include "AttributeSet.h"
#include "Combat/VeloxDamageMath.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "VeloxCombatSettings.generated.h"

/** One ammo type: its tag (SetByCaller key and weapon reference), its attribute on UVeloxAmmoSet and its cap. */
USTRUCT(BlueprintType)
struct FVeloxAmmoTypeInfo
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ammo", Meta = (Categories = "Velox.Ammo"))
	FGameplayTag Tag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ammo")
	FGameplayAttribute Attribute;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ammo", Meta = (ClampMin = "0"))
	int32 MaxAmount = 200;

	/** Ammo given by GE_SpawnInit (the M2 spawn loadout; task 04/06 replaces it with UVeloxCombatSettings::SpawnLoadout). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ammo", Meta = (ClampMin = "0"))
	int32 SpawnAmount = 0;
};

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Combat Settings"))
class VELOX_API UVeloxCombatSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVeloxCombatSettings();

	/** Damage-math parameters; non-shipping `velox.combat.<Field>` cvars override the asset values. */
	FVeloxCombatParams BuildCombatParams() const;

	/** The ammo row for Tag or Attribute; null if unknown. */
	const FVeloxAmmoTypeInfo* FindAmmoType(const FGameplayTag& Tag) const;
	const FVeloxAmmoTypeInfo* FindAmmoType(const FGameplayAttribute& Attribute) const;

	/** Asset if non-null, else the class defaults (identical to the ADR table), so world-free code always has values. */
	static const UVeloxCombatSettings& GetOrDefault(const UVeloxCombatSettings* Settings);

	// Spawn / attributes (Q3 values in cm).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Health")
	float SpawnHealth = 125.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Health")
	float MaxHealth = 100.f;

	/** Health is clamped to [0, HealthHardCap] (megahealth stacks up to here). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Health")
	float HealthHardCap = 200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Health")
	float SpawnArmor = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Health")
	float MaxArmor = 200.f;

	// Damage math.
	/** Q3 ARMOR_PROTECTION 0.66 as an integer percent (ADR 0003 B2): armor save = ceil(Damage x Pct / 100). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Damage", Meta = (ClampMin = "0", ClampMax = "100"))
	int32 ArmorProtectionPercent = 66;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Damage")
	float SelfDamageScale = 0.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Damage")
	float SelfKnockbackScale = 1.0f;

	/** cm/s of knockback per point of raw damage (Q3 g_knockback 1000 / mass 200 = 5 ups). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Damage")
	float KnockbackSpeedPerDamage = 12.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Damage")
	float MaxKnockbackDamage = 200.f;

	/** Q3 G_RadiusDamage lifts the knockback direction by 24 u. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Damage")
	float RadiusKnockbackZOffset = 60.f;

	// Weapons (task 04).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons")
	float WeaponDropTime = 0.2f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons")
	float WeaponRaiseTime = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons")
	bool bSwitchWaitsForRefire = true;

	// Lag compensation (task 03).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|LagCompensation")
	float MaxRewindSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|LagCompensation")
	float HistoryWindowSeconds = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|LagCompensation")
	float MaxOriginErrorCm = 75.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|LagCompensation")
	float FireRateBurstAllowance = 3.f;

	// Projectiles (task 05).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Projectiles")
	float ProjectileForwardPredictionMax = 0.1f;

	// Death / respawn (task 06).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn")
	float MinRespawnDelay = 1.7f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn")
	float ForceRespawnDelay = 20.f;

	/** ADR 0003 open question 2: Q3 has none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn")
	float SpawnProtectionSeconds = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn")
	bool bSpawnProtectionEndsOnFire = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn")
	float CorpseLifetime = 3.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn")
	float SpawnAvoidRadiusCm = 500.f;

	/** The six ammo types (attribute, cap, spawn amount). Filled with the Q3 defaults by the constructor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ammo")
	TArray<FVeloxAmmoTypeInfo> AmmoTypes;

	// TODO(task 04): TObjectPtr<UVeloxWeaponTable> WeaponTable; TArray<FVeloxSpawnWeapon> SpawnLoadout.
};
