// Velox - world-free view model motion (ADR 0006 section 4.6). Unit-tested in Velox.Character.ViewModelBob; used by
// UVeloxViewModelComponent. Lengths in cm, angles in degrees, offsets in the view model's frame (x forward, y right, z up).

#pragma once

#include "CoreMinimal.h"
#include "Core/VeloxViewModelSettings.h"

namespace VeloxViewModelMath
{
	/** A recoil impulse is below this fraction of its peak after RecoilReturnSeconds (the task asks for < 1 %). */
	inline constexpr float RecoilResidual = 0.005f;

	/** Stacked shots saturate here so a fast weapon does not walk off. */
	inline constexpr float MaxRecoilStrength = 1.5f;

	/**
	 * Advances the bob phase by the ground distance covered (distance-driven, not time-driven). Airborne or slower than
	 * BobMinSpeed the phase stands still. Returns the new phase in cycles (unbounded).
	 */
	inline double AdvanceBobPhase(double Phase, float GroundSpeed, bool bOnGround, float DeltaSeconds, const UVeloxViewModelSettings& S)
	{
		if (!bOnGround || GroundSpeed < S.BobMinSpeed || S.BobCycleCm <= 0.f)
		{
			return Phase;
		}
		return Phase + static_cast<double>(GroundSpeed) * DeltaSeconds / S.BobCycleCm;
	}

	/** Bob offset for a phase (cycles). Zero when not on the ground or too slow, so jumping and standing settle at once. */
	inline FVector BobOffset(double Phase, float GroundSpeed, bool bOnGround, const UVeloxViewModelSettings& S)
	{
		if (!bOnGround || GroundSpeed < S.BobMinSpeed)
		{
			return FVector::ZeroVector;
		}
		const double Angle = Phase * UE_DOUBLE_TWO_PI;
		return FVector(0.0, S.BobAmplitudeCm * S.BobLateralScale * FMath::Sin(Angle * 0.5), S.BobAmplitudeCm * FMath::Sin(Angle));
	}

	/** Exponential decay rate (1/s) that leaves RecoilResidual of an impulse after RecoilReturnSeconds. */
	inline float RecoilDecayRate(const UVeloxViewModelSettings& S)
	{
		return -FMath::Loge(RecoilResidual) / FMath::Max(S.RecoilReturnSeconds, 0.001f);
	}

	/** Recoil strength (1 = just fired), decayed over DeltaSeconds. */
	inline float DecayRecoil(float Strength, float DeltaSeconds, const UVeloxViewModelSettings& S)
	{
		return Strength * FMath::Exp(-RecoilDecayRate(S) * FMath::Max(DeltaSeconds, 0.f));
	}

	/** A Fire cue adds one impulse. */
	inline float AddRecoilImpulse(float Strength)
	{
		return FMath::Min(Strength + 1.f, MaxRecoilStrength);
	}

	/** Sway state: X = yaw lag, Y = pitch lag (deg), the weapon rotation relative to rest. */
	struct FSwayState
	{
		FVector2D Angles = FVector2D::ZeroVector;
	};

	/**
	 * First-order lag of the view rotation delta: the weapon lags the turn by SwayDegPerDeg of this frame's delta (ViewDeltaDeg
	 * = yaw, pitch), then returns to rest with time constant SwayReturnSeconds. Clamped to SwayMaxDeg per axis.
	 */
	inline void StepSway(FSwayState& State, const FVector2D& ViewDeltaDeg, float DeltaSeconds, const UVeloxViewModelSettings& S)
	{
		const double Keep = FMath::Exp(-static_cast<double>(DeltaSeconds) / FMath::Max(S.SwayReturnSeconds, 0.001f));
		State.Angles = (State.Angles - ViewDeltaDeg * S.SwayDegPerDeg) * Keep;
		State.Angles.X = FMath::Clamp(State.Angles.X, -S.SwayMaxDeg, S.SwayMaxDeg);
		State.Angles.Y = FMath::Clamp(State.Angles.Y, -S.SwayMaxDeg, S.SwayMaxDeg);
	}

	/** Translation part of the sway (cm): SwayCmPerDeg per degree of lag, clamped to SwayMaxCm per axis. */
	inline FVector SwayOffset(const FSwayState& State, const UVeloxViewModelSettings& S)
	{
		return FVector(0.0, FMath::Clamp(State.Angles.X * S.SwayCmPerDeg, -S.SwayMaxCm, S.SwayMaxCm), FMath::Clamp(State.Angles.Y * S.SwayCmPerDeg, -S.SwayMaxCm, S.SwayMaxCm));
	}

	/**
	 * How far the weapon is lowered, 0 (ready) to 1 (fully down). While a drop runs (bDropping) it goes down over
	 * DropSeconds as PendingDropRemaining counts to zero; afterwards the raise (SwitchRemaining seconds left to the switch
	 * end, 0 = none) brings it back over RaiseSeconds.
	 */
	inline float LowerAlpha(bool bDropping, float PendingDropRemaining, float DropSeconds, float SwitchRemaining, float RaiseSeconds)
	{
		if (bDropping)
		{
			return DropSeconds > 0.f ? FMath::Clamp(1.f - PendingDropRemaining / DropSeconds, 0.f, 1.f) : 1.f;
		}
		return (SwitchRemaining > 0.f && RaiseSeconds > 0.f) ? FMath::Clamp(SwitchRemaining / RaiseSeconds, 0.f, 1.f) : 0.f;
	}
}
