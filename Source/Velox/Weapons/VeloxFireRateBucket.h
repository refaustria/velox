// Velox - server fire-rate validation (ADR 0003 section 5): a token bucket per weapon instance on server receive time.
// World-free, unit-tested in Velox.Combat.FireRateBucket.
//
// Refill 1 / FireInterval tokens per second, capacity FireRateBurstAllowance (>= 1), full on first use. A shot costs one
// token. Receive-time jitter of up to (Capacity - 1) x FireInterval is absorbed; the long-term rate is bounded exactly by
// 1 / FireInterval (a client firing faster runs dry after the burst and every further over-rate shot is rejected).

#pragma once

#include "CoreMinimal.h"

struct FVeloxFireRateBucket
{
	double Tokens = 0.0;
	double LastTime = 0.0;
	bool bInitialised = false;

	void Reset() { bInitialised = false; }

	/** True (and one token spent) if a shot received at Now is within the rate. Interval <= 0: unlimited. */
	bool TryConsume(double Now, float Interval, float Capacity)
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
			return true;
		}
		// Tolerance only for float accumulation of exact-rate arrivals; a real over-rate shot is short by >= 1e-3 tokens.
		if (Tokens >= 1.0 - 1e-6)
		{
			Tokens = FMath::Max(Tokens - 1.0, 0.0);
			return true;
		}
		return false;
	}
};
