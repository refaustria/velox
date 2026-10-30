// Velox - world-free bot steering (ADR 0005 section 5): wish direction, wish scale and jump for one tick, with the Q3
// strafe-jump optimum in the air and bunny hopping on the ground. Pure functions over plain structs, unit-tested in
// Velox.Bots.StrafeSteering (closed loop through VeloxMoveSim::StepFrame). Vanilla Q3 air physics (ADR 0002 section 1).

#pragma once

#include "CoreMinimal.h"

struct FVeloxBotDifficultyParams;

/** One tick of steering input, gathered by the path follower. 2-D vectors are world XY. */
struct FVeloxSteerInput
{
	/** Pawn XY velocity, cm/s. */
	FVector2D Velocity = FVector2D::ZeroVector;
	bool bOnGround = false;
	/** Toward the current path target (any length; normalised here). */
	FVector2D ToTarget = FVector2D::ZeroVector;
	/** Remaining length of the current segment, cm. */
	float SegmentRemainingCm = 0.f;
	/** Turn at the end of the segment, degrees (0 = straight on). Ignored on the final segment. */
	float NextCornerDeg = 0.f;
	/** Task 02 refinement: the segment ends at the goal, so the arrive slowdown applies (ADR: "near the final target"). */
	bool bFinalSegment = false;
	/** Air wish speed, cm/s (AirMaxWishSpeed x Haste scale). */
	float WishSpeed = 0.f;
	/** Air acceleration, 1/s (AirAcceleration). */
	float AirAccel = 0.f;
	/** Velocity substep of the movement component, s (FixedStep). */
	float FixedStep = 0.f;
};

struct FVeloxSteerParams
{
	bool bBunnyHop = false;
	bool bStrafeJump = false;
	/** Hold jump only when at least this much straight segment remains, cm. */
	float MinHopDistanceCm = 400.f;
	/** Release jump before corners sharper than this, degrees. */
	float MaxHopCornerDeg = 35.f;
	/** Wish angle off the velocity when the heading error exceeds the strafe optimum, degrees. */
	float StrafeMaxAngleDeg = 60.f;
	/** Final-target arrive radius: WishScale ramps linearly to 0 at the target, cm. */
	float ArriveSlowdownCm = 150.f;
};

struct FVeloxSteerOutput
{
	/** Unit XY or zero. */
	FVector2D WishDir = FVector2D::ZeroVector;
	float WishScale = 0.f;
	bool bJumpHeld = false;
};

namespace VeloxBotSteering
{
	/**
	 * Q3 optimal strafe angle off the velocity, degrees: acos((W - a) / |V|) with a = AirAccel * Dt * W (the accelerate
	 * clamp's optimum: the wish component of the velocity ends exactly at W). 0 when |V| <= W - a.
	 */
	VELOX_API float OptimalStrafeAngleDeg(float Speed, float WishSpeed, float AirAccel, float Dt);

	/**
	 * One tick. Ground: wish toward the target; jump held while bunny hopping on a long straight segment. A ground tick
	 * with jump held counts as air (the movement component jumps first in that frame, then air-accelerates). Air with
	 * strafe jumping and |V| > W - a: wish = velocity rotated by the optimum toward the target side; when the heading
	 * error exceeds the optimum the wish is rotated by max(optimum, min(StrafeMaxAngleDeg, error)) instead. Air without
	 * strafe jumping: wish toward the target. Final segment: WishScale = clamp(remaining / ArriveSlowdownCm, 0, 1).
	 */
	VELOX_API FVeloxSteerOutput Steer(const FVeloxSteerInput& In, const FVeloxSteerParams& Params);

	/** Steering params of a difficulty preset (bBunnyHop, bStrafeJump; the rest keeps the ADR defaults). */
	VELOX_API FVeloxSteerParams MakeParams(const FVeloxBotDifficultyParams& Difficulty);

	/** Signed angle from A to B in degrees, (-180, 180], positive counter-clockwise (toward +Y from +X). 0 if either is zero. */
	VELOX_API float SignedAngleDeg(const FVector2D& A, const FVector2D& B);
}
