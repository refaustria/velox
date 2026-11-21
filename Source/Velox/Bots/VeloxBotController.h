// Velox - bot controller (ADR 0005 sections 1, 2, 5, 12). An AAIController with a PlayerState that produces player input
// every server tick and applies it through the player entry points only (SetControlRotation, AddMovementInput,
// SetLocalMoveInput, AVeloxCharacter::SetJumpInput, Crouch/UnCrouch, ability input tags + ProcessAbilityInput). It never
// reads or writes velocity, never launches or teleports the pawn; UVeloxMovementComponent knows nothing about bots.
//
// Tick phases (section 2): Perceive -> Decide (forced goal, else UpdateGoal) -> Navigate (UVeloxBotPathFollowingComponent
// ::StepFollow, stuck handling) -> UpdateCombat (overlay) -> Act (view, weapon, fire) -> ApplyInput. Task 05 fills
// Perceive / UpdateCombat / Act's aim, task 06 UpdateGoal; this task ships the stubs (roam, idle aim along the path).

#pragma once

#include "AIController.h"
#include "Bots/VeloxBotCombat.h"
#include "Bots/VeloxBotCoreTypes.h"
#include "Bots/VeloxBotPathFollowingComponent.h"
#include "Bots/VeloxBotTypes.h"
#include "Math/RandomStream.h"
#include "Templates/SubclassOf.h"

#include "VeloxBotController.generated.h"

class AVeloxCharacter;
class AVeloxNavLink;
class AVeloxPickup;
class APlayerState;
class UNavigationQueryFilter;
class UVeloxBotDifficulty;

/**
 * Task m5-06b: the fight stand-off window - the bot's 2-D positions sampled every SampleSeconds over the last
 * FightStandoffSeconds (the first kept sample is the latest one at or before the window start).
 */
struct FVeloxStandoffWindow
{
	TArray<FVector2D, TInlineAllocator<40>> Positions;
	TArray<double, TInlineAllocator<40>> Times;
	void Reset()
	{
		Positions.Reset();
		Times.Reset();
	}
	/** Seconds covered by the kept samples (0 when empty). */
	double Span(double Now) const { return Times.Num() > 0 ? Now - Times[0] : 0.0; }
};

/** The bot's current goal (ADR 0005 section 2). */
struct FVeloxBotGoal
{
	EVeloxBotGoal Type = EVeloxBotGoal::None;
	TWeakObjectPtr<AVeloxPickup> Item;
	TWeakObjectPtr<APawn> Enemy;
	/** Point goals and roam targets (world, on or near the nav mesh). */
	FVector Point = FVector::ZeroVector;
	float Score = 0.f;
	double SetTime = 0.0;
};

/** All per-life state of a bot in one place (debug line, reset on possession). Server only. */
struct FVeloxBotState
{
	FVeloxBotGoal Goal;
	/** velox.bots.Goal / tests: the goal is kept until reached, taken or cleared; UpdateGoal is not consulted. */
	bool bForcedGoal = false;
	/** The path to the goal finished successfully (the bot stands at it). */
	bool bGoalArrived = false;

	// --- Path request ---
	bool bMoveActive = false;
	FAIRequestID MoveRequestId;
	FVector MoveTarget = FVector::ZeroVector;
	TSubclassOf<UNavigationQueryFilter> MoveFilter;
	double NextMoveTime = 0.0;
	int32 FailedMoves = 0;
	/** D35: the follower asked for a new path from here (segment line-of-walk blocked); EnsureMove issues it. */
	bool bRepathRequested = false;
	/** Reason of the pending repath request (a string literal; VeloxBotRepath reason=). */
	const TCHAR* RepathReason = nullptr;
	/** D35: repath requests issued this life (debug line repaths=). */
	int32 Repaths = 0;
	/** D44: world time of the first request of the pending repath (the issue logs age=; -1 = none pending). */
	double RepathRequestedAt = -1.0;
	/** Link fallbacks (section 4): a too-slow JumpFast forces _Walk, a rocket jump without impulse forces _Hop, until then. */
	double WalkFilterUntil = -1.0;
	double HopFilterUntil = -1.0;

	// --- Stuck detection (section 5) ---
	double SlowSince = -1.0;
	int32 StuckStrikes = 0;
	/** D18 progress rule: start of the current window (-1 = none), the 2-D distance to the segment target then, that target. */
	double ProgressSince = -1.0;
	float ProgressDist = 0.f;
	FVector2D ProgressTarget = FVector2D::ZeroVector;
	/** D27: pause the current progress window has used (s; reset with the window). */
	float ProgressPausedSeconds = 0.f;
	/** D30: after a fight_stall strike the decision does not see the enemy until then (world time; -1 = never). */
	double FightSuppressedUntil = -1.0;
	/** Task 06b: fight stand-off rule window (feet positions). */
	FVeloxStandoffWindow FightStandoff;
	/** Task M7 H15: fight dwell window (feet positions at 10 Hz over FightStandoffSeconds + FightDwellPeriodSeconds). */
	FVeloxStandoffWindow FightDwell;
	/** Task M7 H15: last time the fight dwell rule was watched (Fight goal); the window survives FightDwellGraceSeconds without it. */
	double FightDwellWatchedAt = -1.0;
	/** Task M7 H15: since when the bot stands under / over its item goal (VeloxBotRules::IsUnderGoal; -1 = not). */
	double UnderGoalSince = -1.0;
	/**
	 * Task M7 H13: time of this Fight goal (Fight -> Fight kept) in which the void stopped the closing in (the overlay's
	 * back-off / no wish, or the ground edge guard); FightEdgeYieldSeconds of it yields like a stand-off (an enemy across a gap).
	 */
	double FightEdgeBlockedSeconds = 0.0;
	int32 FightEdgeYields = 0;
	/** Task 06b: fight stand-offs that yielded the Fight goal this life (VeloxBotFightYield). */
	int32 FightYields = 0;
	/** Task 06b: world time the pawn left the ground (-1 = on the ground); a flight longer than a hop pauses the progress window. */
	double AirborneSince = -1.0;

