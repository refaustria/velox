// Velox - world-free nav-link traversal state machine (ADR 0005 section 4).

#include "Bots/VeloxBotManeuver.h"

namespace VeloxBotManeuverPrivate
{
	// Time comparisons tolerate float accumulation of the caller's world time.
	constexpr double TimeEpsilon = 1e-6;
}

void FVeloxBotManeuver::Begin(const FVeloxManeuverParams& InParams, const FVector& InStart, const FVector& InEnd, double Time)
{
	Params = InParams;
	Type = InParams.Type;
	Start = InStart;
	End = InEnd;
	StartTime = Time;
	Retries = 0;
	Result = EVeloxManeuverResult::None;
	bSeenAirborne = false;
	bGroundSeen = false;
	bLaunchImpulseSeen = false;
	EnterPhase(EVeloxManeuverPhase::Approach, Time);
}

void FVeloxBotManeuver::EnterPhase(EVeloxManeuverPhase NewPhase, double Time)
{
	Phase = NewPhase;
	PhaseStartTime = Time;
}

FVeloxManeuverOutput FVeloxBotManeuver::Finish(EVeloxManeuverResult InResult, double Time)
{
	Result = InResult;
	EnterPhase(EVeloxManeuverPhase::Finished, Time);
	FVeloxManeuverOutput Out;
	Out.WishToward = EVeloxManeuverWish::End;
	Out.bDone = InResult == EVeloxManeuverResult::Ok;
	Out.bAbort = !Out.bDone;
	Out.Reason = InResult;
	return Out;
}

FVeloxManeuverOutput FVeloxBotManeuver::TickAirborne(const FVeloxManeuverInput& In)
{
	if (In.bImpulseReceived)
	{
		// Task m5-06b: the first impulse of a pad traversal is the pad's own launch; any other impulse in the flight is an
		// external re-launch (a pad under the drop's landing, a rocket hit). At End it is the landing (Ok); a drop that
		// reached its end's level has done what the link asks; anywhere else the flight is no longer this link's.
		const bool bOwn = Type == EVeloxManeuverType::JumpPad && !bLaunchImpulseSeen;
		bLaunchImpulseSeen = true;
		if (!bOwn)
		{
			const bool bAtEnd = In.DistToEnd <= Params.EndAcceptanceCm ||
				(Type == EVeloxManeuverType::Drop && In.HeightAboveEndCm <= Params.EndAcceptanceCm * 0.5f);
			return Finish(bAtEnd ? EVeloxManeuverResult::Ok : EVeloxManeuverResult::Relaunched, In.Time);
		}
	}
	if (!In.bOnGround)
	{
		bSeenAirborne = true;
	}
	else if (bSeenAirborne)
	{
		if (In.DistToEnd <= Params.EndAcceptanceCm)
		{
			return Finish(EVeloxManeuverResult::Ok, In.Time);
		}
		// Task m5-06b: a traversal that began in the air (no ground tick before the flight, no own launch) did not choose
		// this flight; its landing away from End is not a miss. Begun on the ground (or launched by the link), it is.
		return Finish(bGroundSeen || bLaunchImpulseSeen ? EVeloxManeuverResult::Missed : EVeloxManeuverResult::AirStart, In.Time);
	}
	FVeloxManeuverOutput Out;
	Out.WishToward = EVeloxManeuverWish::End;
	// Gap jumps keep the hop going (the steering re-decides after the landing); pads, drops and rocket jumps land flat.
	Out.bJumpHeld = Type == EVeloxManeuverType::Jump || Type == EVeloxManeuverType::JumpFast;
	return Out;
}

