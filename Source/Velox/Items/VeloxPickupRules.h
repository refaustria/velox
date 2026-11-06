// Velox - Q3 pickup rules and the respawn timer, world-free (ADR 0004 section 2; Velox.Items.PickupRules / PickupTiming).
// Q3 1.32 BG_CanItemBeGrabbed and Pickup_Health / Pickup_Armor / Pickup_Ammo / Pickup_Weapon / Pickup_Powerup. The
// pickup actor fills an FVeloxPickupQuery from the taker's attributes and applies the result through GAS; nothing here
// knows about actors, components or the ability system.

#pragma once

#include "CoreMinimal.h"
#include "Items/VeloxItemTypes.h"

/** One taker against one item: the item's numbers and the taker's current values. */
struct FVeloxPickupQuery
{
	EVeloxItemType Type = EVeloxItemType::Health;
	/** Health / armor / ammo amount, or the weapon pickup's ammo (Q3 item quantity). Unused for powerups. */
	int32 Quantity = 0;
	EVeloxHealthCap HealthCap = EVeloxHealthCap::MaxHealth;

	float Health = 0.f;
	float MaxHealth = 0.f;
	float HealthHardCap = 0.f;
	float Armor = 0.f;
	float MaxArmor = 0.f;

	/** Ammo / weapon items: the taker's ammo of the item's ammo type and that type's cap. */
	int32 Ammo = 0;
	int32 MaxAmmo = 0;

	/** Powerups: seconds the item grants and seconds of the same powerup the taker still has. */
	float PowerupSeconds = 0.f;
	float PowerupRemaining = 0.f;
};

/** What a take grants. bGrab false = the item stays (Amount 0). */
struct FVeloxPickupGain
{
	bool bGrab = false;
	/** Health / armor / ammo points added (weapons: ammo added; may be 0 at the ammo cap, the weapon is still granted). */
	int32 Amount = 0;
	/** Powerups: the remaining seconds after the take (old remaining + item seconds, no cap). */
	float PowerupSeconds = 0.f;
};

/** Mode rules a pickup reads at take time (UVeloxGameModeDefinition, ADR 0004 section 5; defaults = FFA/Duel). */
struct FVeloxItemModeRules
{
	/** Scales RespawnSeconds of every non-weapon item. */
	float ItemRespawnScale = 1.f;
	/** Respawn of weapon pickups (Q3 g_weaponRespawn 5; team modes 30). */
	float WeaponRespawnSeconds = 5.f;
	/** Weapon-stay (CTF, M7): the weapon is granted and the item stays. Off in FFA/Duel. */
	bool bWeaponStay = false;
};

namespace VeloxPickupRules
{
	inline int32 ToPoints(float Value) { return FMath::RoundToInt32(Value); }

	/** The health cap of a health item for this taker. */
	inline int32 HealthCapFor(const FVeloxPickupQuery& Q)
	{
		return ToPoints(Q.HealthCap == EVeloxHealthCap::HardCap ? Q.HealthHardCap : Q.MaxHealth);
	}

	/** Q3 BG_CanItemBeGrabbed: health below its cap, armor below max, ammo below its cap; weapons and powerups always. */
	inline bool CanGrab(const FVeloxPickupQuery& Q)
	{
		switch (Q.Type)
		{
		case EVeloxItemType::Health:
			return ToPoints(Q.Health) < HealthCapFor(Q);
		case EVeloxItemType::Armor:
			return ToPoints(Q.Armor) < ToPoints(Q.MaxArmor);
		case EVeloxItemType::Ammo:
			return Q.Ammo < Q.MaxAmmo;
		case EVeloxItemType::Weapon:
		case EVeloxItemType::Powerup:
			return true;
		}
		return false;
	}

	/** Q3 weapon ammo top-up (Pickup_Weapon): Quantity - Have while below it, else +1; never past the cap. */
	inline int32 WeaponAmmoGain(int32 Have, int32 Quantity, int32 MaxAmmo)
	{
		const int32 Raw = Have < Quantity ? Quantity - Have : 1;
		return FMath::Clamp(Raw, 0, FMath::Max(MaxAmmo - Have, 0));
	}

