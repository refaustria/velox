// Velox - world-free leg-state rules of the third-person animation (ADR 0006 sections 4.4 and 4.5).
//
// Q3 cadence: hard cuts between leg states, no blend spaces beyond the 4-way directional pick, no root motion. The
// rules live here (no UWorld, no actor) so Velox.Character.AnimState tests them; UVeloxAnimInstance and the
// sequence-playback driver on AVeloxCharacter only sample the pawn and feed Step().

#pragma once

#include "CoreMinimal.h"
// EVeloxLegState, EVeloxRunDirection and FVeloxAnimTuning are cosmetics data (Core is below Character in the layering).
#include "Core/VeloxCharacterCosmetics.h"

/** What the leg-state rules read each update (sampled from the pawn by FVeloxAnimSampler). */
struct FVeloxAnimInput
{
	float Speed2D = 0.f;
	/** Velocity direction relative to the actor yaw, -180..180 (0 = forward, +90 = right). */
	float DirectionDeg = 0.f;
	float VerticalSpeed = 0.f;
	bool bAirborne = false;
	bool bCrouching = false;
	bool bDead = false;
};

/** Leg-state machine state, carried between updates. */
struct FVeloxAnimStateData
{
	EVeloxLegState LegState = EVeloxLegState::Idle;
	EVeloxRunDirection RunDirection = EVeloxRunDirection::Fwd;
	/** Seconds since LegState was entered. */
	float StateTime = 0.f;
	/** Play rate for the current state's sequence (run/crouch-walk cadence; 1 otherwise). */
	float PlayRate = 1.f;
	/** False until the first Step (which always reports a change so the first sequence is played). */
	bool bStarted = false;
};

namespace VeloxAnimState
{
	/** Quadrant pick from DirectionDeg (-180..180; values outside are wrapped). */
	inline EVeloxRunDirection PickRunDirection(float DirectionDeg, const FVeloxAnimTuning& Tuning = FVeloxAnimTuning())
	{
		const float Wrapped = FMath::UnwindDegrees(DirectionDeg);
		const float Abs = FMath::Abs(Wrapped);
		if (Abs <= Tuning.ForwardHalfAngleDeg)
		{
			return EVeloxRunDirection::Fwd;
		}
		if (Abs >= 180.f - Tuning.ForwardHalfAngleDeg)
		{
			return EVeloxRunDirection::Back;
		}
		return Wrapped > 0.f ? EVeloxRunDirection::Right : EVeloxRunDirection::Left;
	}

	/** Run cadence: Speed2D / ReferenceRunSpeed clamped to [MinPlayRate, MaxPlayRate]. */
	inline float ComputePlayRate(float Speed2D, const FVeloxAnimTuning& Tuning = FVeloxAnimTuning())
	{
		return FMath::Clamp(Speed2D / FMath::Max(Tuning.ReferenceRunSpeed, 1.f), Tuning.MinPlayRate, Tuning.MaxPlayRate);
	}

	/** Velocity direction relative to ActorYawDeg (-180..180, +90 = moving to the actor's right); 0 when not moving. */
	inline float ComputeDirectionDeg(const FVector& Velocity, float ActorYawDeg)
	{
		if (Velocity.SizeSquared2D() < UE_KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		const float VelocityYaw = FMath::RadiansToDegrees(FMath::Atan2(static_cast<float>(Velocity.Y), static_cast<float>(Velocity.X)));
		return FMath::FindDeltaAngleDegrees(ActorYawDeg, VelocityYaw);
	}

	/** Idle, Run, Air and the crouch states loop; JumpStart and Land play once; Dead plays once and holds its last frame. */
	inline bool IsLooping(EVeloxLegState State)
	{
		return State != EVeloxLegState::JumpStart && State != EVeloxLegState::Land && State != EVeloxLegState::Dead;
	}

	/** Grounded locomotion state for Input (idle/run, crouched variants). */
	inline EVeloxLegState Locomotion(const FVeloxAnimInput& Input, const FVeloxAnimTuning& Tuning)
	{
		const bool bMoving = Input.Speed2D >= Tuning.MoveSpeedThreshold;
		if (Input.bCrouching)
		{
			return bMoving ? EVeloxLegState::CrouchWalk : EVeloxLegState::CrouchIdle;
		}
		return bMoving ? EVeloxLegState::Run : EVeloxLegState::Idle;
	}

	/**
	 * Advances the leg state by Dt. Rules: Dead is sticky; airborne from the ground = JumpStart when moving up faster than
	 * JumpStartMinUpSpeed (else Air), JumpStart -> Air after JumpStartSeconds; touching down from Air/JumpStart = Land
	 * (crouched: straight to the crouch variant), Land -> idle/run after LandSeconds (or at once when crouching);
	 * grounded = idle/run at MoveSpeedThreshold, crouch variants when crouched. Run picks the direction quadrant and
	 * Run/CrouchWalk get the cadence play rate. Returns true when the sequence to play changed (leg state, or the run
	 * quadrant while running) and on the first call.
	 */
	inline bool Step(const FVeloxAnimInput& Input, FVeloxAnimStateData& State, float Dt, const FVeloxAnimTuning& Tuning)
	{
		const EVeloxLegState Prev = State.LegState;
		const EVeloxRunDirection PrevDirection = State.RunDirection;
		State.StateTime += FMath::Max(Dt, 0.f);

		EVeloxLegState Next = Prev;
		if (Prev == EVeloxLegState::Dead)
		{
			// Sticky: a corpse never gets up (a new life is a new pawn).
		}
		else if (Input.bDead)
		{
			Next = EVeloxLegState::Dead;
		}
		else if (Input.bAirborne)
		{
			if (Prev == EVeloxLegState::JumpStart)
			{
				if (State.StateTime >= Tuning.JumpStartSeconds)
				{
					Next = EVeloxLegState::Air;
				}
			}
			else if (Prev != EVeloxLegState::Air)
			{
				Next = Input.VerticalSpeed > Tuning.JumpStartMinUpSpeed ? EVeloxLegState::JumpStart : EVeloxLegState::Air;
			}
		}
		else if (Prev == EVeloxLegState::Air || Prev == EVeloxLegState::JumpStart)
		{
			Next = Input.bCrouching ? Locomotion(Input, Tuning) : EVeloxLegState::Land;
		}
		else if (Prev == EVeloxLegState::Land)
		{
			if (Input.bCrouching || State.StateTime >= Tuning.LandSeconds)
			{
				Next = Locomotion(Input, Tuning);
			}
		}
		else
		{
			Next = Locomotion(Input, Tuning);
		}

		if (Next != Prev)
		{
			State.LegState = Next;
			State.StateTime = 0.f;
		}
		if (Next == EVeloxLegState::Run)
		{
			State.RunDirection = PickRunDirection(Input.DirectionDeg, Tuning);
		}
		State.PlayRate = (Next == EVeloxLegState::Run || Next == EVeloxLegState::CrouchWalk) ? ComputePlayRate(Input.Speed2D, Tuning) : 1.f;

		const bool bFirst = !State.bStarted;
		State.bStarted = true;
		return bFirst || Next != Prev || (Next == EVeloxLegState::Run && State.RunDirection != PrevDirection);
	}

	inline bool Step(const FVeloxAnimInput& Input, FVeloxAnimStateData& State, float Dt)
	{
		return Step(Input, State, Dt, FVeloxAnimTuning());
	}
}