	// --- Off-mesh recovery (task 04b): feet on the ground but off the nav mesh, so no path can start here ---
	/** First check that found the feet off the mesh (-1 = on the mesh). */
	double OffMeshSince = -1.0;
	double NextOffMeshCheckTime = 0.0;
	/** A direct (non-pathfinding) move to the nearest nav point is the active move. */
	bool bOffMeshRecovery = false;
	double OffMeshMoveUntil = 0.0;
	int32 OffMeshAttempts = 0;
	/** Set by ApplyInput: the fire ability's NextFireTime advanced this tick (D14, budget fire exclusion). */
	bool bShotFiredThisTick = false;
	int32 OffMeshRecoveries = 0;
	/** D44: recovery attempts that found no reachable nav point this life (`result=no_nav_point`). */
	int32 OffMeshNoNavPoints = 0;
	/** D45: attempts whose fallback was blocked / a repeat with no tangent candidate (`result=no_clear_point`) this life. */
	int32 OffMeshNoClearPoints = 0;
	/** D45: recovery targets found by the tangent probes along a blocking wall (`target=tangent`) this life. */
	int32 OffMeshTangentTargets = 0;
	/** Target of the last recovery attempt (the next attempt avoids it). */
	FVector OffMeshLastTarget = FVector::ZeroVector;

	// --- ADR 0008 3.6.3/4 (task M7 H5): only on maps with a hazard volume (VeloxBotCombatGlue::IsEdgeAware) ---
	/** The current airborne stretch has been seen by ApplyHazardBehaviours (reset on the ground). */
	bool bAirborneSeen = false;
	/**
	 * The current airborne stretch is not the bot's own hop: it began without the jump key, a knockback grace was seen in
	 * it, or it outlasted the hop (VeloxBotRules::IsForeignFlight). Latched until the ground.
	 */
	bool bForeignFlight = false;
	/** 3.6.3: the airborne recovery steered in the current flight (counted once per flight in AirRecoveryFlights). */
	bool bAirRecoverySteered = false;
	/** 3.6.3: the nav point the airborne recovery steers to (valid while bAirRecoveryHasTarget). */
	FVector AirRecoveryTarget = FVector::ZeroVector;
	bool bAirRecoveryHasTarget = false;
	/**
	 * 3.6.3: flights in which the airborne recovery steered / ticks it steered / pad-flight ticks on a hazard map (this
	 * life; ADR 0008 G4, task M7 H8: the pad's ballistic arrive wish is the only move input, zero while on course).
	 */
	int32 AirRecoveryFlights = 0;
	int32 AirRecoveryTicks = 0;
	int32 PadDisciplineTicks = 0;
	/** H8: pad-flight ticks on a hazard map in which the arrive wish steered (not on course). */
	int32 PadArriveTicks = 0;
	/** H8 (review F4): the airborne recovery's safe target for a landing over lava, re-searched every HazardEscapeSearchSeconds. */
	FVector AirRecoverySafeTarget = FVector::ZeroVector;
	bool bAirRecoveryHasSafeTarget = false;
	double NextAirRecoverySearchTime = -1.0;
	/** H8 (review F4): the own-hop landing test runs at most every UVeloxBotSettings::EdgeProbeCheckSeconds (reset on the ground). */
	double NextHazardLandingProbeTime = -1.0;
	/** Task M7 H7: feet height of the last ground tick (the level the bot's own hop lands on). */
	double LastGroundFeetZ = 0.0;
	/** H7: in the bot's own hop the predicted landing lies in lava: steered to the nearest safe nav point (flights / ticks, this life). */
	bool bHazardLandingSteered = false;
	FVector HazardLandingTarget = FVector::ZeroVector;
	bool bHazardLandingHasTarget = false;
	int32 HazardLandingFlights = 0;
	int32 HazardLandingTicks = 0;
	/** Task M7 H13 ground edge guard: ticks it turned the wish inward / episodes (>= 0.5 s apart) this life. */
	int32 EdgeGuardTicks = 0;
	int32 EdgeGuardEpisodes = 0;
	double LastEdgeGuardTime = -1.0;
	/** H13: own hops whose predicted landing has no ground handed to the airborne recovery (flights, this life). */
	int32 OwnHopRecoveries = 0;
	/** H13 soak diagnostics (VeloxBotFall on a void death): the take-off of the current / last airborne stretch. */
	FVector TakeoffAt = FVector::ZeroVector;
	FVector TakeoffVelocity = FVector::ZeroVector;
	double TakeoffTime = -1.0;
	bool bTakeoffJump = false;
	bool bTakeoffDodge = false;
	bool bTakeoffOnLink = false;
	bool bTakeoffFight = false;
	bool bFlightKnocked = false;
	/**
	 * H13: the current flight is a jump pad launch the bot took off its link (the path crossed the pad's trigger): the
	 * pad's link end is steered with the ballistic arrive wish (as on the link) instead of the airborne recovery.
	 */
	bool bOffLinkPadFlight = false;
	FVector OffLinkPadEnd = FVector::ZeroVector;
	int32 OffLinkPadFlights = 0;
	/** H13: highest upward speed of the current flight (a jump pad launch off its link shows as > 1.5 x JumpVelocity). */
	float FlightMaxVz = 0.f;
	FString TakeoffLink;

