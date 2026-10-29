// Velox - world-free HUD formatting (ADR 0004 section 7): clock, powerup seconds, weapon bar rows, kill feed lines,
// phase banner, damage direction maths. Header-only and independent of any world, so Velox.UI.HUDFormat tests it directly;
// UVeloxHUDViewModel feeds it from the gameplay objects.

#pragma once

#include "CoreMinimal.h"
#include "GameModes/VeloxKillFeed.h"
#include "GameModes/VeloxMatchRules.h"

namespace VeloxHUDFormat
{
	// ------------------------------------------------------------------------------------------------------ health

	enum class EHealthBand : uint8
	{
		High,
		Mid,
		Low,
	};

	/** Q3 colour steps: >= HighMin white, >= MidMin yellow, else red. */
	inline EHealthBand HealthBand(int32 Health, int32 HighMin, int32 MidMin)
	{
		return Health >= HighMin ? EHealthBand::High : (Health >= MidMin ? EHealthBand::Mid : EHealthBand::Low);
	}

	// ------------------------------------------------------------------------------------------------------- clock

	/** "mm:ss". bCeil (a countdown, Q3 shows the second still running) rounds up, a count-up rounds down; negative is 0. */
	inline FString Clock(double Seconds, bool bCeil)
	{
		const int32 Total = Seconds <= 0.0 ? 0 : (bCeil ? FMath::CeilToInt32(Seconds) : FMath::FloorToInt32(Seconds));
		return FString::Printf(TEXT("%02d:%02d"), Total / 60, Total % 60);
	}

	/** The match state the clock reads (all times are server world times; Now = GetServerWorldTimeSeconds()). */
	struct FClockInput
	{
		EVeloxMatchPhase Phase = EVeloxMatchPhase::Warmup;
		double Now = 0.0;
		double PhaseStart = 0.0;
		/** 0 = open-ended. */
		double PhaseEnd = 0.0;
		double LiveStart = 0.0;
		int32 TimeLimitSeconds = 0;
	};

	/** The client's server time is not synced yet (a late joiner's first frames): the phase cannot have started in its future. */
	inline bool IsTimeSynced(const FClockInput& Input)
	{
		return Input.Now + 0.5 >= Input.PhaseStart;
	}

	/**
	 * Warmup: empty (the banner says WARMUP). Live: remaining time limit (ceil) or the elapsed time counting up when there is
	 * no limit. Countdown, timed overtime, PostMatch: time to PhaseEnd (ceil). Sudden death (Overtime without an end):
	 * counting up from the Live start. Unsynced time: "--:--".
	 */
	inline FString MatchClockText(const FClockInput& Input)
	{
		if (Input.Phase == EVeloxMatchPhase::Warmup)
		{
			return FString();
		}
		if (!IsTimeSynced(Input))
		{
			return TEXT("--:--");
		}
		switch (Input.Phase)
		{
		case EVeloxMatchPhase::Live:
			if (Input.TimeLimitSeconds > 0)
			{
				return Clock(Input.TimeLimitSeconds - (Input.Now - Input.LiveStart), true);
			}
			return Clock(Input.Now - Input.LiveStart, false);
		case EVeloxMatchPhase::Overtime:
			if (Input.PhaseEnd <= 0.0)
			{
				return Clock(Input.Now - Input.LiveStart, false);
			}
			return Clock(Input.PhaseEnd - Input.Now, true);
		default:
			return Input.PhaseEnd > 0.0 ? Clock(Input.PhaseEnd - Input.Now, true) : FString();
		}
	}

	/** Phase banner: WARMUP; the countdown seconds; FIGHT for FightSeconds after Live starts; OVERTIME / SUDDEN DEATH; the winner. */
	inline FString PhaseBanner(const FClockInput& Input, double FightSeconds, const FString& WinnerName)
	{
		switch (Input.Phase)
		{
		case EVeloxMatchPhase::Warmup:
			return TEXT("WARMUP");
		case EVeloxMatchPhase::Countdown:
			return IsTimeSynced(Input) && Input.PhaseEnd > 0.0 ? FString::FromInt(FMath::Max(FMath::CeilToInt32(Input.PhaseEnd - Input.Now), 1)) : FString();
		case EVeloxMatchPhase::Live:
			return IsTimeSynced(Input) && Input.Now - Input.LiveStart < FightSeconds ? FString(TEXT("FIGHT")) : FString();
		case EVeloxMatchPhase::Overtime:
			return Input.PhaseEnd > 0.0 ? FString(TEXT("OVERTIME")) : FString(TEXT("SUDDEN DEATH"));
		case EVeloxMatchPhase::PostMatch:
			return WinnerName.IsEmpty() ? FString(TEXT("DRAW")) : WinnerName + TEXT(" WINS");
		}
		return FString();
	}

	// -------------------------------------------------------------------------------------------------- powerups

	/** Q3 shows the whole seconds still running (ceil); 0 when the powerup is gone. */
	inline int32 PowerupSeconds(float RemainingSeconds)
	{
		return RemainingSeconds <= 0.f ? 0 : FMath::CeilToInt32(RemainingSeconds);
	}

	// ----------------------------------------------------------------------------------------------- weapon bar

