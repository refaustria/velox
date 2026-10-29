// Velox - combat tunables (ADR 0003 section 1). Asset: /Game/Data/DA_CombatSettings_Default (bootstrap script).
// Tunables never live in gameplay effects: they reach the GEs as SetByCaller magnitudes at apply time.

#pragma once

#include "AttributeSet.h"
#include "Combat/VeloxDamageMath.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "VeloxCombatSettings.generated.h"

class UVeloxWeaponDefinition;
class UVeloxWeaponTable;

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
};

/** One weapon of the spawn loadout and the ammo it comes with (ADR 0003 B5: the only source of spawn ammo). */
USTRUCT(BlueprintType)
struct FVeloxSpawnWeapon
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons")
	TObjectPtr<const UVeloxWeaponDefinition> Definition;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons", Meta = (ClampMin = "0"))
	int32 Ammo = 0;
};

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Combat Settings"))
class VELOX_API UVeloxCombatSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UVeloxCombatSettings();

	/** Damage-math parameters; non-shipping `velox.combat.<Field>` cvars override the asset values. */
	FVeloxCombatParams BuildCombatParams() const;

	/** Regeneration parameters (ADR 0004 section 3). */
	FVeloxRegenParams BuildRegenParams() const;

	/** The ammo row for Tag or Attribute; null if unknown. */
	const FVeloxAmmoTypeInfo* FindAmmoType(const FGameplayTag& Tag) const;
	const FVeloxAmmoTypeInfo* FindAmmoType(const FGameplayAttribute& Attribute) const;

	/** Spawn ammo of AmmoTag: the sum of SpawnLoadout[].Ammo over the entries whose definition uses AmmoTag (B5). */
	int32 GetSpawnAmmo(const FGameplayTag& AmmoTag) const;

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

	/**
	 * ADR 0003 B18: the server accepts a remote shooter's shot while its GE_WeaponSwitching has at most this many seconds
	 * left (jitter and reliable-resend compression, <= 1 RTT at the 250 ms ping limit). The shooter's own machine gates
	 * on its local switch end exactly.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons", Meta = (ClampMin = "0", Units = "s"))
	float SwitchTimingToleranceSeconds = 0.25f;

	/** ADR 0003 B23: the LG beam cue stays this long after the last accepted shot (covers one lost packet's resend). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons", Meta = (ClampMin = "0", Units = "s"))
	float BeamKeepAliveSeconds = 0.25f;

	/** Fields above with the non-shipping `velox.combat.<Field>` cvars applied. */
	float GetSwitchTimingToleranceSeconds() const;
	float GetBeamKeepAliveSeconds() const;

	// Lag compensation (task 03).
	/**
	 * Oldest server pose a shot may rewind to (s), also the B25 projectile fire-stamp window. ADR 0003 B34: a hinted shot
	 * needs RTT + 20-40 ms of rewind, so 0.30 compensates the full 250 ms RTT limit (0.25 covered only ~210 ms).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|LagCompensation", Meta = (ClampMin = "0", Units = "s"))
	float MaxRewindSeconds = 0.30f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|LagCompensation")
	float HistoryWindowSeconds = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|LagCompensation")
	float MaxOriginErrorCm = 75.f;

	/**
	 * A hitbox moving farther than this between two history samples (cm) is a teleport / respawn: poses are not
	 * interpolated across it, the nearer-in-time pose is used (ADR 0003 B16). Legit movement is <= ~42 cm per 60 Hz sample.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|LagCompensation")
	float HitboxTeleportDistanceCm = 150.f;

	/** HitboxTeleportDistanceCm with the non-shipping `velox.combat.HitboxTeleportDistanceCm` cvar applied. */
	float GetHitboxTeleportDistanceCm() const;

	/**
	 * ADR 0003 B32: the one fire-rate tolerance (s), the receive-time delay variation the server bucket absorbs. Capacity
	 * per weapon is 1 + this / FireInterval (FVeloxFireRateBucket::CapacityFor): a token bucket of capacity C never
	 * rejects a client firing exactly at 1 / FireInterval whose shots arrive with a delay spread <= (C - 1) x FireInterval,
	 * and the spread of a reliable resend is about RTT + 80 ms (task 08 evidence). 0.5 s covers a single loss at the
	 * 250 ms RTT limit: MG 6, LG 11, SG 1.5, RL 1.625, RG 1.333 tokens. After a full bucket two shots are at least
	 * FireInterval - this apart (no separate spacing rule); a client firing at 2x the rate is rejected after ~this many
	 * seconds; the long-term rate stays exactly 1 / FireInterval.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|LagCompensation", Meta = (ClampMin = "0", Units = "s"))
	float FireRateJitterToleranceSeconds = 0.5f;

	/** FireRateJitterToleranceSeconds with the non-shipping `velox.combat.FireRateJitterToleranceSeconds` cvar applied. */
	float GetFireRateJitterToleranceSeconds() const;

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

	/** Spawn selection: starts within this distance of a living enemy are skipped while a farther free start exists. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn")
	float SpawnAvoidRadiusCm = 500.f;

	/** Damage of the telefrag a spawning player deals to the occupant of a taken start (Q3 G_KillBox: 100000). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn", Meta = (ClampMin = "1"))
	int32 TelefragDamage = 1000;

	/** Minimum interval between two respawn requests a waiting client sends while it holds fire/jump (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn", Meta = (ClampMin = "0.05", Units = "s"))
	float RespawnRequestIntervalSeconds = 0.25f;

	/** Death camera: distance behind the corpse (cm) and how far above its eye it looks down (degrees), Q3 shows the body. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn", Meta = (ClampMin = "0", Units = "cm"))
	float DeathCamDistanceCm = 220.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn", Meta = (ClampMin = "0", ClampMax = "80", Units = "deg"))
	float DeathCamPitchDegrees = 25.f;

	/** Death camera: the camera backs off walls by this much (cm). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn", Meta = (ClampMin = "0", Units = "cm"))
	float DeathCamWallOffsetCm = 12.f;

	/** Damage of the world kill (pits, FellOutOfWorld): Q3 kills outright. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn", Meta = (ClampMin = "1"))
	int32 EnvironmentKillDamage = 1000;

	/** A corpse that died in the air is dropped straight down by at most this much (cm) so it lies on the floor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn", Meta = (ClampMin = "0", Units = "cm"))
	float CorpseDropMaxCm = 3000.f;

	/** Corpse body colour = player colour times this (dark = dead, readability). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Respawn", Meta = (ClampMin = "0", ClampMax = "1"))
	float CorpseColorScale = 0.3f;

	/** Bright, team-less skins (ADR 0003 section 10): the game mode hands out the first unused entry, wrapping around. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Readability")
	TArray<FColor> PlayerColorPalette;

	/** HUD hit marker (Q3 server-confirmed): how long it shows after ClientConfirmHit, and its arm length (px at 1080p). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Readability", Meta = (ClampMin = "0", Units = "s"))
	float HitMarkerSeconds = 0.25f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Readability", Meta = (ClampMin = "0"))
	float HitMarkerSize = 14.f;

	/**
	 * ADR 0004 section 2: seconds a client-predicted pickup (hidden item, predicted weapon slot) waits for the replicated
	 * take before it is treated as refused and shown again. Two RTTs at the 250 ms ping limit.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Items", Meta = (ClampMin = "0", Units = "s"))
	float PickupPredictionTimeout = 0.5f;

	// Powerups (ADR 0004 section 3). Durations live on the item definitions (PowerupSeconds), never here.
	/** Quad Damage: raw damage of every hit times this at fire (Q3 g_quadfactor 3), knockback included. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Powerups", Meta = (ClampMin = "1"))
	float QuadDamageFactor = 3.f;

	/** Battle Suit: a non-radius hit's damage times this (Q3 0.5, truncated); radius hits cost nothing (knockback stays). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Powerups", Meta = (ClampMin = "0", ClampMax = "1"))
	float BattleSuitDirectDamageScale = 0.5f;

	/** Regeneration: health per second below max health (Q3 15). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Powerups", Meta = (ClampMin = "0"))
	int32 RegenHealthPerTick = 15;

	/** Regeneration: health per second from max health up to HealthHardCap (Q3 5). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Powerups", Meta = (ClampMin = "0"))
	int32 RegenOverTick = 5;

	/** Regeneration: the RegenHealthPerTick step stops at MaxHealth x this (Q3 1.1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Powerups", Meta = (ClampMin = "1"))
	float RegenSoftCapScale = 1.1f;

	/** Haste: fire interval divided by this (Q3 1.3); task 03 applies it (client rule and server bucket). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Powerups", Meta = (ClampMin = "1"))
	float HasteFireRateScale = 1.3f;

	/**
	 * Body tint per active powerup (vector parameter UVeloxCombatFeedbackComponent::PowerupTintParameter on the body
	 * material): Quad blue, Haste yellow, Regen red, Battle Suit green (readability over fidelity). Alpha 0 = no tint.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Readability", Meta = (Categories = "Velox.Powerup"))
	TMap<FGameplayTag, FLinearColor> PowerupTints;

	/** The six ammo types (attribute, cap). Filled with the Q3 defaults by the constructor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ammo")
	TArray<FVeloxAmmoTypeInfo> AmmoTypes;

	/** The weapon set; table index = weapon network id (ADR 0003 section 4). Class default: none (asset required). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons")
	TObjectPtr<const UVeloxWeaponTable> WeaponTable;

	/**
	 * Weapons and ammo of every spawn (Q3: machine gun with 100 bullets; L_CombatTest's mode lists all six). The first
	 * entry is selected. GE_SpawnInit sets the ammo, UVeloxWeaponInventoryComponent::ResetToLoadout the weapons.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons")
	TArray<FVeloxSpawnWeapon> SpawnLoadout;
};
