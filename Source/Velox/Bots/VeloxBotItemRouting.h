// Velox - bot item routing (ADR 0005 sections 2, 4, 7, 13; task M4-06): Q3-style value / travel time with respawn timing
// through UVeloxItemRegistrySubsystem, goal re-evaluation on a cadence and on events, hysteresis through
// VeloxBotDecision::ChooseGoal, Duel camping of the next major item, nav filter changes logged and re-evaluated.
//
// Split like the rest of Bots/: the world-free rules (namespace VeloxBotItemRouting, unit-tested in
// Velox.Bots.ItemRouting) and the world glue UVeloxBotItemRouter (a server world subsystem) that
// AVeloxBotController::UpdateGoal calls every tick. One evaluation:
//   1. bot facts from the PlayerState (UVeloxHealthSet, UVeloxAmmoSet, inventory OwnsIndex) and item facts from each
//      registered pickup's definition and its IsAvailable / GetTimeUntilRespawn,
//   2. pre-score every pickup with the straight-line distance / EstimatedTravelSpeed,
//   3. pick at most PathQueryCandidates pickups: the current goal item, in Duel the next major item to come up, then the
//      best pre-scores,
//   4. FindPathSync for each with the bot's current nav filter: TravelSeconds = path cost / EstimatedTravelSpeed (cost =
//      length plus the link entering costs), partial or failed path = unreachable, score 0,
//   5. VeloxBotDecision::ChooseGoal (fight / item with hysteresis / Duel camp / roam).
// Cost rules (section 13, review D10): an evaluation is a small state machine that runs ONE FindPathSync per server frame
// (K candidates = K frames, scoring and choice in the frame of the last query); only one bot evaluates at a time, so no
// two bots' evaluations share a frame. A bot that is due while another evaluates waits for the slot.

#pragma once

#include "Bots/VeloxBotDecision.h"
#include "Bots/VeloxItemValue.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectKey.h"

#include "VeloxBotItemRouting.generated.h"

class AVeloxBotController;
class AVeloxPickup;
class AVeloxPlayerState;
class APlayerState;
class UNavigationQueryFilter;
class UVeloxItemDefinition;
struct FVeloxBotDifficultyParams;
struct FVeloxBotGoal;

// ------------------------------------------------------------------------------------------------ world-free rules

/** Ammo use of one owned weapon (ADR 0005 D13: WeaponStrength counts weapons with at least one shot). */
struct FVeloxRoutingWeaponAmmo
{
	/** Invalid: the weapon uses no ammo (always usable). */
	FGameplayTag AmmoTag;
	/** 0: the weapon uses no ammo (always usable). */
	int32 AmmoPerShot = 0;
};

/** What the scoring needs from the bot (filled from the live PlayerState attributes and inventory). */
struct FVeloxRoutingBotFacts
{
	int32 Health = 0;
	int32 MaxHealth = 0;
	int32 HealthHardCap = 0;
	int32 Armor = 0;
	int32 MaxArmor = 0;
	/** Ammo tag -> (have, cap). */
	TMap<FGameplayTag, FIntPoint> Ammo;
	/** WeaponTag of every owned weapon. */
	TArray<FGameplayTag> OwnedWeaponTags;
	/** AmmoTag of every owned weapon. */
	TArray<FGameplayTag> OwnedAmmoTags;
	/** WeaponTag of every owned weapon -> its ammo tag and cost per shot (D13 WeaponStrength). */
	TMap<FGameplayTag, FVeloxRoutingWeaponAmmo> OwnedWeaponAmmo;
	/** Duel denial floor of a major item's need (0 = off, the plain section 7 need). */
	float MajorItemMinNeed = 0.f;
	/**
	 * ADR 0005 D22: WeaponStrength(this, preset WeaponPreferences), filled once per evaluation right after GatherBotFacts;
	 * MakeScoringInput copies it to FVeloxItemScoringInput::OwnedWeaponStrength. Default 1: neutral.
	 */
	float WeaponStrength = 1.f;
	/** D22: UVeloxBotSettings::UnownedWeaponNeedFloor (filled by the glue); default 1 = the pre-D22 rule. */
	float UnownedWeaponNeedFloor = 1.f;
};

