// Velox - world-free fire timing decisions (ADR 0003 section 5, amendments B18 and B19). Unit-tested in
// Velox.Combat.FireTiming; used by UVeloxGameplayAbility_Fire on the shooter's machine and on the server.

#pragma once

#include "CoreMinimal.h"

namespace VeloxFireTiming
{
	/**
	 * B19, Q3 PM_Weapon: the next allowed shot after a shot fired at Now. A shot within one frame of its schedule
	 * (Now - Next <= FrameDt) keeps the schedule, so held fire runs at exactly 1 / Interval on any frame rate; a later
	 * shot restarts from Now, so no more than one frame can ever be banked (no double tap after a pause).
	 */
	/** Rounding slack of the one-frame comparison (world time is a double sum of float frame deltas); not a bank. */
	inline constexpr double CarryEpsilonSeconds = 1.0e-6;

	inline double AdvanceNextFireTime(double Next, double Now, double Interval, double FrameDt)
	{
		return (Now - Next <= FMath::Max(FrameDt, 0.0) + CarryEpsilonSeconds) ? Next + Interval : Now + Interval;
	}

	/**
	 * B18: may a fire ability activate with respect to a weapon switch?
	 * Shooter's machine (bLocal): only once its own switch has ended (Now >= SwitchEndTime, local world time; 0 = none).
	 * Server for a remote shooter: when the server's GE_WeaponSwitching has at most Tolerance seconds left
	 * (SwitchRemaining = 0 without the effect); its timeline is shifted by the same latency as the shot, the tolerance
	 * covers jitter and reliable-resend compression.
	 */
	inline bool IsSwitchFireAllowed(bool bLocal, double Now, double SwitchEndTime, double SwitchRemaining, double Tolerance)
	{
		return bLocal ? Now >= SwitchEndTime : SwitchRemaining <= FMath::Max(Tolerance, 0.0);
	}

	/**
	 * ADR 0004 section 3, Haste (Q3 PM_Weapon: addTime /= 1.3): the fire interval while Haste is active, Interval /
	 * HasteFireRateScale. The shooter's machine passes its move-timeline Haste state (the same move stamps its movement
	 * uses), the server for a remote shooter its own Haste tag (the bucket refill rate and the spacing rule); the <= RTT
	 * disagreement at the edges lies inside FireRateJitterToleranceSeconds. A scale <= 0 is ignored.
	 */
	inline double HasteFireInterval(double Interval, bool bHaste, double HasteFireRateScale)
	{
		return bHaste && HasteFireRateScale > 0.0 ? Interval / HasteFireRateScale : Interval;
	}
}