	/** The grant of a take (gain clamped to the cap the rule names; the GEs never know caps). */
	inline FVeloxPickupGain Gain(const FVeloxPickupQuery& Q)
	{
		FVeloxPickupGain Result;
		if (!CanGrab(Q))
		{
			return Result;
		}
		Result.bGrab = true;
		switch (Q.Type)
		{
		case EVeloxItemType::Health:
			Result.Amount = FMath::Clamp(Q.Quantity, 0, HealthCapFor(Q) - ToPoints(Q.Health));
			break;
		case EVeloxItemType::Armor:
			Result.Amount = FMath::Clamp(Q.Quantity, 0, ToPoints(Q.MaxArmor) - ToPoints(Q.Armor));
			break;
		case EVeloxItemType::Ammo:
			Result.Amount = FMath::Clamp(Q.Quantity, 0, Q.MaxAmmo - Q.Ammo);
			break;
		case EVeloxItemType::Weapon:
			Result.Amount = WeaponAmmoGain(Q.Ammo, Q.Quantity, Q.MaxAmmo);
			break;
		case EVeloxItemType::Powerup:
			// Q3 Pickup_Powerup: powerups[tag] += quantity * 1000 on top of what is left.
			Result.PowerupSeconds = FMath::Max(Q.PowerupRemaining, 0.f) + FMath::Max(Q.PowerupSeconds, 0.f);
			break;
		}
		return Result;
	}

	/** Seconds until an item taken now respawns: weapons from the mode, everything else scaled by the mode. */
	inline double RespawnSecondsFor(EVeloxItemType Type, float ItemRespawnSeconds, const FVeloxItemModeRules& Rules)
	{
		if (Type == EVeloxItemType::Weapon)
		{
			return FMath::Max(static_cast<double>(Rules.WeaponRespawnSeconds), 0.0);
		}
		return FMath::Max(static_cast<double>(ItemRespawnSeconds) * FMath::Max(static_cast<double>(Rules.ItemRespawnScale), 0.0), 0.0);
	}

	/** Weapon-stay: a taken weapon item stays available for everyone else. */
	inline bool StaysAfterTake(EVeloxItemType Type, const FVeloxItemModeRules& Rules)
	{
		return Type == EVeloxItemType::Weapon && Rules.bWeaponStay;
	}

	/** Initial spawn delay from the definition's interval and a uniform Alpha in [0, 1] (Q3 powerups 45 + crandom() * 15). */
	inline double InitialDelayFor(const FFloatInterval& Interval, float Alpha)
	{
		const double Min = FMath::Max(static_cast<double>(Interval.Min), 0.0);
		const double Max = FMath::Max(static_cast<double>(Interval.Max), Min);
		return Min + (Max - Min) * FMath::Clamp(static_cast<double>(Alpha), 0.0, 1.0);
	}
}

/**
 * Server respawn timer of one pickup: pure arithmetic on server world time (doubles), so tests assert exact instants.
 * RespawnTime 0 = available; otherwise the item is unavailable while Now < RespawnTime.
 */
struct FVeloxPickupTimer
{
	double RespawnTime = 0.0;

	bool IsAvailable(double Now) const { return RespawnTime <= 0.0 || Now >= RespawnTime; }
	double TimeRemaining(double Now) const { return IsAvailable(Now) ? 0.0 : RespawnTime - Now; }

	/** Taken at Now; back after RespawnSeconds (0 = at once). */
	void Take(double Now, double RespawnSeconds) { RespawnTime = RespawnSeconds > 0.0 ? Now + RespawnSeconds : 0.0; }
	/** Level start / reset: first available after Delay (0 = at once). */
	void ScheduleInitial(double Now, double Delay) { RespawnTime = Delay > 0.0 ? Now + Delay : 0.0; }
	/** The respawn happened (the owner's timer fired). */
	void MarkRespawned() { RespawnTime = 0.0; }
	void Reset() { RespawnTime = 0.0; }
};
