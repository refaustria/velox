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
	/** ADR 0005 D28: the traversal ran TimeoutSeconds (every phase but Launched, which has ImpulseTimeoutSeconds). */
	Timeout,
	/** ADR 0005 D28: on the ground in Approach / BackUp more than BelowStartAbortCm below the link start (another surface). */
	BelowStart,
	/**
	 * Task m5-06b / 06c: the traversal began inside a foreign flight (FVeloxManeuverInput::bBeganInFlight: airborne longer
	 * than the bot's own hop - a pad launch, a drop, knockback) and never touched the ground before its landing (no own
	 * launch either): the flight that ended away from End was not this link's. Not an attempt (the path is re-queried).
	 * A traversal begun in the bot's own hop is an ordinary attempt (Ok / Missed).
	 */
	AirStart,
	/**
	 * Task m5-06b / 06c: an external impulse (a pad under the landing, a rocket hit) re-launched the pawn during the flight
	 * away from End. Next to End it is Ok: within EndAcceptanceCm, or (Drop) at the end's level (HeightAboveEndCm <=
	 * EndAcceptanceCm / 2) within 2 x EndAcceptanceCm. Not an attempt (the path is re-queried).
	 */
	Relaunched,
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
	case EVeloxManeuverResult::Timeout: return TEXT("timeout");
	case EVeloxManeuverResult::BelowStart: return TEXT("below_start");
	case EVeloxManeuverResult::AirStart: return TEXT("air_start");
	case EVeloxManeuverResult::Relaunched: return TEXT("relaunched");
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
	/**
	 * Jump / JumpFast: the take-off window opens this far before Start along the link (UVeloxBotSettings::TakeoffLeadCm);
	 * in it a ledge ahead (FVeloxManeuverInput::bLedgeAhead) fires the jump (task M7 H11).
	 */
	float TakeoffLeadCm = 100.f;
	/**
	 * Jump / JumpFast (task M7 H11): the jump fires at the latest this far past Start along the link (UVeloxBotSettings::
	 * TakeoffMaxPastStartCm) when no ledge was seen first (a link without a drop at its start, a missed probe).
	 */
	float TakeoffMaxPastStartCm = 40.f;
	/** Jump / JumpFast (task M7 H11): XY length of the link (cm); 0 = no along-link speed check. */
	float LinkLengthCm = 0.f;
	/** Jump / JumpFast (task M7 H11): flight time of a jump from the start's level to the end's level (s); 0 = no check. */
	float GapAirSeconds = 0.f;
	/**
	 * Jump / JumpFast (task M7 H11): the landing may stop this far short of End (cm; UVeloxBotSettings::TakeoffLandInsetCm,
	 * the smaller of the 60 / 100 cm authoring insets, so the way back of a both-ways link is covered too).
	 */
	float TakeoffLandInsetCm = 60.f;
	/**
	 * Jump / JumpFast (task M7 H11): more speed across the link than this at the take-off counts as too slow, and near
	 * Start an approach mostly across the link backs up at once (cm/s; UVeloxBotSettings::TakeoffMaxLateralCmS; 0 = off).
	 */
	float TakeoffMaxLateralCmS = 0.f;
	/**
	 * Jump / JumpFast (task M7 H14): how far the landing zone's nav reaches past End along the link (cm; the follower's nav
	 * raycast up to UVeloxBotSettings::TakeoffLandProbeCm); < 0 = farther (not short): no take-off speed cap.
	 */
	float LandBeyondEndCm = -1.f;
	/** Jump / JumpFast (task M7 H14): the capped flight lands this far short of that far nav edge (UVeloxBotSettings::TakeoffLandFarInsetCm). */
	float TakeoffLandFarInsetCm = 40.f;
	/** JumpFast: AVeloxNavLink::RequiredSpeedCmS. */
	float RequiredSpeedCmS = 0.f;
	/** JumpFast: jump only at >= RequiredSpeedCmS x this. */
	float RequiredSpeedFraction = 0.9f;
	/** Jump / JumpFast: back-up distance from Start before the retry, cm (H11: along the link, clamped to the mesh by the follower). */
	float RunUpCm = 600.f;
	/** Jump / JumpFast: retries after a too-slow take-off (H11: JUMP too, on the along-link speed). */
	int32 MaxRetries = 1;
	/** RocketJump: AVeloxNavLink::RunUpSeconds / RocketJumpPitchDeg. */
	float RunUpSeconds = 0.3f;
	float RocketJumpPitchDeg = 80.f;
	/** RocketJump: abort when the explosion did not launch the pawn within this. */
	float ImpulseTimeoutSeconds = 0.3f;
	/** D28: abort a traversal running this long (s; UVeloxBotSettings::LinkTimeoutSeconds). <= 0 disables. */
	float TimeoutSeconds = 6.f;
	/** D28: abort Approach / BackUp on the ground this far below the link start (cm; UVeloxBotSettings::LinkBelowStartCm). */
	float BelowStartAbortCm = 100.f;
};

