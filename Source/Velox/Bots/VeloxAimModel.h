// Velox - world-free bot aim model (ADR 0005 section 8): reaction time, Ornstein-Uhlenbeck aim error, smoothed and
// rate-limited tracking, projectile lead. Pure functions over plain structs with an injected FRandomStream; unit-tested
// in Velox.Bots.AimError / AimLead / AimTurnRate. All angles in degrees. Bots never rewind (section 8).

#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

struct FVeloxBotDifficultyParams;

struct FVeloxAimParams
{
	float ReactionSeconds = 0.f;
	/** Reaction drawn uniformly in ReactionSeconds x [1 - J, 1 + J]. */
	float ReactionJitter = 0.3f;
	/** Stationary standard deviation of the error per axis (sigma). */
	float AimErrorDeg = 0.f;
	/** Correlation time of the error (tau). */
	float ErrorCorrelationSeconds = 0.3f;
	/** Exponential tracking time constant; 0 = no smoothing (only the turn-rate clamp). */
	float TrackingSmoothingSeconds = 0.f;
	/** Clamp of the aim's angular speed (yaw/pitch vector length); <= 0 = unlimited. */
	float MaxTurnRateDegPerSec = 0.f;
	bool bLeadProjectiles = false;
	/** Fraction of the lead (< 1 under-leads). */
	float LeadScale = 1.0f;
};

struct FVeloxAimState
{
	/** The bot's control rotation (output; roll always 0). */
	FRotator Aim = FRotator::ZeroRotator;
	/** OU error: X = yaw, Y = pitch. */
	FVector2D ErrorDeg = FVector2D::ZeroVector;
	/** Seconds of reaction left for the current target (> 0: no new information yet). */
	float ReactionLeft = 0.f;
	bool bHadTarget = false;
	/** Task 02 refinement: identity of the current target, so a target switch re-arms the reaction. */
	int32 TargetId = INDEX_NONE;
	/** Task 02 refinement: the desired rotation of the last tick; held while ReactionLeft > 0. */
	FRotator HeldDesired = FRotator::ZeroRotator;
};

struct FVeloxAimInput
{
	bool bHasTarget = false;
	/** Any stable id of the target (e.g. the PlayerState id); a change counts as a new target. */
	int32 TargetId = INDEX_NONE;
	FVector Eye = FVector::ZeroVector;
	FVector TargetCentre = FVector::ZeroVector;
	FVector TargetVelocity = FVector::ZeroVector;
	/** cm/s; 0 = hitscan (no lead). */
	float ProjectileSpeed = 0.f;
	/** Desired rotation without a target (path direction, last seen enemy location). */
	FRotator IdleAim = FRotator::ZeroRotator;
};

struct VELOX_API FVeloxAimModel
{
	/**
	 * One tick. A new target (none -> some, or a different TargetId) arms ReactionLeft; while it runs the aim keeps
	 * turning toward HeldDesired (no new information); the tick in which it expires tracks for the remaining fraction of
	 * Dt. Desired with a target = (LeadPoint or centre - Eye).Rotation() + ErrorDeg; without one = IdleAim (no error).
	 * The error is an OU process per axis, advanced every tick with the exact discretisation (StepError). Tracking:
	 * Delta x (1 - exp(-h / TrackingSmoothingSeconds)), length clamped to MaxTurnRateDegPerSec x h; pitch clamped to
	 * +-89. Returns the new State.Aim.
	 */
	static FRotator Update(FVeloxAimState& State, const FVeloxAimInput& In, const FVeloxAimParams& Params, float Dt, FRandomStream& Stream);

	/**
	 * Lead point for a constant-velocity projectile from Eye: two fixed-point iterations of t = |P - Eye| / Speed,
	 * P = Target + TargetVel x t x LeadScale. Target when ProjectileSpeed <= 0.
	 */
	static FVector LeadPoint(const FVector& Eye, const FVector& Target, const FVector& TargetVel, float ProjectileSpeed, float LeadScale);

	/**
	 * OU step per axis, exact discretisation: E' = E x rho + Sigma x sqrt(1 - rho^2) x N(0,1), rho = exp(-Dt / Tau). Same
	 * stationary sigma and lag-1 autocorrelation exp(-Dt / Tau) as the ADR's Euler form, independent of the frame rate.
	 * Tau <= 0: white noise with std Sigma. Sigma <= 0: error 0.
	 */
	static void StepError(FVector2D& Error, float Sigma, float Tau, float Dt, FRandomStream& Stream);

	/** Standard normal sample (Box-Muller) from the stream. */
	static float Gaussian(FRandomStream& Stream);

	/** Resets the state to an aim (possession, respawn): no target, no reaction, error 0. */
	static void Reset(FVeloxAimState& State, const FRotator& Aim);

	/** Aim params of a difficulty preset. */
	static FVeloxAimParams MakeParams(const FVeloxBotDifficultyParams& Difficulty);
};
