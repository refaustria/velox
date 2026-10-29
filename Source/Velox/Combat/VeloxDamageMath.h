// Velox - Q3 damage math (ADR 0003 section 2). World-free: the single implementation of Q3 G_Damage / CheckArmor /
// G_RadiusDamage, used by UVeloxDamageExecution, the projectile (task 05) and the client's self-knockback prediction.
// Integer semantics: attributes are floats holding integers, every quantity here is an int32 except speeds.

#pragma once

#include "CoreMinimal.h"
#include "Combat/VeloxHitMath.h"

/** Tunables of the damage math, built from UVeloxCombatSettings (cvar overrides applied there). */
struct FVeloxCombatParams
{
	/** Q3 ARMOR_PROTECTION 0.66 as an integer percent: share of the (post-self-scale) damage the armor absorbs, rounded up. */
	int32 ArmorProtectionPercent = 66;
	/** Q3 halves self damage (integer truncation). */
	float SelfDamageScale = 0.5f;
	/** Vanilla Q3 has full self knockback. */
	float SelfKnockbackScale = 1.0f;
	/** cm/s per point of raw damage: Q3 g_knockback 1000 / mass 200 = 5 ups x UnitScale 2.5. */
	float KnockbackSpeedPerDamage = 12.5f;
	/** Q3 caps the knockback damage at 200. */
	int32 MaxKnockbackDamage = 200;
};

struct FVeloxDamageInput
{
	/** Raw damage of one hit (one pellet). */
	int32 Damage = 0;
	/** Victim armor before the hit. */
	int32 Armor = 0;
	/** Victim is the attacker (half damage, SelfKnockbackScale). */
	bool bSelf = false;
	/** Q3 DAMAGE_NO_ARMOR. */
	bool bNoArmor = false;
	/** Q3 DAMAGE_NO_KNOCKBACK (also set when the hit has no direction). */
	bool bNoKnockback = false;
	/** Spawn protection: no health/armor loss, no knockback. */
	bool bProtected = false;
	/** Per-weapon knockback scale (UVeloxWeaponDefinition::KnockbackScale). */
	float KnockbackScale = 1.0f;
};

struct FVeloxDamageResult
{
	int32 HealthLoss = 0;
	int32 ArmorLoss = 0;
	/** cm/s, before direction. */
	float KnockbackSpeed = 0.f;

	FVeloxDamageResult& operator+=(const FVeloxDamageResult& Other)
	{
		HealthLoss += Other.HealthLoss;
		ArmorLoss += Other.ArmorLoss;
		KnockbackSpeed += Other.KnockbackSpeed;
		return *this;
	}
};

struct FVeloxDamageMath
{
	/**
	 * One hit, Q3 order: knockback from the raw damage (capped, scaled), then self damage x SelfDamageScale (truncated),
	 * minimum 1 point for every hit (also a zero-point radius hit, which then has no knockback: ADR 0003 B3), then armor
	 * save = min(Armor, Damage, ceil(Damage x Pct / 100)) in exact integer arithmetic unless bNoArmor (B2),
	 * HealthLoss = Damage - save. bProtected zeroes everything; a negative Damage is rejected.
	 */
	static FVeloxDamageResult Apply(const FVeloxDamageInput& In, const FVeloxCombatParams& Params)
	{
		FVeloxDamageResult Out;
		if (In.bProtected || In.Damage < 0)
		{
			return Out;
		}

		if (!In.bNoKnockback)
		{
			const int32 KnockbackDamage = FMath::Min(In.Damage, Params.MaxKnockbackDamage);
			Out.KnockbackSpeed = static_cast<float>(KnockbackDamage) * Params.KnockbackSpeedPerDamage * In.KnockbackScale * (In.bSelf ? Params.SelfKnockbackScale : 1.f);
			Out.KnockbackSpeed = FMath::Max(Out.KnockbackSpeed, 0.f);
		}

		int32 Damage = In.Damage;
		if (In.bSelf)
		{
			Damage = FMath::FloorToInt32(static_cast<float>(Damage) * Params.SelfDamageScale);
		}
		Damage = FMath::Max(Damage, 1);

		int32 Save = 0;
		if (!In.bNoArmor && In.Armor > 0)
		{
			// Exact integer ceiling of Damage x Pct / 100 (Q3 ceil(damage * 0.66)); no float, no tolerance.
			const int32 Pct = FMath::Clamp(Params.ArmorProtectionPercent, 0, 100);
			Save = (Damage * Pct + 99) / 100;
			Save = FMath::Clamp(Save, 0, FMath::Min(In.Armor, Damage));
		}

		Out.ArmorLoss = Save;
		Out.HealthLoss = Damage - Save;
		return Out;
	}

	/** Pellet weapons apply each pellet as its own hit (Q3 rounding: 11 x 10 against 100 armor costs 77 armor, not 67). */
	static FVeloxDamageResult ApplyPellets(const FVeloxDamageInput& In, int32 PelletHits, const FVeloxCombatParams& Params)
	{
		FVeloxDamageResult Total;
		FVeloxDamageInput Pellet = In;
		for (int32 Index = 0; Index < PelletHits; ++Index)
		{
			const FVeloxDamageResult One = Apply(Pellet, Params);
			Total += One;
			Pellet.Armor = FMath::Max(Pellet.Armor - One.ArmorLoss, 0);
		}
		return Total;
	}

	/** Q3 G_RadiusDamage: (int)(Damage x (1 - Dist / Radius)), 0 at or outside the radius. */
	static int32 RadiusPoints(int32 Damage, float Dist, float Radius)
	{
		if (Radius <= 0.f || Dist >= Radius || Damage <= 0)
		{
			return 0;
		}
		return FMath::TruncToInt32(static_cast<float>(Damage) * (1.f - FMath::Max(Dist, 0.f) / Radius));
	}

	/** Distance from Origin to the surface of a vertical capsule (0 inside). */
	static float ClosestCapsuleDistance(const FVector& Origin, const FVector& Centre, float Radius, float HalfHeight)
	{
		const double SegmentHalf = FMath::Max(static_cast<double>(HalfHeight - Radius), 0.0);
		const double Z = FMath::Clamp(Origin.Z, Centre.Z - SegmentHalf, Centre.Z + SegmentHalf);
		const FVector Closest(Centre.X, Centre.Y, Z);
		return FMath::Max(static_cast<float>(FVector::Dist(Origin, Closest)) - Radius, 0.f);
	}

	/**
	 * ADR 0003 section 2: splash distance to a hit shape. M2 shapes are vertical capsules and forward to the overload
	 * above; a tilted shape (M5 per-bone) uses the general segment distance of FVeloxHitMath.
	 */
	static float ClosestCapsuleDistance(const FVector& Origin, const FVeloxHitShape& Shape)
	{
		if (Shape.Axis.Equals(FVector::UpVector, 1e-4))
		{
			return ClosestCapsuleDistance(Origin, Shape.Centre, Shape.Radius, Shape.HalfHeight);
		}
		return static_cast<float>(FVeloxHitMath::PointShapeDistance(Origin, Shape));
	}

	/** Q3: dir = target - origin, dir.z += 24 u (ZOffset in cm), normalised. Zero if degenerate. */
	static FVector RadiusKnockbackDir(const FVector& Origin, const FVector& TargetCentre, float ZOffset)
	{
		FVector Dir = TargetCentre - Origin;
		Dir.Z += ZOffset;
		return Dir.GetSafeNormal();
	}
};
