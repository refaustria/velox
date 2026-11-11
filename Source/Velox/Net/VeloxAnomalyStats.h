// Velox - per-player anomaly counters (ADR 0007 section 5.4, task m6-online/09). World-free.
//
// Every bounded cheat surface ADR 0003 accepted (B25 clamp window, B32 burst after idle, B35 withheld RPC / orphans,
// B37 (ii) switch tolerance, B42 Late retargets, origin snaps, time-stamp clamps, fire rejections) and task 07's refused
// RPCs and time-discrepancy resolutions are logged per event. These counters turn them into a per-player rate: an honest
// player at the ping limit hits a surface once per lost packet, a modified client every time. Log only in M6: the
// thresholds default to 0 (disabled) and nothing kicks; UVeloxAnomalyLog prints the lines (60 s cadence and at logout).
//
// Unit rows: Velox.Net.AnomalyFormat, Velox.Net.AnomalyRates (Tests/VeloxNetTests.cpp).

#pragma once

#include "CoreMinimal.h"
#include "Containers/ContainerAllocationPolicies.h"

#include "VeloxAnomalyStats.generated.h"

/** One counted surface. Shot and Switch are the denominators (accepted remote shots / accepted remote switch presses). */
enum class EVeloxAnomaly : uint8
{
	/**
	 * A remote shot accepted by the server's validation (hitscan or projectile target data). A hitscan shot that
	 * ResolveServerShot rejects for a server fault (reason=no_lag_compensation) counts as a Reject only (review 0007-09
	 * F3). Not counted at all: the lag-compensation subsystem's own VeloxShotRejected reason=nan_origin (reached only by a
	 * shooter without a pawn; the subsystem stays untouched and reports no rejection to the ability), so such a shot
	 * still counts as accepted.
	 */
	Shot,
	/** A remote weapon-switch press accepted by the server (a new switch, or a retarget of the running one: pending / late). */
	Switch,
	/** VeloxShotRejected reason=<tag> (the reason is passed along). */
	Reject,
	/** VeloxShotOriginSnap (B15/B36): the client origin was replaced by the validated one. */
	OriginSnap,
	/** VeloxShotTimeClamp (B25): a projectile fire stamp outside LastMoveStamp +- MaxRewindSeconds. */
	TimeClamp,
	/** VeloxWeaponSwitch retarget mode=late (B42): the press arrived after the server's drop end, within the tolerance. */
	LateRetarget,
	/** VeloxWeaponSwitch retarget mode=fresh (B42): the press arrived outside the tolerance and ran as a new switch. */
	FreshRetarget,
	/** VeloxMoveShotOrphan (B35/B38): a move-carried shot whose fire RPC never came. */
	MoveShotOrphan,
	/** VeloxRpcRefused (ADR 0007 section 5.1). */
	RpcRefused,
	/** VeloxTimeDiscrepancy (ADR 0007 section 5.3): a movement time-discrepancy resolution began. */
	Discrepancy,
};

/**
 * Server-side counters of one player (cumulative since login). Not replicated, not reset by a match restart. No heap
 * allocation for up to MaxInlineRejectReasons distinct reject reasons (there are seven today).
 */
struct VELOX_API FVeloxPlayerAnomalyStats
{
	static constexpr int32 MaxInlineRejectReasons = 8;

	int32 Shots = 0;
	int32 Switches = 0;
	/** Reject counts by reason tag, in order of first occurrence. */
	TArray<TPair<FName, int32>, TInlineAllocator<MaxInlineRejectReasons>> Rejects;
	int32 OriginSnaps = 0;
	int32 TimeClamps = 0;
	int32 LateRetargets = 0;
	int32 FreshRetargets = 0;
	int32 MoveShotOrphans = 0;
	int32 RpcRefused = 0;
	int32 Discrepancies = 0;
	/** The engine's smoothed round-trip ping of the player (ms), sampled when a line is printed. Not a counter. */
	float RttMs = 0.f;

	/** Adds Count to the surface's counter (Reason: the reject tag, ignored otherwise; NAME_None counts as "unknown"). */
	void Note(EVeloxAnomaly Kind, int32 Count = 1, FName Reason = NAME_None);