/** What the scoring needs from one pickup (definition + registry state). */
struct FVeloxRoutingItemFacts
{
	int32 CandidateId = INDEX_NONE;
	EVeloxItemType Type = EVeloxItemType::Health;
	int32 Quantity = 0;
	float BotBaseValue = 0.f;
	bool bMajorItem = false;
	EVeloxHealthCap HealthCap = EVeloxHealthCap::MaxHealth;
	/** Ammo items: their ammo; weapon items: the weapon's ammo. */
	FGameplayTag AmmoTag;
	/** Weapon items: the weapon granted. */
	FGameplayTag WeaponTag;
	bool bAvailable = true;
	float TimeUntilRespawn = 0.f;
	FVector Location = FVector::ZeroVector;
};

/** One pickup after the straight-line pre-score (input of the path-query selection). */
struct FVeloxRoutingPreScore
{
	int32 CandidateId = INDEX_NONE;
	float PreScore = 0.f;
	bool bMajorItem = false;
	bool bAvailable = true;
	float TimeUntilRespawn = 0.f;
	float StraightSeconds = 0.f;
};

/** Result of one path query as the routing sees it. */
struct FVeloxRoutingPathResult
{
	/** FindPathSync returned a path at all. */
	bool bFound = false;
	bool bPartial = false;
	/** Path cost (length x area cost + link entering costs) and geometric length, cm. */
	double Cost = 0.0;
	double Length = 0.0;
	/** Last path point (where a partial path stops; D24 diagnostics). */
	FVector End = FVector::ZeroVector;
};

namespace VeloxBotItemRouting
{
	/**
	 * FVeloxItemScoringInput for one pickup as seen by one bot with the given travel time. With Bot.MajorItemMinNeed > 0
	 * (Duel) a major item's BotBaseValue is scaled so that Value = BotBaseValue x max(Need, MajorItemMinNeed) while Need > 0.
	 */
	VELOX_API FVeloxItemScoringInput MakeScoringInput(const FVeloxRoutingItemFacts& Item, const FVeloxRoutingBotFacts& Bot, float TravelSeconds);

	/**
	 * ADR 0005 D13: how good the bot's best usable weapon is, 0..1. Usable = owned (Bot.OwnedWeaponAmmo) and either no
	 * ammo (invalid tag, AmmoPerShot 0, ammo tag not tracked in Bot.Ammo) or Bot.Ammo[tag].X >= AmmoPerShot. Result: the
	 * largest preference Weight among usable weapons / the largest Weight in Preferences (Medium: MG-only 20/90, SG 60/90,
	 * RL 1). Nothing usable (or no usable weapon in the table) -> 0; an empty table or all weights 0 -> 1 (no data:
	 * neutral, the pre-D13 behaviour).
	 */
	VELOX_API float WeaponStrength(const FVeloxRoutingBotFacts& Bot, TConstArrayView<FVeloxBotWeaponPreference> Preferences);

	/**
	 * ADR 0005 D22: the per-evaluation derived bot facts, right after GatherBotFacts (used by the straight-line pre-score and
	 * the final score alike): Bot.WeaponStrength = WeaponStrength(Bot, Preferences), Bot.UnownedWeaponNeedFloor = Floor
	 * (UVeloxBotSettings::UnownedWeaponNeedFloor in the glue).
	 */
	VELOX_API void FillDerivedBotFacts(FVeloxRoutingBotFacts& Bot, TConstArrayView<FVeloxBotWeaponPreference> Preferences, float UnownedWeaponNeedFloor);

	/**
	 * ADR 0005 D24: a partial path marks the item Unreachable (UnreachableRetrySeconds) only when the evaluation started
	 * from the bot's own grounded position. From a start projected below an airborne pawn (pad flight, bunny hop) a partial
	 * path proves nothing: the item scores 0 for this evaluation and is queried again at the next one. A failed query
	 * (start / end projection) never marks it.
	 */
	VELOX_API bool ShouldMarkUnreachable(const FVeloxRoutingPathResult& Path, bool bAirborneStart);

	/** Distance / Speed (0 speed: the distance in metres per second fallback is not used; returns a large value). */
	VELOX_API float TravelSeconds(double DistanceCm, float SpeedCmS);

	/** Travel time from a path: max(Cost, Length) / Speed (Recast cost = length with area cost 1 + entering costs). */
	VELOX_API float PathTravelSeconds(const FVeloxRoutingPathResult& Path, float SpeedCmS);

