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
	bBeganInFlight = false;
	EnterPhase(EVeloxManeuverPhase::Approach, Time);
}

void FVeloxBotManeuver::Begin(const FVeloxManeuverParams& InParams, const FVector& InStart, const FVector& InEnd, const FVeloxManeuverInput& In)
{
	Begin(InParams, InStart, InEnd, In.Time);
	bBeganInFlight = In.bBeganInFlight;
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
		// reached its end's level next to End (task m5-06c, review D2: within 2 x EndAcceptanceCm) has done what the link
		// asks; anywhere else the flight is no longer this link's.
		const bool bOwn = Type == EVeloxManeuverType::JumpPad && !bLaunchImpulseSeen;
		bLaunchImpulseSeen = true;
		if (!bOwn)
		{
			const bool bAtEndLevel = Type == EVeloxManeuverType::Drop && In.HeightAboveEndCm <= Params.EndAcceptanceCm * 0.5f &&
				In.DistToEnd <= Params.EndAcceptanceCm * 2.f;
			const bool bAtEnd = In.DistToEnd <= Params.EndAcceptanceCm || bAtEndLevel;
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
		// Task m5-06b / 06c (review D1): a traversal that began inside a foreign flight (bBeganInFlight: longer than the
		// bot's own hop) with no ground tick and no own launch did not choose this flight; its landing away from End is
		// not a miss. Begun on the ground, in the bot's own hop, or launched by the link, it is.
		const bool bForeignFlight = bBeganInFlight && !bGroundSeen && !bLaunchImpulseSeen;
		return Finish(bForeignFlight ? EVeloxManeuverResult::AirStart : EVeloxManeuverResult::Missed, In.Time);
	}
	FVeloxManeuverOutput Out;
	Out.WishToward = EVeloxManeuverWish::End;
	// Task M7 H11: every flight lands flat, gap jumps too. A held key re-jumps in the landing frame itself (bAutoHop,
	// UVeloxMovementComponent::ProcessLanded), so the pawn is never on the ground at a bot tick and the landing above is
	// never seen: the gap jump hopped on at End (or in the pit) until the D28 timeout (Nemus B2: every link_timeout strike).
	Out.bJumpHeld = false;
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
			// Task M7 H11: take off at the edge, not up to TakeoffLeadCm early in any direction (the radial DistToStart): a
			// 400 cm gap needs the take-off within about 90 cm of the edge at 800 cm/s. In the window a ledge ahead fires the
			// jump; without one it fires TakeoffMaxPastStartCm past Start. StepCm: the jump goes out with this tick's move.
			if ((In.bLedgeAhead && In.ProgressCm + In.StepCm >= -Params.TakeoffLeadCm) || In.ProgressCm + In.StepCm >= Params.TakeoffMaxPastStartCm)
			{
				// Task M7 H11: the jump carries the speed along the link, not the 2-D speed: a bot that came in from the side
				// (Nemus Log_Land joins the log at 90 degrees, the Templum podium from any angle) took off with ~800 cm/s mostly
				// across the link and fell short. Below the along-link speed the flight needs (VeloxBotManeuverRules::
				// GapTakeoffSpeed), back up along the link for a straight run-up (Params.RunUpCm, clamped to the mesh by the
				// follower; under 100 cm there is no room) and retry; a JUMP out of retries jumps anyway (best effort, the bot is
				// at the edge), a JUMP_FAST stays tooslow.
				const float NeedAlong = VeloxBotManeuverRules::GapTakeoffSpeed(Params.LinkLengthCm, In.ProgressCm, Params.TakeoffLandInsetCm, Params.GapAirSeconds);
				const bool bSlowAlong = In.AlongSpeedCmS < NeedAlong;
				// The speed across the link carries the flight off a narrow landing (the flight's arrive wish brakes only part).
				const bool bAcross = Params.TakeoffMaxLateralCmS > 0.f && In.LateralSpeedCmS > Params.TakeoffMaxLateralCmS;
				const bool bTooSlow = bSlowAlong || bAcross || (Type == EVeloxManeuverType::JumpFast && In.Speed2D < Params.RequiredSpeedCmS * Params.RequiredSpeedFraction);
				if (bTooSlow)
				{
					const bool bCanBackUp = Retries < Params.MaxRetries && Params.RunUpCm >= VeloxBotManeuverRules::MinRunUpCm;
					if (!bCanBackUp && Type == EVeloxManeuverType::Jump)
					{
						Out.bJumpHeld = true;
						return Out;
					}
					if (!bCanBackUp)
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
			else if (Params.TakeoffMaxLateralCmS > 0.f && In.ProgressCm >= -Params.TakeoffLeadCm && In.LateralSpeedCmS > Params.TakeoffMaxLateralCmS &&
				In.LateralSpeedCmS > In.AlongSpeedCmS && Retries < Params.MaxRetries && Params.RunUpCm >= VeloxBotManeuverRules::MinRunUpCm)
			{
				// Task M7 H11: near Start and mostly across the link (Nemus Log_Land meets the log next to the take-off): brake
				// into the run-up now instead of running across the narrow strip to the edge (below_start: off the log's side).
				++Retries;
				EnterPhase(EVeloxManeuverPhase::BackUp, In.Time);
				Out.WishToward = EVeloxManeuverWish::AwayFromStart;
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
