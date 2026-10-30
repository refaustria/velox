// Velox - world-free nav-link traversal state machine (ADR 0005 section 4): jump pad, drop, gap jump, fast gap jump and
// the Q3 BotRocketJump sequence. Fed one FVeloxManeuverInput per tick by the path follower, it answers where to wish,
// whether to hold jump / fire and which pitch to force; the follower turns that into FVeloxBotInput. Unit-tested in
// Velox.Bots.Maneuver.

#pragma once

#include "CoreMinimal.h"

/**
 * Traversal kinds, one per EVeloxNavLinkType value in the same order (task 01 owns the reflected link enum on
 * AVeloxNavLink; the follower maps it with a switch). Kept separate so this header stays free of UHT.
 */
enum class EVeloxManeuverType : uint8
{
	JumpPad,
	Drop,
	Jump,
	JumpFast,
	RocketJump,
};

enum class EVeloxManeuverPhase : uint8
{
	Idle,
	/** Walk to Start (pad, rocket jump) or toward End (drop, gaps). */
	Approach,
	/** JumpFast too slow at the takeoff point: back away from Start for a run-up. */
	BackUp,
	/** RocketJump: waiting at Start for the rocket launcher switch. */
	Switch,
	/** RocketJump: forward toward End for RunUpSeconds. */
	RunUp,
	/** RocketJump: jump + fire sent, waiting for the explosion's knockback. */
	Launched,
	/** Off the ground, steering toward End until landing. */
	Airborne,
	Finished,
};

enum class EVeloxManeuverResult : uint8
{
	None,
	Ok,
	/** Landed farther than EndAcceptanceCm from End. */
	Missed,
	/** RocketJump: no impulse within ImpulseTimeoutSeconds. */
	NoImpulse,
	/** JumpFast: below the speed fraction at the takeoff point after the retries. */
	TooSlow,
	/** RocketJump: eligibility (health, rockets) lost before the launch. */
	Ineligible,
};

/** `result=` value of the VeloxBotLink log line (ok | missed | aborted, ADR section 4) and a detail reason. */
inline const TCHAR* LexToString(EVeloxManeuverResult Result)
{
	switch (Result)
	{
	case EVeloxManeuverResult::Ok: return TEXT("ok");
	case EVeloxManeuverResult::Missed: return TEXT("missed");
	case EVeloxManeuverResult::NoImpulse: return TEXT("noimpulse");
	case EVeloxManeuverResult::TooSlow: return TEXT("tooslow");
	case EVeloxManeuverResult::Ineligible: return TEXT("ineligible");
	default: return TEXT("none");
	}
}

/** Where the follower should steer this tick. */
enum class EVeloxManeuverWish : uint8
{
	Start,
	End,
	/** Task 02 refinement: away from Start along the incoming segment (JumpFast back-up). */
	AwayFromStart,
};

/** Per-link tunables (link properties and UVeloxBotSettings; defaults = the ADR 0005 section 4 / 11 values). */
struct FVeloxManeuverParams
{
	EVeloxManeuverType Type = EVeloxManeuverType::Drop;
	/** Pad / rocket jump: arrival radius at Start, cm. */
	float StartAcceptanceCm = 30.f;
	/** Landing within this of End counts as ok (UVeloxBotSettings::LinkEndAcceptanceCm). */
	float EndAcceptanceCm = 150.f;
	/** Jump / JumpFast: hold jump from this distance before Start (UVeloxBotSettings::TakeoffLeadCm). */
	float TakeoffLeadCm = 100.f;
	/** JumpFast: AVeloxNavLink::RequiredSpeedCmS. */
	float RequiredSpeedCmS = 0.f;
	/** JumpFast: jump only at >= RequiredSpeedCmS x this. */
	float RequiredSpeedFraction = 0.9f;
	/** JumpFast: back-up distance from Start before the retry, cm. */
	float RunUpCm = 600.f;
	/** JumpFast: retries after a too-slow approach. */
	int32 MaxRetries = 1;
	/** RocketJump: AVeloxNavLink::RunUpSeconds / RocketJumpPitchDeg. */
	float RunUpSeconds = 0.3f;
	float RocketJumpPitchDeg = 80.f;
	/** RocketJump: abort when the explosion did not launch the pawn within this. */
	float ImpulseTimeoutSeconds = 0.3f;
};

/** One tick of observations (all world-free). */
struct FVeloxManeuverInput
{
	bool bOnGround = true;
	float Speed2D = 0.f;
	float DistToStart = 0.f;
	float DistToEnd = 0.f;
	/** An external impulse (knockback) arrived since the last tick. */
	bool bImpulseReceived = false;
	/** RocketJump: the rocket launcher is the current weapon and its switch has ended (SwitchEndTime passed). */
	bool bSwitchDone = false;
	/** RocketJump: health / rockets still allow the jump (section 4 eligibility). */
	bool bEligible = true;
	/** Seconds (world time). */
	double Time = 0.0;
};

struct FVeloxManeuverOutput
{
	EVeloxManeuverWish WishToward = EVeloxManeuverWish::End;
	/** Task 02 refinement: 0 while standing (waiting for the switch), else 1. */
	float WishScale = 1.f;
	bool bJumpHeld = false;
	bool bFire = false;
	/** Force the view pitch to PitchDeg this tick (the rocket-jump tick). */
	bool bForcePitch = false;
	float PitchDeg = 0.f;
	/** Task 02 refinement: face End with the view yaw (rocket jump switch / run-up / launch). */
	bool bFaceEnd = false;
	/** Task 02 refinement: select the rocket launcher (rocket jump until launched). */
	bool bWantRocketLauncher = false;
	bool bDone = false;
	bool bAbort = false;
	EVeloxManeuverResult Reason = EVeloxManeuverResult::None;
};

struct VELOX_API FVeloxBotManeuver
{
	EVeloxManeuverType Type = EVeloxManeuverType::Drop;
	FVector Start = FVector::ZeroVector;
	FVector End = FVector::ZeroVector;
	EVeloxManeuverPhase Phase = EVeloxManeuverPhase::Idle;
	double StartTime = 0.0;
	double PhaseStartTime = 0.0;
	int32 Retries = 0;
	EVeloxManeuverResult Result = EVeloxManeuverResult::None;
	FVeloxManeuverParams Params;
	/** Airborne phase: the pawn has been seen off the ground (a landing needs a preceding flight). */
	bool bSeenAirborne = false;

	/** Starts a traversal (Start / End are stored for the follower; the machine itself only uses the input distances). */
	void Begin(const FVeloxManeuverParams& InParams, const FVector& InStart, const FVector& InEnd, double Time);

	/** One tick. After bDone / bAbort the machine is Finished and returns the same final output. */
	FVeloxManeuverOutput Tick(const FVeloxManeuverInput& In);

	bool IsActive() const { return Phase != EVeloxManeuverPhase::Idle && Phase != EVeloxManeuverPhase::Finished; }

private:
	void EnterPhase(EVeloxManeuverPhase NewPhase, double Time);
	FVeloxManeuverOutput Finish(EVeloxManeuverResult InResult, double Time);
	FVeloxManeuverOutput TickAirborne(const FVeloxManeuverInput& In);
};