	/** Sum over all reasons. */
	int32 GetRejects() const;
	/** Count of one reason (0 if never seen). */
	int32 GetRejects(FName Reason) const;
	/** Accepted plus rejected shots: the reject_share denominator (review 0007-09 F2). */
	int32 GetShotAttempts() const;

	/** Any counter non-zero (RttMs is not a counter). */
	bool HasAnyCounter() const;
	/** All counters equal (RttMs ignored): nothing happened between two samples. */
	bool SameCounters(const FVeloxPlayerAnomalyStats& Other) const;
};

/**
 * Per-player thresholds for the anomaly log (ADR 0007 section 5.4). Every value 0 = disabled, the M6 default: no line
 * ever carries exceeds=. M6 never kicks on them (velox.net.AnomalyKickThresholds stays log only); they exist so the M7
 * decision is config ([/Script/Velox.VeloxAnomalyLog] Thresholds=(...) in DefaultGame.ini).
 */
USTRUCT()
struct VELOX_API FVeloxAnomalyThresholds
{
	GENERATED_BODY()

	/** late_share above this (0..1) is exceeded, once Switches >= MinSwitchesForLateShare (ADR 0007 M7 trigger: 0.20 / 20). */
	UPROPERTY(EditAnywhere, Config, Category = "Velox|Anomaly")
	float MaxLateShare = 0.f;

	UPROPERTY(EditAnywhere, Config, Category = "Velox|Anomaly")
	int32 MinSwitchesForLateShare = 20;

	/** reject_share above this (0..1, rejects per attempt) is exceeded, once attempts (Shots + rejects) >= MinShotsForRejectShare. */
	UPROPERTY(EditAnywhere, Config, Category = "Velox|Anomaly")
	float MaxRejectShare = 0.f;

	UPROPERTY(EditAnywhere, Config, Category = "Velox|Anomaly")
	int32 MinShotsForRejectShare = 20;

	/** Cumulative counts above these are exceeded. */
	UPROPERTY(EditAnywhere, Config, Category = "Velox|Anomaly")
	int32 MaxOriginSnaps = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Velox|Anomaly")
	int32 MaxTimeClamps = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Velox|Anomaly")
	int32 MaxMoveShotOrphans = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Velox|Anomaly")
	int32 MaxRpcRefused = 0;

	UPROPERTY(EditAnywhere, Config, Category = "Velox|Anomaly")
	int32 MaxDiscrepancies = 0;
};

namespace VeloxAnomaly
{
	struct FRates
	{
		/** LateRetargets / max(Switches, 1). */
		float LateShare = 0.f;
		/** Rejects / max(Shots + Rejects, 1): rejects per attempt, in [0,1] (review 0007-09 F2). */
		float RejectShare = 0.f;
	};

	VELOX_API FRates Rates(const FVeloxPlayerAnomalyStats& Stats);

	/**
	 * Names of the exceeded thresholds ("late_share", "reject_share", "snaps", "clamps", "orphans", "rpc_refused",
	 * "discrepancies"); empty when every threshold is 0. A value must be strictly above its threshold.
	 */
	VELOX_API TArray<FName> Exceeds(const FVeloxPlayerAnomalyStats& Stats, const FVeloxAnomalyThresholds& Thresholds);

	/**
	 * The log line (cumulative values, the delta since Previous in parentheses; shares and rtt from Stats):
	 *   VeloxAnomaly at=<At> pid=<Id> shots=12(+4) switches=3(+1) rejects{rate:2(+2),type:1(+0)} snaps=0(+0)
	 *   clamps=0(+0) late_retargets=0(+0) fresh_retargets=0(+0) orphans=0(+0) rpc_refused=0(+0) discrepancies=0(+0)
	 *   late_share=0.00 reject_share=0.25 rtt_ms=210 [exceeds=a,b] player=<name, may contain spaces, to end of line>
	 * The player name is last so a name with spaces cannot shift a key (evaluators match by pid=, ADR 0007 F3).
	 */
	VELOX_API FString FormatLine(const FVeloxPlayerAnomalyStats& Stats, const FString& Player, int32 Id, const FVeloxPlayerAnomalyStats& Previous,
		const TCHAR* At, const TArray<FName>& Exceeded = TArray<FName>());
}