	/** 3.6.4: the hazard escape owns the input (feet in lava, until grounded outside it or HazardEscapeMaxSeconds). */
	bool bHazardEscape = false;
	double HazardEscapeSince = -1.0;
	double NextHazardEscapeSearchTime = 0.0;
	/** 3.6.4: where the escape heads (a nav point outside every hazard volume, else the nearest side of the volume). */
	FVector HazardEscapeTarget = FVector::ZeroVector;
	bool bHazardEscapeHasTarget = false;
	/** 3.6.4: escapes started / finished out of the lava this life. */
	int32 HazardEscapes = 0;
	int32 HazardEscapesOut = 0;

	// --- Input ---
	FVeloxBotNavOutput Nav;
	FVeloxBotInput Pending;
	/** Key state last sent to the pawn (edges for jump, crouch, fire). */
	bool bJumpApplied = false;
	bool bCrouchApplied = false;
	bool bFireApplied = false;
	/** The view the next UpdateControlRotation applies. */
	FRotator View = FRotator::ZeroRotator;

	// --- Bookkeeping ---
	double PossessTime = 0.0;
	double NextDebugTime = 0.0;
	double NextRoamTime = 0.0;
	int32 LinksOk = 0;
	int32 LinksFailed = 0;
	/** Controller time per tick, microseconds (rough per-bot cost; section 13 stats land in task 05). */
	double TickMicrosSum = 0.0;
	double TickMicrosMax = 0.0;
	int32 TickCount = 0;
};

/** World-free rules of the controller glue (unit-tested in Velox.Bots.ControllerRules). */
namespace VeloxBotRules
{
	/** Per-life random seed: HashCombine(MasterSeed, BotIndex, LifeCount) (no XOR collisions between bots and lives). */
	VELOX_API int32 BotSeed(int32 MasterSeed, int32 BotIndex, int32 LifeCount);

	/**
	 * Key presence a player's stick would produce for a world wish (section 1, step 3): (forward, right) of WishDir in the
	 * view yaw frame, times WishScale. bSnapKeys (CPM air control on): the minor component of the unit wish is dropped
	 * when its magnitude is below SnapThreshold, so the CPM rules see pure forward or pure side keys.
	 */
	VELOX_API FVector2D MoveKeys(const FVector2D& WishDirWorld, float WishScale, float ViewYawDeg, bool bSnapKeys, float SnapThreshold);

	/** Turns Current toward Desired by at most MaxDegPerSec * Dt (yaw by the shortest way, pitch clamped to +-89). */
	VELOX_API FRotator TurnToward(const FRotator& Current, const FRotator& Desired, float MaxDegPerSec, float Dt);

	/**
	 * Stuck rule: SlowSince tracks when the speed fell below SpeedThreshold while tracking (an active path, no link). True
	 * once that lasted StuckSeconds (SlowSince then restarts at Now); not tracking or fast enough resets it.
	 */
	VELOX_API bool UpdateStuck(double& SlowSince, bool bTracking, float Speed2D, float SpeedThreshold, double Now, float StuckSeconds);

	/**
	 * Progress stuck rule (ADR 0005 D18), beside UpdateStuck: while tracking, the 2-D distance Dist2D to the current
	 * segment target must shrink by >= ProgressCm within StuckSeconds. The window (Since, StartDist, WindowTarget) starts
	 * at the first tracked call, and restarts at Now with the current distance when the target moves (a new segment or
	 * re-query; 2-D, more than 1 cm), when the progress is made (returns false), or when the window ran StuckSeconds
	 * without it (returns true: a strike). bPaused (the combat overlay owns the wish): the window's clock stops (Since
	 * moves forward by Dt) and the start distance follows the current one, so only follower time counts and the
	 * follower is measured from where the overlay left the bot; a pause does not reset the clock. D27: a window may be
	 * paused for at most MaxPauseSeconds in total (PausedSeconds, reset with the window); after that it runs with the
	 * overlay active. Not tracking clears the window (Since = -1) and returns false.
	 */
	VELOX_API bool UpdateStuckProgress(double& Since, float& StartDist, FVector2D& WindowTarget, float& PausedSeconds, bool bTracking, bool bPaused,
		const FVector2D& Target, float Dist2D, float ProgressCm, double Now, float Dt, float StuckSeconds, float MaxPauseSeconds,
		bool bOnGround = true, bool bFlight = false);
	// Task 06b (the two trailing parameters): a window never expires while airborne (bOnGround false: the strike waits for
	// the next ground tick, a bunny hop touches the ground once per hop), and during a flight (bFlight: airborne longer
	// than the bot's own hop - a pad launch, a drop, a fall, knockback) the window is paused like under the overlay but
	// without the D27 cap (the flight is bounded by gravity): the clock stops and the start distance follows the bot,
	// so the follower is measured from where it lands.

