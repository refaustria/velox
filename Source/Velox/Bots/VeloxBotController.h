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
#include "Bots/VeloxBotCoreTypes.h"
#include "Bots/VeloxBotPathFollowingComponent.h"
#include "Bots/VeloxBotTypes.h"
#include "Math/RandomStream.h"
#include "Templates/SubclassOf.h"

#include "VeloxBotController.generated.h"

class AVeloxCharacter;
class AVeloxPickup;
class APlayerState;
class UNavigationQueryFilter;
class UVeloxBotDifficulty;

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
	/** Link fallbacks (section 4): a too-slow JumpFast forces _Walk, a rocket jump without impulse forces _Hop, until then. */
	double WalkFilterUntil = -1.0;
	double HopFilterUntil = -1.0;

	// --- Stuck detection (section 5) ---
	double SlowSince = -1.0;
	int32 StuckStrikes = 0;

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
	 * Name for a new bot: Requested if given and free, else the first free entry of Names; a taken name gets the lowest
	 * free numeric suffix starting at 2 ("Celer2"). Comparison ignores case. "Bot" when Names is empty.
	 */
	VELOX_API FString NextBotName(const TArray<FString>& Names, const TArray<FString>& Taken, const FString& Requested);

	/** ?Bots=N capped to the mode's slots: MaxActive - 1 (one human slot) unless bFill; MaxActive <= 0 = no cap. */
	VELOX_API int32 UrlBotCount(int32 Requested, int32 MaxActive, bool bFill);
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

	/** Called by the follower when a link traversal ends: VeloxBotLink log line, counters, re-query on failure. */
	void NotifyLinkFinished(const FVeloxBotLinkReport& Report);

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

	/** Stuck strikes before a non-forced goal is dropped for a roam point. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1"))
	int32 StuckStrikesToRoam = 3;

	/** Minimum time between path requests of one bot (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float RepathIntervalSeconds = 0.5f;

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

private:
	void Decide(float DeltaSeconds);
	void Navigate(float DeltaSeconds);
	/** Requests a path to the goal when needed (no path, target moved, filter changed). */
	void EnsureMove(double Now);
	void StopMove();
	/** Where the current goal wants the bot, false when it wants no path. */
	bool GetGoalTarget(FVector& OutTarget, float& OutAcceptance) const;
	void ResetLifeState();
	void ReleaseAllInput();
	void LogDebugLine(double Now);
	void UpdateRocketJumpState();

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
	FDelegateHandle PickupTakenHandle;
};
