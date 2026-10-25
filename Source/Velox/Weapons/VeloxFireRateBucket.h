// Velox - server fire-rate validation (ADR 0003 section 5, B19): a token bucket plus a minimum spacing per weapon
// instance on server receive time. World-free, unit-tested in Velox.Combat.FireRateBucket.
//
// Bucket: refill 1 / FireInterval tokens per second, capacity FireRateBurstAllowance (>= 1), full on first use. A shot
// costs one token. Receive-time jitter of up to (Capacity - 1) x FireInterval is absorbed; the long-term rate is bounded
// exactly by 1 / FireInterval (a client firing faster runs dry after the burst and every further over-rate shot is
// rejected).
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
