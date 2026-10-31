// Velox - world-free clamp of a spectator's synced view point (ADR 0004 C11 "Server-side limits").
//
// The owning client sends its free-fly view point with ServerSetSpectatorLocation (engine, unreliable, ~4 Hz throttle).
// The server keeps it as LastSpectatorSyncLocation, the view point for relevance and priority (C11 fact b). The clamp
// keeps that point inside the arena: hygiene, not security (server-side gating of what spectators receive is M6).

#pragma once

#include "CoreMinimal.h"

namespace VeloxSpectate
{
	/** Safety height above WorldSettings->KillZ below which no view point is accepted (C11: KillZ + 50). */
	inline constexpr double KillZMarginCm = 50.0;

	/** Bits of FClampResult::Reasons. */
	enum EClampReason : uint8
	{
		ClampNone = 0,
		ClampSpeed = 1 << 0,
		ClampBounds = 1 << 1,
		ClampKillZ = 1 << 2,
	};

	struct FClampResult
	{
		FVector Point = FVector::ZeroVector;
		/** EClampReason bits; 0 = the request was accepted unchanged. */
		uint8 Reasons = ClampNone;
		/** Distance between the request and the accepted point (cm). */
		double DistCm = 0.0;

		bool WasClamped() const { return Reasons != ClampNone; }
	};

	/** "speed", "bounds", "killz" or "speed+bounds"... for the clamp log line; "none" when unchanged. */
	inline FString ReasonsToString(uint8 Reasons)
	{
		FString Out;
		const auto Add = [&Out](const TCHAR* Name)
		{
			if (!Out.IsEmpty())
			{
				Out += TEXT("+");
			}
			Out += Name;
		};
		if (Reasons & ClampSpeed)
		{
			Add(TEXT("speed"));
		}
		if (Reasons & ClampBounds)
		{
			Add(TEXT("bounds"));
		}
		if (Reasons & ClampKillZ)
		{
			Add(TEXT("killz"));
		}
		return Out.IsEmpty() ? FString(TEXT("none")) : Out;
	}

	/**
	 * The point the server accepts for Requested.
	 *  1. Speed step: at most MaxStepSpeedCmS x Dt from LastAccepted (when set). Dt <= 0 allows no step (the caller floors
	 *     Dt at DeathSpectateSyncDtFloorSeconds, so this only guards a misuse); a non-finite request is replaced by
	 *     LastAccepted (or the box centre / origin).
	 *  2. Bounds: the nearest point of Bounds (already expanded by the margin); an invalid box is unbounded.
	 *  3. KillZ: Z raised to MinZ (KillZ + KillZMarginCm; pass -inf / lowest() for none).
	 * MaxStepSpeedCmS = DeathSpectateSpeedCmS x DeathSpectateSyncSpeedScale.
	 */
	inline FClampResult ClampViewPoint(const FVector& Requested, const TOptional<FVector>& LastAccepted, double Dt, const FBox& Bounds, double MinZ, double MaxStepSpeedCmS)
	{
		FClampResult Result;
		FVector P = Requested;
		if (P.ContainsNaN() || !FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y) || !FMath::IsFinite(P.Z))
		{
			P = LastAccepted.IsSet() ? LastAccepted.GetValue() : (Bounds.IsValid ? Bounds.GetCenter() : FVector::ZeroVector);
			Result.Reasons |= ClampBounds;
		}
		if (LastAccepted.IsSet())
		{
			const FVector From = LastAccepted.GetValue();
			const double MaxStep = FMath::Max(0.0, MaxStepSpeedCmS) * FMath::Max(0.0, Dt);
			const FVector Step = P - From;
			const double StepLen = Step.Size();
			if (StepLen > MaxStep + UE_KINDA_SMALL_NUMBER)
			{
				P = StepLen > UE_SMALL_NUMBER ? From + Step * (MaxStep / StepLen) : From;
				Result.Reasons |= ClampSpeed;
			}
		}
		if (Bounds.IsValid && !Bounds.IsInsideOrOn(P))
		{
			P = Bounds.GetClosestPointTo(P);
			Result.Reasons |= ClampBounds;
		}
		if (P.Z < MinZ)
		{
			P.Z = MinZ;
			Result.Reasons |= ClampKillZ;
		}
		Result.Point = P;
		Result.DistCm = Requested.ContainsNaN() ? 0.0 : FVector::Dist(Requested, P);
		return Result;
	}
}