	/**
	 * Task 06b: the airborne stretch that started at AirborneSince (-1 = on the ground) is a flight, not the bot's own
	 * hop: airborne for longer than HopAirSeconds (2 x JumpVelocity / |g| plus a slack, the controller computes it).
	 */
	VELOX_API bool IsFlight(bool bOnGround, double AirborneSince, double Now, float HopAirSeconds);

	/**
	 * Task 06c: the bot's own hop air time plus a slack, the IsFlight threshold: 2 x JumpVelocity / |GravityZ| +
	 * SlackSeconds (just SlackSeconds without gravity). Shared by the controller's progress rule and the path follower's
	 * FVeloxManeuverInput::bBeganInFlight.
	 */
	VELOX_API float HopAirSeconds(float JumpVelocity, float GravityZ, float SlackSeconds);

	/**
	 * Task 06b fight stand-off rule (beside D30): true (a stand-off: the Fight goal yields) when, while bWatched (Fight
	 * goal, not on a link, not suppressed), every position the bot had over the last Seconds lies within RadiusCm (2-D)
	 * of where it is now. Positions are sampled every SampleSeconds into Window; not watched or firing clears it, so a
	 * goal change or a yield starts a fresh Seconds. Airborne time counts (jumping in place is no progress).
	 */
	VELOX_API bool UpdateFightStandoff(FVeloxStandoffWindow& Window, bool bWatched, const FVector2D& Pos, double Now, float RadiusCm, float Seconds,
		float SampleSeconds = 0.25f);

	/**
	 * Task M7 H15 fight dwell rule (beside the 06b stand-off): true when, while bWatched, there is an anchor sample A at
	 * least Seconds old whose positions at A + PeriodSeconds, A + 2 PeriodSeconds, ... up to Seconds all lie within
	 * RadiusCm (2-D) of A's. That is the soak's 1 Hz dwell run (ADR 0006 section 7, D34: consecutive 1 Hz samples within
	 * 150 cm of the run's first) at every sampling phase. A strafing fight in the open oscillates around a fixed point
	 * with an amplitude above RadiusCm, so the 06b test (every sample within RadiusCm of now) never holds while the 1 Hz
	 * samples stay inside the circle. Positions are sampled every SampleSeconds (a matched sample lies within that of its time);
	 * not watched clears the window; a hit clears it too, unless !bMayFire (sampled, not judged: the window is kept and
	 * false returned). PeriodSeconds <= 0 turns the rule off.
	 */
	VELOX_API bool UpdateFightDwell(FVeloxStandoffWindow& Window, bool bWatched, const FVector2D& Pos, double Now, float RadiusCm, float Seconds,
		float PeriodSeconds, float SampleSeconds = 0.1f, bool bMayFire = true);

	/**
	 * Task 06b: 3-D arrival test at a goal target (item location, roam / chase point): within Radius2D horizontally and
	 * within MaxDzCm vertically (feet vs target; the controller passes the nav agent height, so a bot on another floor
	 * level - under or over the item - is not at it).
	 */
	VELOX_API bool IsAtGoal(const FVector& Feet, const FVector& Target, float Radius2D, float MaxDzCm);

	/** Task M7 H15: within Radius2D of Target horizontally but more than MaxDzCm above or below it (the item on another level). */
	VELOX_API bool IsUnderGoal(const FVector& Feet, const FVector& Target, float Radius2D, float MaxDzCm);

	/**
	 * D30 fight-stall predicate: the speed rule also watches a pathless fight (Goal == Fight, no active move, the goal's
	 * enemy visible), on the ground and not on a link. A Fight goal without sight has a chase path (D12) and is tracked.
	 */
	VELOX_API bool IsFightWatched(EVeloxBotGoal Goal, bool bMoveActive, bool bEnemyVisible, bool bOnGround, bool bOnLink);

	/**
	 * Off-mesh recovery rule (task 04b), evaluated at the check cadence while on the ground: on the mesh resets OffMeshSince
	 * (-1) and returns false; off the mesh starts the clock and returns true once it ran GraceSeconds, unless a recovery
	 * move is running (bRecoveryMoveActive and Now < RecoveryMoveUntil).
	 */
	VELOX_API bool UpdateOffMesh(double& OffMeshSince, bool bOnMesh, bool bRecoveryMoveActive, double RecoveryMoveUntil, double Now, float GraceSeconds);

	/**
	 * ADR 0005 D45: the 2-D tangent of a blocking wall from its impact normal (unit, perpendicular to the normal's 2-D part).
	 * False when the normal's 2-D length is below 0.5 (a floor or ceiling hit has no wall to follow).
	 */
	VELOX_API bool WallTangent(const FVector& ImpactNormal, FVector2D& OutTangent);

	/**
	 * Name for a new bot: Requested if given and free, else the first free entry of Names; a taken name gets the lowest
	 * free numeric suffix starting at 2 ("Celer2"). Comparison ignores case. "Bot" when Names is empty.
	 */
	VELOX_API FString NextBotName(const TArray<FString>& Names, const TArray<FString>& Taken, const FString& Requested);