/** One tick of observations (all world-free). */
struct FVeloxManeuverInput
{
	bool bOnGround = true;
	float Speed2D = 0.f;
	float DistToStart = 0.f;
	float DistToEnd = 0.f;
	/**
	 * Task M7 H11: signed distance of the feet past Start along the link's XY axis Start -> End (cm; negative = before
	 * Start). Jump / JumpFast take off on it (the radial DistToStart fired the jump up to TakeoffLeadCm early).
	 */
	float ProgressCm = 0.f;
	/** Task M7 H11: the XY distance the pawn covers this tick (Speed2D x DeltaTime, cm): the take-off fires one tick early. */
	float StepCm = 0.f;
	/** Task M7 H11: the XY velocity along the link axis (cm/s; negative = away from End). */
	float AlongSpeedCmS = 0.f;
	/** Task M7 H11: the XY speed across the link axis (cm/s, >= 0). */
	float LateralSpeedCmS = 0.f;
	/**
	 * Task M7 H11 (Jump / JumpFast, on the ground in the take-off window): no floor just ahead of the feet along the link
	 * axis (the follower's down trace StepCm + UVeloxBotSettings::TakeoffLedgeProbeCm ahead): the take-off edge.
	 */
	bool bLedgeAhead = false;
	/** An external impulse (knockback) arrived since the last tick. */
	bool bImpulseReceived = false;
	/** RocketJump: the rocket launcher is the current weapon and its switch has ended (SwitchEndTime passed). */
	bool bSwitchDone = false;
	/** RocketJump: health / rockets still allow the jump (section 4 eligibility). */
	bool bEligible = true;
	/** D28: link start z - feet z (cm; positive = the pawn is below the start). */
	float HeightBelowStartCm = 0.f;
	/** Task m5-06b: feet z - link end z (cm; positive = the pawn is above the end). */
	float HeightAboveEndCm = 0.f;
	/**
	 * Task m5-06c (review D1): the pawn is in a foreign flight when the traversal begins (airborne longer than
	 * VeloxBotRules::HopAirSeconds: a pad launch, a drop, knockback; the bot's own hop is not one). Read only by
	 * Begin(Params, Start, End, In), which stores it; later ticks ignore it.
	 */
	bool bBeganInFlight = false;
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

namespace VeloxBotManeuverRules
{
	/** Task M7 H11: a back-up shorter than this (cm, the mesh behind the link start) gives no run-up worth a retry. */
	constexpr float MinRunUpCm = 100.f;

	/**
	 * Task M7 H11: along-link take-off speed (cm/s) that lands LandInsetCm short of End from ProgressCm past Start after
	 * AirSeconds of flight: max(0, LinkLengthCm - ProgressCm - LandInsetCm) / AirSeconds; 0 without a flight time.
	 */
	inline float GapTakeoffSpeed(float LinkLengthCm, float ProgressCm, float LandInsetCm, float AirSeconds)
	{
		return AirSeconds > UE_KINDA_SMALL_NUMBER ? FMath::Max(0.f, LinkLengthCm - ProgressCm - LandInsetCm) / AirSeconds : 0.f;
	}