	/**
	 * The pickups to query paths for, at most MaxQueries ids: KeepId first (the current goal or camp item, if present in
	 * PreScored), then in Duel the unavailable major item with the smallest TimeUntilRespawn - StraightSeconds (camp
	 * candidate), then the best PreScore > 0 (ties by CandidateId ascending).
	 */
	VELOX_API void SelectQueryCandidates(TConstArrayView<FVeloxRoutingPreScore> PreScored, int32 MaxQueries, int32 KeepId, bool bDuel, TArray<int32>& OutIds);

	/**
	 * Score of the goal the bot is already committed to: VeloxItemValue::Score with the timing gate widened by the travel
	 * time the bot had when it chose the item (Wait <= MaxWait x Skill + CommitTravelSeconds). Without it a bot that moves
	 * faster than EstimatedTravelSpeed toward an unavailable item sees Wait = TimeUntilRespawn - Travel grow past the gate
	 * on the way and turns back (goal oscillation). CommitTravelSeconds 0 = Score.
	 */
	VELOX_API float CommittedScore(const FVeloxItemScoringInput& In, const FVeloxItemScoringParams& Params, float CommitTravelSeconds);

	/**
	 * Goal candidate after the path query: unreachable (no path or partial) scores 0; else CommittedScore() at the path
	 * travel time (CommitTravelSeconds > 0 only for the current goal item).
	 */
	VELOX_API FVeloxGoalItemCandidate MakeGoalCandidate(const FVeloxRoutingItemFacts& Item, const FVeloxRoutingBotFacts& Bot, const FVeloxRoutingPathResult& Path,
		float SpeedCmS, const FVeloxItemScoringParams& Params, float CommitTravelSeconds = 0.f);

	/**
	 * Duel "nothing else worth going for" (task 06 refinement of section 7, see the report): candidate scores below
	 * MinScore count as 0, so the camping branch of ChooseGoal can trigger on a map full of minor items.
	 */
	VELOX_API void ApplyMinScore(TArray<FVeloxGoalItemCandidate>& Candidates, float MinScore);

	/** Evaluation due: an event (take, respawn, filter change) is pending, the goal was just lost, or the cadence is up. */
	VELOX_API bool IsEvaluationDue(double Now, double NextEvaluationTime, bool bGoalLost, bool bEventPending);

	/** Camp spot: Offset cm from the item toward the bot (horizontal); the item itself when the bot stands on it. */
	VELOX_API FVector CampPoint(const FVector& ItemLocation, const FVector& BotLocation, float OffsetCm);

	/**
	 * Refusal guard: the bot has stood at an available goal item for at least RefuseSeconds without taking it (a pickup
	 * rule the need formula does not model, or a trigger miss), so the item is put on cooldown for this bot.
	 */
	VELOX_API bool IsRefusedAtArrival(bool bArrived, bool bItemAvailable, double ArrivedSince, double Now, float RefuseSeconds);

	/** "Health.Mega" from Velox.Item.Health.Mega (log label). */
	VELOX_API FString ItemLabel(const FGameplayTag& ItemTag);
}

// ------------------------------------------------------------------------------------------------ world glue

/** What UpdateBot asks the controller to do. */
enum class EVeloxRouteUpdate : uint8
{
	/** Keep the current goal (nothing evaluated, or the evaluation confirmed it). */
	Unchanged,
	/** SetGoal(OutGoal). */
	NewGoal,
	/** No item and no camp: the controller picks a roam point with its own stream (StartRoam). */
	Roam,
};

/** The last evaluation of one bot (debug, functional tests). */
struct FVeloxBotRouteRecord
{
	double Time = -1.0;
	TSubclassOf<UNavigationQueryFilter> Filter;
	TArray<FVeloxGoalItemCandidate> Candidates;
	/** CandidateId -> pickup of the candidates above. */
	TArray<TWeakObjectPtr<AVeloxPickup>> CandidatePickups;
	FVeloxBotGoalChoice Choice;
	/** D24: the start was projected below an airborne pawn (`start=air`). */
	bool bAirborneStart = false;
	/** D20 diagnostics: the evaluation's WeaponStrength (`wstr=` in the VeloxBot line) and FightItemBias x FightScore (`fight=`). */
	float WeaponStrength = 1.f;
	float BiasedFightScore = 0.f;
	int32 PathQueries = 0;
	/** Server frames the evaluation took (one path query per frame). */
	int32 Frames = 0;
	double Micros = 0.0;
};

