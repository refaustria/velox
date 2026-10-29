// Velox - server fire-rate validation (ADR 0003 section 5, B19): a token bucket plus a minimum spacing per weapon
// instance on server receive time. World-free, unit-tested in Velox.Combat.FireRateBucket.
//
// Bucket: refill 1 / FireInterval tokens per second, capacity CapacityFor(FireInterval) (>= 1), full on first use. A
// shot costs one token. Receive-time delay spread of up to (Capacity - 1) x FireInterval is absorbed (the bucket is a
// GCRA with tolerance (Capacity - 1) x FireInterval: a client firing exactly at the rate is never rejected while every
// shot's delay lies within that window of the others'); the long-term rate is bounded exactly by 1 / FireInterval (a
// client firing faster runs dry after the burst and every further over-rate shot is rejected). Task 08b (D3): the
// capacity is sized in time, max(FireRateBurstAllowance, 1 + FireRateJitterToleranceSeconds / FireInterval): a fixed
// 3 tokens gave the MG 200 ms, less than one reliable resend (~RTT + 80 ms), and sustained fire never refills it.
// Spacing (B19): the bucket alone admits Capacity simultaneous shots after any idle (three rails in one frame). A shot
// received less than MinSpacing = max(0, Interval - FireRateSpacingTolerance) after the last accepted one is rejected;
// rail 1.25 s, SG 0.75 s, RL 0.55 s, MG/PG/LG 0 (bucket only). A rejected shot costs nothing.

#pragma once

#include "CoreMinimal.h"

enum class EVeloxFireRateResult : uint8
{
	Accepted,
	/** Bucket empty (long-term rate exceeded). */
	Rate,
	/** Too soon after the last accepted shot (burst). */
	Spacing,
};

struct FVeloxFireRateBucket
{
	double Tokens = 0.0;
	double LastTime = 0.0;
	double LastAcceptedTime = 0.0;
	bool bInitialised = false;
	bool bHasAccepted = false;

	void Reset()
	{
		bInitialised = false;
		bHasAccepted = false;
	}

	/** D3: token capacity of a weapon, max(BurstAllowance, 1 + JitterToleranceSeconds / Interval), at least 1. */
	static float CapacityFor(float Interval, float BurstAllowance, float JitterToleranceSeconds)
	{
		const float FromTime = Interval > 0.f ? 1.f + FMath::Max(JitterToleranceSeconds, 0.f) / Interval : 1.f;
		return FMath::Max3(BurstAllowance, FromTime, 1.f);
	}

	/** B19: the minimum receive-time spacing of a weapon. */
	static float MinSpacingFor(float Interval, float SpacingTolerance) { return FMath::Max(Interval - FMath::Max(SpacingTolerance, 0.f), 0.f); }

	/** A shot received at Now: Accepted (one token spent), or why not. Interval <= 0: unlimited. */
	EVeloxFireRateResult Consume(double Now, float Interval, float Capacity, float MinSpacing)
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
		// Tolerances only for float accumulation of exact-rate arrivals; a real violation is short by >= 1e-3 (s / tokens).
		if (bHasAccepted && MinSpacing > 0.f && Now - LastAcceptedTime < static_cast<double>(MinSpacing) - 1e-6)
		{
			return EVeloxFireRateResult::Spacing;
		}
		if (Tokens >= 1.0 - 1e-6)
		{
			Tokens = FMath::Max(Tokens - 1.0, 0.0);
			LastAcceptedTime = bHasAccepted ? FMath::Max(LastAcceptedTime, Now) : Now;
			bHasAccepted = true;
			return EVeloxFireRateResult::Accepted;
		}
		return EVeloxFireRateResult::Rate;
	}

	/** Consume() == Accepted. */
	bool TryConsume(double Now, float Interval, float Capacity, float MinSpacing)
	{
		return Consume(Now, Interval, Capacity, MinSpacing) == EVeloxFireRateResult::Accepted;
	}
};
