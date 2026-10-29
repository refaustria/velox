// Velox - server fire-rate validation (ADR 0003 section 5, B32; supersedes B19's server paragraph): one token bucket
// per weapon instance on server receive time. World-free, unit-tested in Velox.Combat.FireRateBucket.
//
// Bucket: refill 1 / FireInterval tokens per second, capacity CapacityFor(FireInterval, Tol) = 1 + Tol / FireInterval
// (>= 1), full on first use. A shot costs one token; a rejected shot costs nothing. The bucket is a GCRA with tolerance
// Tol = (Capacity - 1) x FireInterval: a client firing exactly at the rate is never rejected while every shot's receive
// delay lies within Tol of the others' (a reliable resend compresses by about RTT + 80 ms, 330 ms at the 250 ms RTT
// limit, so Tol 0.5 s covers one loss there), and after a full bucket two shots cannot be accepted closer than
// FireInterval - Tol, which is the minimum spacing B19 patched in separately (that rule rejected honest rails after one
// lost packet at the ping limit and is gone). The long-term rate is bounded exactly by 1 / FireInterval. Capacities at
// Tol 0.5 s: MG 6, LG 11, SG 1.5, RL 1.625, RG 1.333 (two rails 1.0 s apart once, then 1.5 s on average).

#pragma once

#include "CoreMinimal.h"

enum class EVeloxFireRateResult : uint8
{
	Accepted,
	/** Bucket empty (long-term rate exceeded beyond the tolerance). */
	Rate,
};

struct FVeloxFireRateBucket
{
	double Tokens = 0.0;
	double LastTime = 0.0;
	bool bInitialised = false;

	void Reset()
	{
		bInitialised = false;
	}

	/** B32: token capacity of a weapon, 1 + JitterToleranceSeconds / Interval, at least 1 (Interval <= 0: 1). */
	static float CapacityFor(float Interval, float JitterToleranceSeconds)
	{
		return Interval > 0.f ? 1.f + FMath::Max(JitterToleranceSeconds, 0.f) / Interval : 1.f;
	}

	/** A shot received at Now: Accepted (one token spent), or why not. Interval <= 0: unlimited. */
	EVeloxFireRateResult Consume(double Now, float Interval, float Capacity)
	{
		const double Cap = FMath::Max(static_cast<double>(Capacity), 1.0);
		if (!bInitialised)
		{
			Tokens = Cap;
			LastTime = Now;
			bInitialised = true;
		}
		else if (Interval > 0.f)
		{
			Tokens = FMath::Min(Cap, Tokens + FMath::Max(Now - LastTime, 0.0) / static_cast<double>(Interval));
			LastTime = FMath::Max(LastTime, Now);
		}
		if (Interval <= 0.f)
		{
			return EVeloxFireRateResult::Accepted;
		}
		// Tolerance only for float accumulation of exact-rate arrivals; a real violation is short by >= 1e-3 tokens.
		if (Tokens >= 1.0 - 1e-6)
		{
			Tokens = FMath::Max(Tokens - 1.0, 0.0);
			return EVeloxFireRateResult::Accepted;
		}
		return EVeloxFireRateResult::Rate;
	}

	/** Consume() == Accepted. */
	bool TryConsume(double Now, float Interval, float Capacity)
	{
		return Consume(Now, Interval, Capacity) == EVeloxFireRateResult::Accepted;
	}
};
