// Velox - world-free movement simulation core (ADR 0002 section 3, formulas in section 1).
//
// Pure functions over plain structs: no UObject, no world, no logging. UVeloxMovementComponent calls them from its
// velocity hooks, tests and bots call them directly. Units are UE units (cm, s); "XY" means horizontal.
// This header must include nothing but CoreMinimal.h.

#pragma once

#include "CoreMinimal.h"

/**
 * POD copy of every number in UVeloxMovementSettings, built per move by UVeloxMovementSettings::BuildSimParams()
 * (the only conversion point). Defaults are neutral zeros on purpose: real values only come from the settings.
 */
struct FVeloxMoveParams
{
	// Units
	float UnitScale = 0.f;                 // cm per Q3 unit (display/presets only)

	// Ground
	float MaxGroundSpeed = 0.f;            // cm/s, wish speed cap
	float GroundAcceleration = 0.f;        // 1/s (Q3 pm_accelerate)
	float GroundFriction = 0.f;            // 1/s (Q3 pm_friction)
	float StopSpeed = 0.f;                 // cm/s (Q3 pm_stopspeed)
	float CrouchSpeedScale = 0.f;          // applied by the CMC to WishSpeed
	float StepHeight = 0.f;                // cm, engine field (MaxStepHeight)
	float WalkableFloorAngle = 0.f;        // degrees, engine field
	float EdgePerchRadius = 0.f;           // cm, engine field (PerchRadiusThreshold)

	// Air
	float AirAcceleration = 0.f;           // 1/s (Q3 pm_airaccelerate)
	float AirMaxWishSpeed = 0.f;           // cm/s, vanilla air wish cap
	bool bAirControl = false;              // CPM air rules
	float AirStopAcceleration = 0.f;       // 1/s (CPM pm_airstopaccelerate)
	float AirStrafeAcceleration = 0.f;     // 1/s (CPM pm_strafeaccelerate)
	float AirStrafeMaxWishSpeed = 0.f;     // cm/s (CPM pm_wishspeed)
	float AirControl = 0.f;                // CPM pm_aircontrol
	float AirControlScale = 0.f;           // CPM aircontrol k

	// Jump / gravity
	float JumpVelocity = 0.f;              // cm/s
	bool bAutoHop = false;                 // jump held re-jumps on landing (CMC landing path)
	float GravityZ = 0.f;                  // cm/s^2, negative is down

	// Ramps
	float RampSlideMinNormalSpeed = 0.f;   // cm/s
	float RampExitMinAngle = 0.f;          // degrees
	bool bRampExitCatchAir = false;

	// Knockback
	float KnockbackGraceMin = 0.f;         // s
	float KnockbackGraceMax = 0.f;         // s
	float KnockbackGracePerSpeed = 0.f;    // s per cm/s

	// Integration / prediction
	float FixedStep = 0.f;                 // s
	bool bFixedStepIntegration = false;
	bool bCombineAirMoves = false;
};

/** One move's input. WishDir is unit XY or zero; WishSpeed in [0, MaxGroundSpeed] (Q3 wishvel = WishDir * WishSpeed). */
struct FVeloxMoveInput
{
	FVector WishDir = FVector::ZeroVector;
	float WishSpeed = 0.f;
	bool bHasForwardInput = false;
	bool bHasSideInput = false;
	bool bJumpHeld = false;
};

/** State carried by the reference model StepFrame. */
struct FVeloxMoveState
{
	FVector Velocity = FVector::ZeroVector;
	bool bOnGround = false;
	float KnockbackGrace = 0.f;
};

namespace VeloxMoveSim
{
	/** Number of velocity-integration substeps for a move of length Dt: ceil(Dt / FixedStep), 1 if fixed step is off. */
	VELOX_API int32 NumSubsteps(float Dt, const FVeloxMoveParams& P);

	/** Q3 PM_Friction on the XY part of V (Z untouched). Does not know about knockback grace (see GroundMove). */
	VELOX_API FVector ApplyFriction(const FVector& V, float Dt, const FVeloxMoveParams& P);

	/** Q3 PM_Accelerate: adds at most Accel*Dt*WishSpeed along WishDir, never beyond WishSpeed along WishDir. */
	VELOX_API FVector Accelerate(const FVector& V, const FVector& WishDir, float WishSpeed, float Accel, float Dt);

	/** CPM air control: turns the XY velocity towards WishDir without changing its length. Caller gates on input. */
	VELOX_API FVector AirControl(const FVector& V, const FVector& WishDir, float Dt, const FVeloxMoveParams& P);

	/**
	 * Ground velocity update, substepped, Z untouched. Per substep: friction + accelerate with GroundAcceleration; while
	 * KnockbackGrace > 0 (Q3 PMF_TIME_KNOCKBACK) no friction and accelerate with AirAcceleration instead.
	 */
	VELOX_API FVector GroundMove(const FVector& V, const FVeloxMoveInput& In, float Dt, float KnockbackGrace, const FVeloxMoveParams& P);

	/** Air velocity update (vanilla or CPM), substepped, no gravity. Z untouched. */
	VELOX_API FVector AirMove(const FVector& V, const FVeloxMoveInput& In, float Dt, const FVeloxMoveParams& P);

	/** V.Z += GravityZ * Dt. Tests/bots only; the CMC uses NewFallVelocity. */
	VELOX_API FVector ApplyGravity(const FVector& V, float Dt, const FVeloxMoveParams& P);

	/** Q3 jump: V.Z = JumpVelocity (set, not max). XY untouched. */
	VELOX_API FVector Jump(const FVector& V, const FVeloxMoveParams& P);

	/** V minus its component along the (unit) Normal. */
	VELOX_API FVector ClipVelocity(const FVector& V, const FVector& Normal);

	/** V + Impulse, or Impulse if bOverride. */
	VELOX_API FVector ApplyKnockback(const FVector& V, const FVector& Impulse, bool bOverride);

	/** clamp(|Impulse| * KnockbackGracePerSpeed, KnockbackGraceMin, KnockbackGraceMax); 0 for a zero impulse. */
	VELOX_API float KnockbackGraceFor(const FVector& Impulse, const FVeloxMoveParams& P);

	/**
	 * Launch velocity from From to Target under GravityZ (negative). Target above From: Q3 AimAtTarget (apex at
	 * the target, as Q3 target_position). Target level with or below From: minimum-launch-speed ballistic solution.
	 * Returns zero if unsolvable (no gravity, or Target directly below/at From).
	 */
	VELOX_API FVector JumpPadLaunchVelocity(const FVector& From, const FVector& Target, float GravityZ);

	/**
	 * Reference frame, no collision (tests and bot planning; the CMC adds sweeps, floor logic and the apex substep).
	 * Ground + bJumpHeld: Jump first (no friction in the jump frame, Q3 PM_WalkMove checks jump before PM_Friction),
	 * then the frame continues as air. Ground otherwise: GroundMove, Z = 0. Air: AirMove + ApplyGravity.
	 * KnockbackGrace counts down by Dt. bJumpHeld is treated as "jump now"; bAutoHop gating is the CMC's job.
	 */
	VELOX_API void StepFrame(FVeloxMoveState& S, const FVeloxMoveInput& In, float Dt, const FVeloxMoveParams& P);
}