	/**
	 * Task M7 H14: the largest along-link take-off speed (cm/s) whose flight from ProgressCm past Start still lands
	 * FarInsetCm short of the landing zone's far nav edge (LandBeyondEndCm past End): (LinkLengthCm + LandBeyondEndCm -
	 * FarInsetCm - ProgressCm) / AirSeconds, at least 1 cm/s. 0 = no cap (LandBeyondEndCm < 0: the zone is not short; no
	 * flight time).
	 */
	inline float GapTakeoffMaxSpeed(float LinkLengthCm, float ProgressCm, float LandBeyondEndCm, float FarInsetCm, float AirSeconds)
	{
		if (LandBeyondEndCm < 0.f || AirSeconds <= UE_KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		return FMath::Max(1.f, (LinkLengthCm + LandBeyondEndCm - FarInsetCm - ProgressCm) / AirSeconds);
	}

	/**
	 * Task M7 H14: shed speed (coast) only when the cap is above the speed the flight needs (GapTakeoffSpeed): a zone too
	 * short for both is left to the H11 rules.
	 */
	inline bool ShouldShedTakeoffSpeed(float AlongSpeedCmS, float MaxAlongCmS, float NeedAlongCmS)
	{
		return MaxAlongCmS > 0.f && MaxAlongCmS > NeedAlongCmS && AlongSpeedCmS > MaxAlongCmS;
	}

	/**
	 * Task M7 H11: flight time of a jump (JumpVelocity up, gravity |GravityZ|) from a start DropCm above the landing level
	 * (negative = the landing is higher): the later root of the ballistic height; the apex time when the level is out of
	 * reach; 0 without gravity.
	 */
	inline float JumpAirSeconds(float JumpVelocity, float GravityZ, float DropCm)
	{
		const float G = FMath::Abs(GravityZ);
		if (G < UE_KINDA_SMALL_NUMBER)
		{
			return 0.f;
		}
		const float Disc = JumpVelocity * JumpVelocity + 2.f * G * DropCm;
		return Disc > 0.f ? (JumpVelocity + FMath::Sqrt(Disc)) / G : JumpVelocity / G;
	}
}

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
	/** Task m5-06b: the pawn stood on the ground in a pre-flight phase of this traversal (else a landing away from End is AirStart). */
	bool bGroundSeen = false;
	/** Task m5-06b: the link's own launch impulse arrived (the pad's AddKnockback, the rocket jump's splash); a later one re-launches. */
	bool bLaunchImpulseSeen = false;
	/** Task m5-06c: FVeloxManeuverInput::bBeganInFlight at Begin (a landing away from End is AirStart only then). */
	bool bBeganInFlight = false;

	/** Starts a traversal (Start / End are stored for the follower; the machine itself only uses the input distances). */
	void Begin(const FVeloxManeuverParams& InParams, const FVector& InStart, const FVector& InEnd, double Time);
	/** Task m5-06c: Begin at In.Time, storing In.bBeganInFlight (the follower's observation at the traversal start). */
	void Begin(const FVeloxManeuverParams& InParams, const FVector& InStart, const FVector& InEnd, const FVeloxManeuverInput& In);

	/** One tick. After bDone / bAbort the machine is Finished and returns the same final output. */
	FVeloxManeuverOutput Tick(const FVeloxManeuverInput& In);

	bool IsActive() const { return Phase != EVeloxManeuverPhase::Idle && Phase != EVeloxManeuverPhase::Finished; }
	/**
	 * ADR 0008 3.6.3 pad discipline: the airborne phase of a JUMP_PAD traversal. TickAirborne itself still wishes toward
	 * End at scale 1 (the pre-M7 behaviour, kept for maps without hazards); on hazard maps the controller zeroes the move
	 * input while this is true (the pad's arc is computed for a pawn without input), aim stays live.
	 */
	bool IsPadFlight() const { return Type == EVeloxManeuverType::JumpPad && Phase == EVeloxManeuverPhase::Airborne; }

private:
	void EnterPhase(EVeloxManeuverPhase NewPhase, double Time);
	FVeloxManeuverOutput Finish(EVeloxManeuverResult InResult, double Time);
	FVeloxManeuverOutput TickAirborne(const FVeloxManeuverInput& In);
};
