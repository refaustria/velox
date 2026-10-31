// Velox - world-free footstep cadence (ADR 0006 section 9, task M5-10).
// A step every SpacingCm of ground travel, alternating left and right; none crouched, airborne or slower than MinSpeed.
// Distance-driven (not time-driven), so proxies and bots with the same motion make the same steps.

#pragma once

#include "CoreMinimal.h"

namespace VeloxFootstepMath
{
	struct FState
	{
		/** Ground travel since the last step (cm). */
		float AccumulatedCm = 0.f;
		/** The next step is the right foot. */
		bool bNextRight = false;
	};

	/** Whether a pawn makes footstep noise at all: on the ground, not crouched, at least MinSpeed (cm/s). */
	inline bool IsAudible(bool bGrounded, bool bCrouched, float SpeedCmS, float MinSpeedCmS)
	{
		return bGrounded && !bCrouched && SpeedCmS >= MinSpeedCmS;
	}

	/**
	 * Adds DistanceCm of travel when audible and returns the number of steps that completed (usually 0 or 1). OutFirstRight is
	 * the foot of the first returned step; later ones alternate. An inaudible interval adds nothing and keeps the
	 * accumulated distance, so a stop and go does not shorten or lengthen the next stride.
	 */
	inline int32 Advance(FState& State, float DistanceCm, bool bAudible, float SpacingCm, bool& OutFirstRight)
	{
		OutFirstRight = State.bNextRight;
		if (!bAudible || SpacingCm <= 0.f || DistanceCm <= 0.f)
		{
			return 0;
		}
		State.AccumulatedCm += DistanceCm;
		int32 Steps = 0;
		while (State.AccumulatedCm >= SpacingCm)
		{
			State.AccumulatedCm -= SpacingCm;
			State.bNextRight = !State.bNextRight;
			++Steps;
		}
		return Steps;
	}

	/** 0 = none, 1 = soft, 2 = hard landing for the impact speed |Vz|. */
	inline int32 LandingKind(float ImpactSpeedCmS, float SoftVz, float HardVz)
	{
		const float Speed = FMath::Abs(ImpactSpeedCmS);
		return Speed >= HardVz ? 2 : (Speed >= SoftVz ? 1 : 0);
	}

	/** 0 = light, 1 = medium, 2 = heavy pain band for the health left after the damage. */
	inline int32 PainBand(float RemainingHealth, float HeavyBelow, float MediumBelow)
	{
		return RemainingHealth < HeavyBelow ? 2 : (RemainingHealth < MediumBelow ? 1 : 0);
	}
}