	/** ?Bots=N capped to the mode's slots: MaxActive - 1 (one human slot) unless bFill; MaxActive <= 0 = no cap. */
	VELOX_API int32 UrlBotCount(int32 Requested, int32 MaxActive, bool bFill);
	/**
	 * ADR 0008 3.6.3 (task M7 H5): the airborne stretch is a foreign flight, not the bot's own hop, when it began without
	 * the jump key held (walked off, knocked off), a knockback grace is running (an impulse: a rocket, a pad off a link),
	 * or it is a flight by IsFlight (longer than the own hop). The controller latches the answer until the ground.
	 */
	VELOX_API bool IsForeignFlight(bool bJumpHeldAtTakeoff, float KnockbackGraceSeconds, bool bIsFlight);
	/**
	 * ADR 0008 3.6.3: seconds until a ballistic flight (VelocityZ, gravity magnitude Gravity > 0) is HeightAboveLevelCm
	 * lower than now (the descending crossing; HeightAboveLevelCm >= 0: the level is at or below the feet). 0 for a
	 * level above the flight's reach or without gravity.
	 */
	VELOX_API float FlightSecondsToLevel(float VelocityZ, float HeightAboveLevelCm, float Gravity);
	/**
	 * ADR 0008 3.6.3: the air-recovery wish from the predicted landing (2-D) to the recovery nav point: zero when the
	 * landing is within OnCourseCm of it (the flight already lands there), else the unit direction landing -> target
	 * (the correction the velocity needs, not feet -> target).
	 */
	/**
	 * Task M7 H13 ground edge guard: Wish with its component along Inward replaced by one unit of Inward (the part along
	 * the edge kept), normalised; zero for a zero Inward. A zero wish gives Inward.
	 */
	VELOX_API FVector2D EdgeGuardWish(const FVector2D& Wish, const FVector2D& Inward);

	/** Task M7 H13: an airborne upward speed above this x JumpVelocity is a launch (jump pad), not the bot's own hop (641 cm/s; pads 900+). */
	extern VELOX_API const float PadLaunchVzFactor;

	/** Task M7 H13: SetGoal keeps the fight stand-off window when a Fight goal is replaced by a Fight goal (re-evaluation, target switch). */
	VELOX_API bool KeepsFightStandoff(EVeloxBotGoal OldType, EVeloxBotGoal NewType);

	VELOX_API FVector2D AirRecoveryWish(const FVector2D& Landing, const FVector2D& Target, float OnCourseCm);
	/**
	 * ADR 0008 3.6.4: the escape point when no nav point outside the hazard is found: the nearest side of an oriented box
	 * (Center, unit X axis AxisX in 2-D, half extents HalfX / HalfY) seen from Feet, pushed OutCm beyond that side.
	 */
	VELOX_API FVector2D HazardBoxExit(const FVector2D& Feet, const FVector2D& Center, const FVector2D& AxisX, float HalfX, float HalfY, float OutCm);
}

UCLASS(Config = Game)
class VELOX_API AVeloxBotController : public AAIController
{
	GENERATED_BODY()

public:
	AVeloxBotController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Preset of this bot (null = UVeloxBotSettings::DefaultDifficulty). The POD params are rebuilt on every possession. */
	void SetDifficulty(const UVeloxBotDifficulty* InDifficulty);
	const UVeloxBotDifficulty* GetDifficulty() const { return Difficulty; }
	const FVeloxBotDifficultyParams& GetDifficultyParams() const { return Params; }

	/** Stagger index assigned by UVeloxBotSubsystem (0..N-1, reused after a kick). */
	int32 GetBotIndex() const { return BotIndex; }
	void SetBotIndex(int32 InIndex) { BotIndex = InIndex; }
	/** Possessions so far (1 on the first life). */
	int32 GetLifeCount() const { return LifeCount; }
	const FVeloxBotState& GetBotState() const { return State; }
	/** Task M7 H13: a void damage event hit this bot's pawn: logs VeloxBotFall (the take-off that led there; soak diagnostics). */
	void NoteVoidDeath();
	/** Perception / aim / fire state (task 05): enemies, target, aim model. Read by the debug line and task 06's goal choice. */
	const FVeloxBotCombatState& GetCombatState() const { return Combat; }
	UVeloxBotPathFollowingComponent* GetBotPathFollowing() const;
	/** Player name of the PlayerState ("?" without one). */
	FString GetBotName() const;

	// --- Forced goals (velox.bots.Goal, functional tests) ---
	void SetForcedItemGoal(AVeloxPickup* Item);
	void SetForcedPointGoal(const FVector& Point);
	/** Back to the brain's own goals. */
	void ClearForcedGoal();

	/** Per-bot input freeze (tests); velox.bots.Freeze freezes every bot. Frozen bots release every key. */
	void SetFrozen(bool bInFrozen) { bFrozen = bInFrozen; }
	bool IsFrozen() const;

	/** Query filter for this tick from the preset and the bot's state (section 4): Full / Hop / Walk. */
	TSubclassOf<UNavigationQueryFilter> GetNavFilter() const;

	/**
	 * Rocket-jump eligibility for Link (section 4, review F1 / D11): the preset rocket-jumps, the bot owns the launcher and
	 * a rocket, and its health is at least the link's MinHealthForRocketJump. bOutSwitchDone: the launcher is the current
	 * weapon and its switch has ended. Asked by the follower inside the maneuver begin / tick (no one-tick-stale state).
	 */
	bool EvaluateRocketJump(const AVeloxNavLink& Link, bool& bOutSwitchDone) const;