UCLASS(Config = Game)
class VELOX_API UVeloxBotItemRouter : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVeloxBotItemRouter* Get(const UWorld* World);

	/**
	 * ADR 0005 D41: the one query extent of every grounded "am I on the nav mesh" projection - the router's ground start
	 * projection, AVeloxBotController's off-mesh test and the D35 segment check:
	 * (AirborneProjectRadiusCm, AirborneProjectRadiusCm, UVeloxBotSettings::OnMeshHalfHeightCm), from the config defaults.
	 */
	static FVector GetOnMeshExtent();

	/**
	 * AVeloxBotController::UpdateGoal, every server tick of a bot without a forced goal: watches the nav filter (logs
	 * `VeloxBotFilter`, re-evaluates on a change), and when an evaluation is due and this frame's slot is free, evaluates
	 * (steps 1-5 above). OutGoal is valid for NewGoal.
	 */
	EVeloxRouteUpdate UpdateBot(AVeloxBotController& Bot, const FVeloxBotDifficultyParams& Params, const FVeloxBotGoal& Current, bool bGoalArrived,
		FRandomStream& Stream, FVeloxBotGoal& OutGoal);

	/** Last evaluation of Bot, null before the first. */
	const FVeloxBotRouteRecord* FindLastEvaluation(const AVeloxBotController* Bot) const;

	/** Re-evaluate Bot on its next tick (tests, cheats). */
	void RequestEvaluation(const AVeloxBotController* Bot);

	/** Totals since the world started (budget line, task 05's VeloxBotBudget can read these). */
	int64 GetPathQueryCount() const { return TotalPathQueries; }
	int64 GetEvaluationCount() const { return TotalEvaluations; }

	// --- Tunables (DefaultGame.ini [/Script/Velox.VeloxBotItemRouter]) ---

	/** Duel camping stand-off from the camped item (ADR 0005 section 7 CampOffsetCm). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "cm"))
	float CampOffsetCm = 200.f;

	/** Duel: candidate scores below this count as "not worth going for" (camping instead). 0 = the plain section 7 rule. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0"))
	float DuelMinItemScore = 3.f;

	/** Duel (ADR 0005 D39): a major coming up within DuelCampMaxLeadSeconds beats any item scoring below this. 0 = camp only when no item is choosable. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0"))
	float DuelCampOverScore = 7.f;

	/** Duel (D39): camp candidates are majors with TimeUntilRespawn - TravelSeconds <= this. 0 = any lead. */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float DuelCampMaxLeadSeconds = 20.f;

	/**
	 * Duel: a major item whose need is above 0 but below this counts with this need (denial: the take also restarts the
	 * opponent's wait for it). 0 = the plain section 7 need. Task 06 refinement, see the report.
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", ClampMax = "1"))
	float DuelMajorItemMinNeed = 0.8f;

	/** Upper bound of the committed travel time that widens the current goal's timing gate (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float MaxCommitTravelSeconds = 8.f;

	/** Standing at an available goal item this long without the take puts it on cooldown for the bot (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0.1", Units = "s"))
	float ArrivedRefuseSeconds = 1.5f;

	/** How long a refused item is ignored by that bot (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float RefusedItemCooldownSeconds = 10.f;

	/** A pickup whose path came back partial / failed is not queried again for this long, unless the nav filter changes (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "0", Units = "s"))
	float UnreachableRetrySeconds = 5.f;

	/** Airborne evaluation: the path start is the mesh point found below the pawn within this depth (cm)... */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1", Units = "cm"))
	float AirborneProjectDownCm = 600.f;

	/**
	 * ...and the horizontal radius of both start projections (cm). On the ground the vertical half extent is
	 * UVeloxBotSettings::OnMeshHalfHeightCm (D41, GetOnMeshExtent).
	 */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1", Units = "cm"))
	float AirborneProjectRadiusCm = 100.f;

	/** `VeloxBotRouteStats` interval (s). */
	UPROPERTY(Config, EditDefaultsOnly, Category = "Velox|Bot", Meta = (ClampMin = "1", Units = "s"))
	float StatsIntervalSeconds = 5.f;

protected:
	//~UWorldSubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	//~End of UWorldSubsystem interface

