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
	if (!In.bOnGround)
	{
		bSeenAirborne = true;
	}
	else if (bSeenAirborne)
	{
		return Finish(In.DistToEnd <= Params.EndAcceptanceCm ? EVeloxManeuverResult::Ok : EVeloxManeuverResult::Missed, In.Time);
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