FVeloxManeuverOutput FVeloxBotManeuver::Tick(const FVeloxManeuverInput& In)
{
	FVeloxManeuverOutput Out;
	Out.WishToward = EVeloxManeuverWish::End;

	// D28 (a): a traversal that cannot complete (a Drop whose End lies through a wall, a pad flight that never lands) is
	// abandoned; Launched keeps its own, shorter ImpulseTimeoutSeconds.
	if (IsActive() && Phase != EVeloxManeuverPhase::Launched && Params.TimeoutSeconds > 0.f &&
		In.Time - StartTime + VeloxBotManeuverPrivate::TimeEpsilon >= Params.TimeoutSeconds)
	{
		return Finish(EVeloxManeuverResult::Timeout, In.Time);
	}
	// D28 (b): on the ground well below the link start the pawn is on another surface (it fell off the approach).
	if ((Phase == EVeloxManeuverPhase::Approach || Phase == EVeloxManeuverPhase::BackUp) && In.bOnGround && In.HeightBelowStartCm > Params.BelowStartAbortCm)
	{
		return Finish(EVeloxManeuverResult::BelowStart, In.Time);
	}

	// Task m5-06b: ground contact before the flight (any phase but Airborne, where the ground is the landing).
	if (In.bOnGround && Phase != EVeloxManeuverPhase::Airborne && Phase != EVeloxManeuverPhase::Finished)
	{
		bGroundSeen = true;
	}

	switch (Phase)
	{
	case EVeloxManeuverPhase::Idle:
		Out.WishScale = 0.f;
		return Out;

	case EVeloxManeuverPhase::Finished:
		Out.bDone = Result == EVeloxManeuverResult::Ok;
		Out.bAbort = !Out.bDone;
		Out.Reason = Result;
		return Out;

	case EVeloxManeuverPhase::Approach:
		switch (Type)
		{
		case EVeloxManeuverType::JumpPad:
			if (In.bImpulseReceived && In.bOnGround)
			{
				// The pad's launch on the tick it fires (the pawn leaves the ground on the next one).
				bLaunchImpulseSeen = true;
			}
			if (!In.bOnGround)
			{
				EnterPhase(EVeloxManeuverPhase::Airborne, In.Time);
				return TickAirborne(In);
			}
			// To the pad centre, then on across it (the trigger launches like for players); never jump.
			Out.WishToward = In.DistToStart > Params.StartAcceptanceCm ? EVeloxManeuverWish::Start : EVeloxManeuverWish::End;
			return Out;

		case EVeloxManeuverType::Drop:
			if (!In.bOnGround)
			{
				EnterPhase(EVeloxManeuverPhase::Airborne, In.Time);
				return TickAirborne(In);
			}
			return Out;

		case EVeloxManeuverType::Jump:
		case EVeloxManeuverType::JumpFast:
			if (!In.bOnGround)
			{
				EnterPhase(EVeloxManeuverPhase::Airborne, In.Time);
				return TickAirborne(In);
			}
			if (In.DistToStart <= Params.TakeoffLeadCm)
			{
				const bool bTooSlow = Type == EVeloxManeuverType::JumpFast && In.Speed2D < Params.RequiredSpeedCmS * Params.RequiredSpeedFraction;
				if (bTooSlow)
				{
					if (Retries >= Params.MaxRetries)
					{
						return Finish(EVeloxManeuverResult::TooSlow, In.Time);
					}
					++Retries;
					EnterPhase(EVeloxManeuverPhase::BackUp, In.Time);
					Out.WishToward = EVeloxManeuverWish::AwayFromStart;
					return Out;
				}
				Out.bJumpHeld = true;
			}
			return Out;

		case EVeloxManeuverType::RocketJump:
			if (!In.bEligible)
			{
				return Finish(EVeloxManeuverResult::Ineligible, In.Time);
			}
			if (In.DistToStart <= Params.StartAcceptanceCm)
			{
				EnterPhase(EVeloxManeuverPhase::Switch, In.Time);
				return Tick(In);
			}
			Out.WishToward = EVeloxManeuverWish::Start;
			Out.bWantRocketLauncher = true;
			return Out;
		}
		return Out;

	case EVeloxManeuverPhase::BackUp:
		if (In.DistToStart >= Params.RunUpCm)
		{
			EnterPhase(EVeloxManeuverPhase::Approach, In.Time);
			return Out;
		}
		Out.WishToward = EVeloxManeuverWish::AwayFromStart;
		return Out;

	case EVeloxManeuverPhase::Switch:
		if (!In.bEligible)
		{
			return Finish(EVeloxManeuverResult::Ineligible, In.Time);
		}
		if (In.bSwitchDone)
		{
			EnterPhase(EVeloxManeuverPhase::RunUp, In.Time);
			return Tick(In);
		}
		Out.WishToward = EVeloxManeuverWish::Start;
		Out.WishScale = 0.f;
		Out.bFaceEnd = true;
		Out.bWantRocketLauncher = true;
		return Out;

	case EVeloxManeuverPhase::RunUp:
		if (!In.bEligible)
		{
			return Finish(EVeloxManeuverResult::Ineligible, In.Time);
		}
		Out.bFaceEnd = true;
		Out.bWantRocketLauncher = true;
		if (In.Time - PhaseStartTime + VeloxBotManeuverPrivate::TimeEpsilon >= Params.RunUpSeconds)
		{
			// Q3 BotRocketJump: look down, jump and fire in the same tick.
			Out.bForcePitch = true;
			Out.PitchDeg = -Params.RocketJumpPitchDeg;
			Out.bJumpHeld = true;
			Out.bFire = true;
			bSeenAirborne = false;
			EnterPhase(EVeloxManeuverPhase::Launched, In.Time);
		}
		return Out;

	case EVeloxManeuverPhase::Launched:
		if (In.bImpulseReceived)
		{
			bSeenAirborne = !In.bOnGround;
			bLaunchImpulseSeen = true;
			EnterPhase(EVeloxManeuverPhase::Airborne, In.Time);
			return Out;
		}
		if (In.Time - PhaseStartTime > Params.ImpulseTimeoutSeconds + VeloxBotManeuverPrivate::TimeEpsilon)
		{
			return Finish(EVeloxManeuverResult::NoImpulse, In.Time);
		}
		return Out;

	case EVeloxManeuverPhase::Airborne:
		return TickAirborne(In);
	}
	return Out;
}