private:
	/** One evaluation in progress: its query list, start point and filter, the paths found so far (review D10). */
	struct FPendingEvaluation
	{
		bool bActive = false;
		double StartTime = 0.0;
		FVector Start = FVector::ZeroVector;
		/** D24: Start was projected below an airborne pawn; partial paths then do not mark Unreachable. */
		bool bAirborneStart = false;
		/** D24 diagnostics: the pawn's nav agent location before the projection. */
		FVector AgentLocation = FVector::ZeroVector;
		TSubclassOf<UNavigationQueryFilter> Filter;
		TArray<TWeakObjectPtr<AVeloxPickup>> Queue;
		TArray<FVeloxRoutingPathResult> Results;
		int32 PathQueries = 0;
		double Micros = 0.0;
	};

	struct FBotRouteState
	{
		TWeakObjectPtr<AVeloxBotController> Bot;
		double NextEvaluationTime = 0.0;
		bool bEventPending = true;
		/** The bot had a goal on an earlier tick (its loss triggers an immediate evaluation). */
		bool bHadGoal = false;
		bool bFilterKnown = false;
		TSubclassOf<UNavigationQueryFilter> Filter;
		/** Arrival at an available goal item (refusal guard). */
		TWeakObjectPtr<AVeloxPickup> ArrivedItem;
		double ArrivedSince = -1.0;
		/** Pickup -> ignored until (refusal guard). */
		TMap<TWeakObjectPtr<AVeloxPickup>, double> Cooldowns;
		/** Pickup -> not queried until (partial path with the current filter; cleared on a filter change). */
		TMap<TWeakObjectPtr<AVeloxPickup>, double> Unreachable;
		/** Due frames spent waiting for the slot (stagger statistics). */
		int32 DeferredFrames = 0;
		FPendingEvaluation Pending;
		double NextOffMeshLogTime = 0.0;
		/** Combat target id that was visible on the last tick (INDEX_NONE: none); a change re-evaluates (Fight vs Item). */
		int32 LastVisibleTargetId = INDEX_NONE;
		/** The item last chosen as a goal and the path travel time then (CommittedScore). */
		TWeakObjectPtr<AVeloxPickup> CommitItem;
		float CommitTravelSeconds = 0.f;
		FVeloxBotRouteRecord Last;
	};

	FBotRouteState& FindOrAddState(AVeloxBotController& Bot);
	/** Steps 1-3; false when the bot cannot evaluate now (no pawn, no nav, airborne without mesh below), the slot is then not used. */
	bool BeginEvaluation(AVeloxBotController& Bot, FBotRouteState& S, const FVeloxBotDifficultyParams& Params, const FVeloxBotGoal& Current);
	/** Step 4 for one candidate (one FindPathSync); after the last one FinishEvaluation in the same frame. */
	EVeloxRouteUpdate StepEvaluation(AVeloxBotController& Bot, FBotRouteState& S, const FVeloxBotDifficultyParams& Params, const FVeloxBotGoal& Current,
		FRandomStream& Stream, FVeloxBotGoal& OutGoal);
	/** Step 5: scores with the live item state and the evaluation's paths, ChooseGoal, goal mapping, log. */
	void FinishEvaluation(AVeloxBotController& Bot, FBotRouteState& S, const FVeloxBotDifficultyParams& Params, const FVeloxBotGoal& Current,
		FRandomStream& Stream, EVeloxRouteUpdate& OutUpdate, FVeloxBotGoal& OutGoal);
	/** Drops Bot's running evaluation (filter change, lost slot) and frees the slot if Bot holds it. */
	void AbortEvaluation(AVeloxBotController& Bot, FBotRouteState& S);
	void OnPickupRespawned(AVeloxPickup* Pickup);
	void OnPickupTaken(AVeloxPickup* Pickup, APlayerState* TakenBy);
	void MaybeLogStats(double Now);

	TMap<TObjectKey<AVeloxBotController>, FBotRouteState> States;
	FDelegateHandle RespawnedHandle;
	FDelegateHandle TakenHandle;

	/** The bot whose evaluation runs (one at a time) and the last frame the router ran a step (one per frame). */
	TWeakObjectPtr<AVeloxBotController> ActiveBot;
	uint64 LastStepFrame = MAX_uint64 - 1;
	int64 TotalEvaluations = 0;
	int64 TotalPathQueries = 0;

	// Stats window.
	double StatsWindowStart = -1.0;
	int32 WindowEvaluations = 0;
	int32 WindowPathQueries = 0;
	double WindowMicrosSum = 0.0;
	double WindowMicrosMax = 0.0;
	int32 WindowMaxDeferredFrames = 0;
	double WindowQueryMicrosMax = 0.0;
	FString WindowSlowestQuery = TEXT("-");
};
