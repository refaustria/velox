// Velox - movement tunables (ADR 0002 section 1). C++ defaults are the ADR 0002 initial tuning table
// (Q3 values converted with UnitScale 2.5 cm per Q3 unit); DA_MovementSettings_Default carries the same values.

#pragma once

#include "Engine/DataAsset.h"
#include "Movement/VeloxMoveSim.h"

#include "VeloxMovementSettings.generated.h"

/**
 * All movement tunables for UVeloxMovementComponent / VeloxMoveSim. No movement constant lives in code.
 * BuildSimParams() is the only place these fields are converted into FVeloxMoveParams.
 */
UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Movement Settings"))
class VELOX_API UVeloxMovementSettings : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** POD copy of every field for VeloxMoveSim (per move), with the non-shipping velox.move.<Field> cvar overrides applied. */
	FVeloxMoveParams BuildSimParams() const;

	/** The same copy without cvar overrides (asset values only; DumpSettings and tests). */
	FVeloxMoveParams BuildBaseSimParams() const;

	// --- Units ---

	/** Centimetres per Q3 unit. Assets store UE units; this is for HUD display (ups) and presets only. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Units", Meta = (ClampMin = "0.0"))
	float UnitScale = 2.5f;

	// --- Ground ---

	/** Maximum wish speed on the ground (cm/s; Q3 g_speed 320). Also MaxWalkSpeed and MaxAcceleration. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float MaxGroundSpeed = 800.0f;

	/** Ground acceleration factor (1/s; Q3 pm_accelerate 10, CPM 15). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float GroundAcceleration = 10.0f;

	/** Ground friction factor (1/s; Q3 pm_friction 6). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float GroundFriction = 6.0f;

	/** Below this speed friction acts as if moving at this speed (cm/s; Q3 pm_stopspeed 100). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float StopSpeed = 250.0f;

	/** Wish speed scale while crouched (Q3 pm_duckScale 0.25). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrouchSpeedScale = 0.25f;

	/** Maximum step-up height (cm; Q3 STEPSIZE 18). Applied to MaxStepHeight. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float StepHeight = 45.0f;

	/** Steepest walkable floor (degrees; Q3 MIN_WALK_NORMAL 0.7). Applied with SetWalkableFloorAngle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float WalkableFloorAngle = 45.5f;

	/** Capsule stands on edges like a box (cm). Applied to PerchRadiusThreshold. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ground", Meta = (ClampMin = "0.0"))
	float EdgePerchRadius = 15.0f;

	// --- Air ---

	/** Air acceleration factor (1/s; Q3 pm_airaccelerate 1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air", Meta = (ClampMin = "0.0"))
	float AirAcceleration = 1.0f;

	/** Wish speed cap applied to air acceleration (cm/s; vanilla = g_speed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air", Meta = (ClampMin = "0.0"))
	float AirMaxWishSpeed = 800.0f;

	/** Enables the CPM air rules (air stop accel, side-strafe accel, forward air control).
	 *  ScriptName: Python would otherwise name both this and AirControl "air_control". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air|CPM", Meta = (ScriptName = "AirControlEnabled"))
	bool bAirControl = false;

	/** CPM: acceleration when wishing against the velocity (1/s; pm_airstopaccelerate 2.5). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air|CPM", Meta = (ClampMin = "0.0"))
	float AirStopAcceleration = 2.5f;

	/** CPM: acceleration with side-only input (1/s; pm_strafeaccelerate 70). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air|CPM", Meta = (ClampMin = "0.0"))
	float AirStrafeAcceleration = 70.0f;

	/** CPM: wish speed cap with side-only input (cm/s; pm_wishspeed 30). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air|CPM", Meta = (ClampMin = "0.0"))
	float AirStrafeMaxWishSpeed = 75.0f;

	/** CPM: air control strength with forward/back-only input (pm_aircontrol 150). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air|CPM", Meta = (ClampMin = "0.0"))
	float AirControl = 150.0f;

	/** CPM: air control constant k (cm/s; CPM k 32 in Q3 units * UnitScale; a speed added to v_unit*speed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Air|CPM", Meta = (ClampMin = "0.0"))
	float AirControlScale = 80.0f;

	// --- Jump / gravity ---

	/** Vertical take-off velocity of a jump (cm/s; Q3 JUMP_VELOCITY 270). Sets Z, does not add. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Jump", Meta = (ClampMin = "0.0"))
	float JumpVelocity = 675.0f;

	/** Holding jump re-jumps in the landing frame, before friction (bunny hop). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Jump")
	bool bAutoHop = true;

	/** Pawn gravity (cm/s^2, negative is down; Q3 g_gravity 800). Used instead of the project gravity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Gravity")
	float GravityZ = -2000.0f;

	// --- Ramps ---

	/** Rising into a surface faster than this along its normal is a slide, not a landing (cm/s; Q3 10 ups). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ramps", Meta = (ClampMin = "0.0"))
	float RampSlideMinNormalSpeed = 25.0f;

	/** Floors steeper than this (degrees) count as ramps for ramp-exit vertical speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ramps", Meta = (ClampMin = "0.0", ClampMax = "90.0"))
	float RampExitMinAngle = 5.0f;

	/** Leave the ground at the top of a ramp with the ramp's vertical speed instead of snapping down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Ramps")
	bool bRampExitCatchAir = true;

	// --- Knockback ---

	/** Minimum ground-friction-free time after a knockback (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Knockback", Meta = (ClampMin = "0.0"))
	float KnockbackGraceMin = 0.05f;

	/** Maximum ground-friction-free time after a knockback (s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Knockback", Meta = (ClampMin = "0.0"))
	float KnockbackGraceMax = 0.2f;

	/** Friction-free time per cm/s of knockback (s per cm/s; Q3 2 ms per knockback point). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Knockback", Meta = (ClampMin = "0.0"))
	float KnockbackGracePerSpeed = 0.00016f;

	// --- Integration / prediction ---

	/** Velocity integration substep (s; Q3 pmove_msec 8). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Integration", Meta = (ClampMin = "0.001"))
	float FixedStep = 0.008f;

	/** Substep velocity updates at FixedStep (frame-rate independent strafe gain). Off only for A/B tests. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Integration")
	bool bFixedStepIntegration = true;

	/** Allow the engine to combine saved moves while falling (ADR 0002 section 5). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Integration")
	bool bCombineAirMoves = false;
};