	/** One weapon of the table as the bar sees it. Ammo < 0: no ammo shown (gauntlet, weapons without a cost). */
	struct FWeaponBarInput
	{
		int32 Index = INDEX_NONE;
		int32 Slot = 0;
		FString Name;
		int32 Ammo = -1;
	};

	struct FWeaponBarRow
	{
		int32 Index = INDEX_NONE;
		int32 Slot = 0;
		FString Name;
		int32 Ammo = -1;
		bool bCurrent = false;
		/** "5 Rocket Launcher 25" (slot digit, name, ammo). */
		FString Label;
	};

	/** The owned weapons (bit Index of OwnedMask) ordered by slot (ties by table index), the current one flagged. */
	inline TArray<FWeaponBarRow> WeaponBarRows(const TArray<FWeaponBarInput>& Weapons, uint16 OwnedMask, int32 CurrentIndex)
	{
		TArray<FWeaponBarRow> Rows;
		for (const FWeaponBarInput& Weapon : Weapons)
		{
			if (Weapon.Index < 0 || Weapon.Index >= 16 || (OwnedMask & (1u << Weapon.Index)) == 0)
			{
				continue;
			}
			FWeaponBarRow& Row = Rows.AddDefaulted_GetRef();
			Row.Index = Weapon.Index;
			Row.Slot = Weapon.Slot;
			Row.Name = Weapon.Name;
			Row.Ammo = Weapon.Ammo;
			Row.bCurrent = Weapon.Index == CurrentIndex;
			Row.Label = Weapon.Ammo >= 0 ? FString::Printf(TEXT("%d %s %d"), Weapon.Slot, *Weapon.Name, Weapon.Ammo) : FString::Printf(TEXT("%d %s"), Weapon.Slot, *Weapon.Name);
		}
		Rows.StableSort([](const FWeaponBarRow& A, const FWeaponBarRow& B) { return A.Slot != B.Slot ? A.Slot < B.Slot : A.Index < B.Index; });
		return Rows;
	}

	// ------------------------------------------------------------------------------------------------- kill feed

	/** "Velox.Damage.Rocket" -> "Rocket"; the text after the last '.'. */
	inline FString TagLeaf(const FString& TagName)
	{
		int32 Dot = INDEX_NONE;
		return TagName.FindLastChar(TEXT('.'), Dot) ? TagName.Mid(Dot + 1) : TagName;
	}

	/** The weapon label of a feed entry; empty for no tag and for the generic Self tag. */
	inline FString FeedWeaponLabel(const FVeloxKillFeedEntry& Entry)
	{
		if (!Entry.DamageTag.IsValid())
		{
			return FString();
		}
		const FString Leaf = TagLeaf(Entry.DamageTag.ToString());
		return Leaf.Equals(TEXT("Self")) ? FString() : Leaf;
	}

	/** One feed line as plain text: "Killer [Rocket] Victim", "Victim [Rocket] (suicide)". */
	inline FString FeedLine(const FVeloxKillFeedEntry& Entry)
	{
		const FString Weapon = FeedWeaponLabel(Entry);
		const FString Cause = Weapon.IsEmpty() ? FString() : FString::Printf(TEXT(" [%s]"), *Weapon);
		if (Entry.bSuicide)
		{
			return FString::Printf(TEXT("%s%s (suicide)"), *Entry.VictimName, *Cause);
		}
		return Weapon.IsEmpty() ? FString::Printf(TEXT("%s killed %s"), *Entry.KillerName, *Entry.VictimName) : FString::Printf(TEXT("%s%s %s"), *Entry.KillerName, *Cause, *Entry.VictimName);
	}

	/** Visible for the feed: the age (Now - entry time) is below the lifetime. A negative age (unsynced client time) counts as visible. */
	inline bool FeedEntryVisible(double Now, double EntryServerTime, double LifetimeSeconds)
	{
		return Now - EntryServerTime < LifetimeSeconds;
	}

	// ------------------------------------------------------------------------------------------ damage direction

	/** Quadrants of the damage indicator, clockwise from the view direction. */
	enum class EDamageQuadrant : uint8
	{
		Front = 0,
		Right = 1,
		Back = 2,
		Left = 3,
	};

	/** World yaw (degrees) of the horizontal direction From -> To; 0 when they coincide horizontally. */
	inline float YawFromTo(const FVector& From, const FVector& To)
	{
		const FVector2D Delta(To.X - From.X, To.Y - From.Y);
		return Delta.IsNearlyZero() ? 0.f : FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	}

	/** Which quadrant a damage source at WorldYaw is in for a viewer looking along ViewYaw (right = clockwise from above). */
	inline EDamageQuadrant QuadrantFor(float WorldYaw, float ViewYaw)
	{
		const float Relative = FMath::FindDeltaAngleDegrees(ViewYaw, WorldYaw);
		if (FMath::Abs(Relative) <= 45.f)
		{
			return EDamageQuadrant::Front;
		}
		if (FMath::Abs(Relative) > 135.f)
		{
			return EDamageQuadrant::Back;
		}
		return Relative > 0.f ? EDamageQuadrant::Right : EDamageQuadrant::Left;
	}

	/** 1 when the damage just happened, linearly to 0 after Duration; 0 for a future or expired record. */
	inline float DamageIntensity(double Age, double Duration)
	{
		return (Age < 0.0 || Duration <= 0.0 || Age >= Duration) ? 0.f : static_cast<float>(1.0 - Age / Duration);
	}
}
