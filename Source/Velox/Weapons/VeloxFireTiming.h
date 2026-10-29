// Velox - world-free fire timing decisions (ADR 0003 section 5, amendments B18, B19 and B37). Unit-tested in
// Velox.Combat.FireTiming; used by UVeloxGameplayAbility_Fire and UVeloxGameplayAbility_WeaponSwitch on the shooter's
// machine and on the server.

#pragma once

#include "CoreMinimal.h"

namespace VeloxFireTiming
{
	/** Rounding slack of the one-frame comparison (world time is a double sum of float frame deltas); not a bank. */
	inline constexpr double CarryEpsilonSeconds = 1.0e-6;

	/**
	 * B19, Q3 PM_Weapon: the next allowed shot after a shot fired at Now. A shot within one frame of its schedule
	 * (Now - Next <= FrameDt) keeps the schedule, so held fire runs at exactly 1 / Interval on any frame rate; a later
	 * shot restarts from Now, so no more than one frame can ever be banked (no double tap after a pause).
	 * A local-rhythm rule (shooter's machine: client, listen host, bots); the server never runs it on receive time (B37).
	 */
	inline double AdvanceNextFireTime(double Next, double Now, double Interval, double FrameDt)
	{
		return (Now - Next <= FMath::Max(FrameDt, 0.0) + CarryEpsilonSeconds) ? Next + Interval : Now + Interval;
	}

	/**
	 * B37: the server's refire wait for a remote shooter's switch: max(0, LastAccepted + Interval - Now), where
	 * LastAccepted is the receive time of the weapon's last accepted shot (very negative before the first one -> 0).
	 * Bunched resent shots all set the same receive time, so the wait never exceeds one Interval.
	 */
	inline double RemoteRefireWait(double LastAccepted, double Interval, double Now)
	{
		return FMath::Max(LastAccepted + FMath::Max(Interval, 0.0) - Now, 0.0);
	}

	/**
	 * B18 (tolerance per B37): may a fire ability activate with respect to a weapon switch?
	 * Shooter's machine (bLocal): only once its own switch has ended (Now >= SwitchEndTime, local world time; 0 = none).
	 * Server for a remote shooter: when the server's GE_WeaponSwitching has at most Tolerance seconds left
	 * (SwitchRemaining = 0 without the effect); its timeline is shifted by the same latency as the shot. Tolerance is the
	 * one resend tolerance FireRateJitterToleranceSeconds (0.5 s): reliable in-order delivery bounds the skew between the
	 * switch RPC and a later fire RPC by the resend compression (RTT + 80 ms, 0.33 s at the ping limit), as for B32.
	 */
	inline bool IsSwitchFireAllowed(bool bLocal, double Now, double SwitchEndTime, double SwitchRemaining, double Tolerance)
	{
		return bLocal ? Now >= SwitchEndTime : SwitchRemaining <= FMath::Max(Tolerance, 0.0);
	}

	/**
	 * ADR 0004 section 3, Haste (Q3 PM_Weapon: addTime /= 1.3): the fire interval while Haste is active, Interval /
	 * HasteFireRateScale. The shooter's machine passes its move-timeline Haste state (the same move stamps its movement
	 * uses), the server for a remote shooter its own Haste tag (the bucket refill rate and capacity, B32); the <= RTT
	 * disagreement at the edges lies inside FireRateJitterToleranceSeconds. A scale <= 0 is ignored.
	 */
	inline double HasteFireInterval(double Interval, bool bHaste, double HasteFireRateScale)
	{
		return bHaste && HasteFireRateScale > 0.0 ? Interval / HasteFireRateScale : Interval;
	}
}