	/** A FindPathSync was run for this bot in its current tick (item router, own MoveTo; VeloxBotBudget paths=/s, D10). */
	void NotePathQuery() { ++TickPathQueries; }

	/** Called by the follower when a link traversal ends: VeloxBotLink log line, counters, re-query on failure. */
	void NotifyLinkFinished(const FVeloxBotLinkReport& Report);

	/**
	 * D35: the follower's current segment is blocked; EnsureMove requests a new path under its usual guards (ground
	 * only, RepathIntervalSeconds) and replaces the path in place. No StopMove, no stuck strike. Reason: a literal.
	 */
	void RequestRepath(const TCHAR* Reason);
	/** D35: repath requests issued this life (functional tests). */
	int32 GetRepathCount() const { return State.Repaths; }
	/** A path move (or the off-mesh recovery move) is active. */
	bool IsMoveActive() const { return State.bMoveActive; }

	/** D30: a fight_stall strike hides the visible enemy from the goal decision until FightSuppressedUntil. */
	bool IsFightSuppressed(double Time) const { return Time < State.FightSuppressedUntil; }

	/** One `VeloxBot name= goal= ...` line (velox.bots.Debug, velox.bots.List). */
	FString DescribeForDebug() const;

	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnVeloxBotLinkFinished, AVeloxBotController* /*Bot*/, const FVeloxBotLinkReport& /*Report*/);
	/** Every finished link traversal (functional tests). */
	FOnVeloxBotLinkFinished OnLinkFinished;

	//~AActor interface
	virtual void Tick(float DeltaSeconds) override;
	//~End of AActor interface

	//~AAIController interface
	/** Does not call Super (section 1): applies State.View (pitch included), FaceRotation for the capsule yaw. */
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;
	//~End of AAIController interface

	// --- Tunables (DefaultGame.ini [/Script/Velox.VeloxBotController]; ADR 0005 sections 1, 5) ---

	/** Path acceptance radius at a point / roam goal (cm, agent radius not added). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1", Units = "cm"))
	float PointAcceptanceRadiusCm = 60.f;

	/** Path acceptance radius at an item (cm): close enough that the capsule overlaps the 37.5 cm pickup trigger. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1", Units = "cm"))
	float ItemAcceptanceRadiusCm = 30.f;

	/** Below this horizontal speed with an active path the stuck clock runs (cm/s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "cm/s"))
	float StuckSpeedCmS = 50.f;

	/** D18: while tracking a segment, the 2-D distance to its target must shrink by this much within StuckSeconds (cm). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "cm"))
	float StuckProgressCm = 100.f;

	/** D27: a progress window may be paused under the combat overlay for at most this long in total (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float StuckProgressMaxPauseSeconds = 4.f;

	/** D30: after a fight_stall strike the goal decision ignores the visible enemy for this long (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float FightStallCooldownSeconds = 4.f;

	/**
	 * Task 06b: a Fight goal whose bot stays within FightStandoffRadiusCm (2-D) for FightStandoffSeconds yields (the
	 * D30 suppression for FightStallCooldownSeconds, then the decision routes to an item or a roam point; aim and fire
	 * are untouched). Covers what D30's speed rule does not: dodge hops at a ledge corner, a chase whose last seen point
	 * is reached or unreachable, a target on another level (s).
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float FightStandoffSeconds = 3.f;

	/** Task M7 H13: a Fight goal whose closing in the void blocked this long (accumulated) yields like a stand-off (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "0", Units = "s"))
	float FightEdgeYieldSeconds = 2.f;

	/** Task 06b: radius of the fight stand-off circle (cm, 2-D; the soak's dwell radius). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "cm"))
	float FightStandoffRadiusCm = 150.f;

	/**
	 * Task M7 H15: sampling period of the fight dwell rule (VeloxBotRules::UpdateFightDwell): a Fight goal whose positions
	 * PeriodSeconds apart stay within FightStandoffRadiusCm of one anchor for FightStandoffSeconds yields like a stand-off
	 * (reason=dwell). 1 s = the soak's dwell sampling; 0 turns the rule off (s).
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float FightDwellPeriodSeconds = 1.f;

	/**
	 * Task M7 H15: the fight dwell window survives a Fight goal interrupted this long (a re-evaluation flicking to an item
	 * and back reset it: Sacrarium 7 s fight dwell); a longer non-Fight stretch clears it (s).
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float FightDwellGraceSeconds = 1.f;

	/**
	 * Task M7 H15: a bot following its path that stands within the item's acceptance (2-D) but on another level (the path
	 * ended under an item on a ledge or pedestal) this long drops the item for UnreachableRetrySeconds (s; 0 = off).
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float UnderGoalSeconds = 1.f;

	/** Task 06b: an airborne stretch longer than the bot's own hop (2 x JumpVelocity / |g|) plus this is a flight (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float StuckProgressFlightSlackSeconds = 0.15f;

	/** Stuck strikes before a non-forced goal is dropped for a roam point. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1"))
	int32 StuckStrikesToRoam = 3;

	/** Minimum time between path requests of one bot (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float RepathIntervalSeconds = 0.5f;

	/** Fight goal, enemy remembered but not visible (D12): the chase path is re-queried when the last seen location moved farther than this (cm). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "cm"))
	float ChaseMoveToleranceCm = 300.f;

	/** A goal target that moved farther than this re-queries the path (cm). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "cm"))
	float GoalMoveToleranceCm = 50.f;

	/** CPM key snap (section 1): with air control on, a minor key component below this fraction is dropped. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", ClampMax = "1"))
	float CpmKeySnap = 0.3f;

	/** How long a failed JumpFast / rocket jump keeps the downgraded filter (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float LinkFallbackSeconds = 10.f;

	/** Failed path requests in a row before a non-forced goal is dropped. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1"))
	int32 FailedMovesToRoam = 3;

	/** Random candidates (from the bot's stream) projected onto the nav mesh per roam pick. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1"))
	int32 RoamPointAttempts = 8;

	/** Vertical half extent of the roam candidate projection (cm): finds floors and platform tops below / above. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1", Units = "cm"))
	float RoamProjectHalfHeightCm = 500.f;

	/** Off-mesh recovery (task 04b): how often an on-ground bot checks that its feet project onto the nav mesh (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0.05", Units = "s"))
	float OffMeshCheckSeconds = 0.5f;

	/** Off the mesh this long on the ground starts a recovery (s); short excursions (a ledge edge, a box top) are ignored. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float OffMeshGraceSeconds = 1.f;

	/** Candidate rings of the recovery search, nearest first (cm); the last one is also the radius of the nearest-point fallback. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot")
	TArray<float> OffMeshRecoverRingsCm = { 150.f, 300.f, 600.f, 1000.f };

	/** Candidate directions per ring. D45 test hook: 0 runs no ring probe (the swept fallback and its tangent probes still run). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0"))
	int32 OffMeshRecoverDirections = 12;

	/** Radius of the wall test toward a candidate (cm): thinner than the capsule, the walk slides along walls. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1", Units = "cm"))
	float OffMeshSweepRadiusCm = 15.f;

	/** Horizontal half extent of one candidate's projection onto the mesh (cm). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1", Units = "cm"))
	float OffMeshCandidateExtentCm = 100.f;

	/** Vertical half extent of the candidate projections (cm). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1", Units = "cm"))
	float OffMeshRecoverHalfHeightCm = 800.f;

	/** A direct recovery move that has not brought the feet back onto the mesh within this is replaced by a new attempt (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0.5", Units = "s"))
	float OffMeshRecoverTimeoutSeconds = 4.f;

	/** Recovery attempts before the bot gives up and suicides through environment damage (D17, B28 respawn; no teleport). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1"))
	int32 OffMeshMaxAttempts = 6;

	/**
	 * ADR 0008 3.6.3 airborne recovery (only on maps with a hazard volume): horizontal half extent of the projection of
	 * the predicted landing onto the nav mesh, at or below the bot's height (cm).
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "1", Units = "cm"))
	float AirRecoveryExtentCm = 600.f;
	/** 3.6.3: depth of the recovery window below the feet (cm); nav points deeper are not looked for. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "1", Units = "cm"))
	float AirRecoveryDownCm = 800.f;
	/** 3.6.3: "at or below" tolerates nav points this far above the feet (cm; the mesh floats above the floor by up to a cell height). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "0", Units = "cm"))
	float AirRecoveryUpToleranceCm = 20.f;
	/** 3.6.3: a predicted landing within this of the recovery nav point is on course: no steering (cm, 2-D). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "0", Units = "cm"))
	float AirRecoveryOnCourseCm = 20.f;
	/** 3.6.4 hazard escape: radius of the search for the nearest nav point outside the hazard (cm). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "1", Units = "cm"))
	float HazardEscapeExtentCm = 400.f;
	/** 3.6.4: candidate directions per search ring (four rings at a quarter of HazardEscapeExtentCm each). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "1"))
	int32 HazardEscapeDirections = 16;
	/** 3.6.4: the escape window above the feet (cm): the rim of a lava channel is higher than its bed (a jump reaches it). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "0", Units = "cm"))
	float HazardEscapeUpCm = 150.f;
	/** 3.6.4: the escape window below the feet (cm). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "0", Units = "cm"))
	float HazardEscapeDownCm = 100.f;
	/** 3.6.4: a candidate nav point is in a hazard when the point this high above it is (cm; the lava volume is thin). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "0", Units = "cm"))
	float HazardProbeUpCm = 20.f;
	/** 3.6.4: the escape target is searched again this often while escaping (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "0.02", Units = "s"))
	float HazardEscapeSearchSeconds = 0.25f;
	/** 3.6.4: the escape gives the input back after this long, out or not (s; the lava kills well before). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot|Hazard", Meta = (ClampMin = "0.1", Units = "s"))
	float HazardEscapeMaxSeconds = 3.f;

	/** velox.bots.Debug line interval (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0.1", Units = "s"))
	float DebugIntervalSeconds = 1.f;

protected:
	//~AActor interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~End of AActor interface

	//~AController interface
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	/** Task M7 H10: a unique PlayerId (UVeloxBotSubsystem::AssignUniquePlayerId). */
	virtual void InitPlayerState() override;
	//~End of AController interface

	// --- Phases (ADR 0005 section 2) ---

	/** Perception (task 05): enemies, damage hearing. Stub. */
	virtual void Perceive(float DeltaSeconds);
	/** Goal choice when no forced goal is set (task 06: item routing). Stub: roam when there is no goal. */
	virtual void UpdateGoal(float DeltaSeconds);
	/** Combat overlay on State.Pending after the path step (task 05: strafing, dodging, range keeping). Stub. */
	virtual void UpdateCombat(float DeltaSeconds);
	/** View, weapon select, fire into State.Pending (task 05: aim model, weapon choice, fire rule). Base: idle aim along the path, maneuver view requests. */
	virtual void Act(float DeltaSeconds);

	/** Section 1: rotation -> AddMovementInput -> SetLocalMoveInput -> SetJumpInput -> crouch -> ability tags -> ProcessAbilityInput. */
	void ApplyInput(const FVeloxBotInput& Input, float DeltaSeconds);

	/** Sets the goal (not forced), clearing arrival and the path. */
	void SetGoal(const FVeloxBotGoal& NewGoal);
	/** Picks a random reachable point within UVeloxBotSettings::RoamRadiusCm; false if none. */
	bool StartRoam();

	FVeloxBotState State;
	/** Task 05 (VeloxBotCombatGlue): reset per life inside Perceive. */
	FVeloxBotCombatState Combat;

