// Velox - one weapon as data (ADR 0003 section 4). Assets: /Game/Weapons/DA_Weapon_<Name> (bootstrap script), Q3
// vanilla numbers at UnitScale 2.5 as the initial tuning. Nothing weapon-specific is hard-coded in the abilities.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"

#include "VeloxWeaponDefinition.generated.h"

class AVeloxProjectile;
class UVeloxGameplayAbility_Fire;

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Weapon Definition"))
class VELOX_API UVeloxWeaponDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Seconds per shot as a positive number (FireInterval clamped to 1 ms). */
	float GetSafeFireInterval() const { return FMath::Max(FireInterval, 0.001f); }

	/** Single-ray weapons trace one direction (MG with its Q3 spread, LG, RG); the shotgun traces PelletCount. */
	bool IsPelletWeapon() const { return PelletCount > 1; }

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon", Meta = (Categories = "Velox.Weapon"))
	FGameplayTag WeaponTag;

	/** Q3 weapon slot (MG 2, SG 3, RL 5, LG 6, RG 7, PG 8; 1/4/9 reserved). InputTag.Weapon.Slot<N> selects it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon", Meta = (ClampMin = "1", ClampMax = "9"))
	int32 Slot = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon")
	FText DisplayName;

	/** Granted by the inventory with SourceObject = this definition. Null: owned and selectable, cannot fire. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapon")
	TSubclassOf<UVeloxGameplayAbility_Fire> FireAbility;

	/** Seconds between shots (Q3 weapon time). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Fire", Meta = (ClampMin = "0.001", Units = "s"))
	float FireInterval = 0.1f;

	/** Damage per hit; per pellet for the shotgun, direct hit for projectiles. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Fire", Meta = (ClampMin = "0"))
	int32 Damage = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Fire", Meta = (ClampMin = "1"))
	int32 PelletCount = 1;

	/** Q3 spread: square pattern half angle for pellets (SG 700/8192), random disc for single rays (MG 200/8192). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Fire", Meta = (ClampMin = "0", Units = "deg"))
	float SpreadAngleDeg = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Fire", Meta = (ClampMin = "0", Units = "cm"))
	float RangeCm = 20480.f;

	/** Rail: the ray continues through up to MaxPenetrations pawns and stops at world geometry. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Fire")
	bool bPenetratesPawns = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Fire", Meta = (ClampMin = "1"))
	int32 MaxPenetrations = 1;

	/** Lightning gun: every shot also keeps the looping BeamCueTag alive on the server. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Fire")
	bool bBeam = false;

	/** Projectile weapons: the class the server spawns and the owning client fakes (UVeloxGameplayAbility_FireProjectile). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Projectile")
	TSubclassOf<AVeloxProjectile> ProjectileClass;

	/** Collision sphere radius of the projectile (ADR 0003 section 7: 2 cm); also the self-splash match tolerance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Projectile", Meta = (ClampMin = "0", Units = "cm"))
	float ProjectileRadiusCm = 2.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Projectile", Meta = (ClampMin = "0", Units = "cm/s"))
	float ProjectileSpeed = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Projectile", Meta = (ClampMin = "0", Units = "s"))
	float ProjectileLifetime = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Projectile", Meta = (ClampMin = "0"))
	int32 SplashDamage = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Projectile", Meta = (ClampMin = "0", Units = "cm"))
	float SplashRadiusCm = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Damage", Meta = (ClampMin = "0"))
	float KnockbackScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Damage", Meta = (Categories = "Velox.Damage"))
	FGameplayTag DamageTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ammo", Meta = (Categories = "Velox.Ammo"))
	FGameplayTag AmmoTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ammo", Meta = (ClampMin = "0"))
	int32 AmmoPerShot = 1;

	/** Ammo given with a weapon pickup (M3) and by AddWeapon callers that pass no amount. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ammo", Meta = (ClampMin = "0"))
	int32 PickupAmmo = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cues", Meta = (Categories = "GameplayCue"))
	FGameplayTag FireCueTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cues", Meta = (Categories = "GameplayCue"))
	FGameplayTag ImpactCueTag;

	/** Looping cue while the beam is held (LG only); empty for other weapons. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Cues", Meta = (Categories = "GameplayCue"))
	FGameplayTag BeamCueTag;
};