private:
	void Decide(float DeltaSeconds);
	void Navigate(float DeltaSeconds);
	/** Requests a path to the goal when needed (no path, target moved, filter changed). */
	void EnsureMove(double Now);
	/** Off-mesh check and recovery move (task 04b); true while recovery owns the move (EnsureMove then requests no path). */
	bool UpdateOffMeshRecovery(double Now);
	/**
	 * ADR 0008 3.6.4 (ahead of UpdateOffMeshRecovery): feet in a lava volume start the escape, which owns the move until
	 * the bot stands outside the lava; searches the escape target (nearest nav point outside every hazard volume within
	 * HazardEscapeExtentCm). True while escaping (EnsureMove then requests no path). Inert off hazard maps.
	 */
	bool UpdateHazardEscape(double Now);
	/** Escape target search; false when neither a nav point nor the volume's side was found. */
	bool FindHazardEscapeTarget(const FVector& Feet, FVector& OutTarget) const;
	/**
	 * Task M7 H7: own hop / dodge jump with its predicted landing in lava: steer to the nearest safe nav point (ApplyHazardBehaviours).
	 * Task M7 H13: true when the predicted landing has no ground at all (void): the caller hands the flight to the airborne recovery.
	 */
	bool UpdateHazardLanding(double Now, const FVector& Feet, const FVector& Velocity, float GravityZ, float JumpVelocity, float WalkableFloorZ);
	/**
	 * Task M7 H13 ground edge guard (grounded, off a link, not near the start of the link ahead): Feet + Velocity2D x
	 * EdgeGuardLookaheadSeconds + Wish x EdgeGuardWishCm unsafe (VeloxBotCombatGlue::IsUnsafeGround) -> the wish's outward
	 * part is replaced by the direction to the nearest nav point, no hop.
	 */
	void UpdateEdgeGuard(double Now, const FVector& Feet, const FVector& Velocity);
	/** H13: no guard tick within the last 0.5 s (a new episode). */
	bool LastEdgeGuardTimeGap(double Now) const;
	/**
	 * ADR 0008 3.6.3/4 on State.Pending after Act (inert off hazard maps): the hazard escape (face the target, forward,
	 * jump), pad discipline (no move input in a JUMP_PAD flight; aim live), the airborne recovery (foreign flight, not on a
	 * link: full air control from the predicted landing toward the nearest nav point at or below the bot).
	 */
	void ApplyHazardBehaviours(double Now);
	void StopMove();
	/**
	 * One stuck strike (section 5, D18, D28, D30): the VeloxBotStuck line with rule=, both clocks restart, the path is
	 * dropped and re-queried, StuckStrikesToRoam strikes roam (non-forced goals). Target / Dist describe what the bot was
	 * heading for (segment target, link end, enemy); WindowDist is the progress window's start distance or -1.
	 */
	void AddStuckStrike(const TCHAR* Rule, const FVector2D& Target, float Dist, float WindowDist);
	/** D30: the Fight goal's enemy is visible now (false for any other goal). */
	bool IsGoalEnemyVisible() const;
	/** Where the current goal wants the bot, false when it wants no path. */
	bool GetGoalTarget(FVector& OutTarget, float& OutAcceptance) const;
	/** Task 06b: vertical arrival tolerance (cm): the nav agent height (fallback: the capsule height). */
	float GetArrivalMaxDzCm() const;
	/** Task 06b: 3-D arrival at the current goal target (true when the goal has no point, e.g. a visible fight). */
	bool IsAtGoalTarget() const;
	void ResetLifeState();
	void ReleaseAllInput();
	void LogDebugLine(double Now);

	void OnPickupTaken(AVeloxPickup* Pickup, APlayerState* TakenBy);

	AVeloxCharacter* GetBotCharacter() const;
	double Now() const;

	UPROPERTY(Transient)
	TObjectPtr<const UVeloxBotDifficulty> Difficulty;

	FVeloxBotDifficultyParams Params;
	FRandomStream Stream;
	int32 BotIndex = INDEX_NONE;
	int32 LifeCount = 0;
	bool bFrozen = false;
	/** Path queries run for this bot in the current tick (reset at the start of Tick). */
	int32 TickPathQueries = 0;
	FDelegateHandle PickupTakenHandle;
};
