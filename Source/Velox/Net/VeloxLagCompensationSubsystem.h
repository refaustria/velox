// Velox - server-side lag compensation (ADR 0003 section 6): per-pawn hitbox history, rewound hitscan traces.
//
// Time domains:
//   WorldTime  server UWorld::GetTimeSeconds(). The client names it through VeloxLagComp::EstimateServerTime
//              (GetServerWorldTimeSeconds(), exact since ADR 0003 B31) minus its
//              measured interpolation delay (FVeloxShotTiming::ViewServerTime).
//   PawnStamp  the pawn's CMC->GetServerLastTransformUpdateTimeStamp(): the value the engine replicates to viewers.
//              The client names the stamp of the drawn pose with UVeloxMovementComponent::GetRenderedServerStamp (the
//              received path nearest the drawn place, task 08b D1; not SmoothingClientTimeStamp). Accumulated client move time for
//              remote players, server world time for bots / the listen host. Monotonic per pawn, advances only on
//              movement. The client's hint (HintTarget, HintTargetStamp) is in this domain.
//
// The history and the pose selection are world-free (FVeloxHitboxHistory, VeloxLagComp::SelectPose, CollectHits)
// and unit-tested in Velox.Combat.LagCompRewind; the subsystem only feeds them from the world.

#pragma once

#include "Combat/VeloxHitMath.h"
#include "Subsystems/WorldSubsystem.h"

#include "VeloxLagCompensationSubsystem.generated.h"

class AActor;
class APlayerController;
class UVeloxCombatSettings;
class UVeloxHitboxComponent;

/** One recorded instant of one pawn. */
struct FVeloxHitboxSample
{
	double WorldTime = 0.0;
	float PawnStamp = 0.f;
	FVeloxHitboxPose Pose;
	/** The pawn jumped farther than HitboxTeleportDistanceCm since the previous point: never interpolate into this one. */
	bool bDiscontinuity = false;
};

/**
 * Fixed-capacity ring of samples, oldest overwritten. World times strictly increase, stamps never decrease (both
 * guaranteed by the caller: one sample per server frame at most). Pushing reuses the slot's pose storage, so sampling
 * one-shape poses never allocates after Init.
 */
class VELOX_API FVeloxHitboxHistory
{
public:
	void Init(int32 InCapacity);
	void Reset() { Head = 0; Count = 0; }

	int32 Num() const { return Count; }
	int32 Capacity() const { return Samples.Num(); }

	/** Index 0 = oldest, Num() - 1 = newest. */
	const FVeloxHitboxSample& Get(int32 IndexFromOldest) const;
	const FVeloxHitboxSample* Newest() const { return Count > 0 ? &Get(Count - 1) : nullptr; }

	/** Slot for the next sample (overwrites the oldest when full); the caller fills all fields. */
	FVeloxHitboxSample& Push();
	/** TeleportDistanceCm > 0 flags the sample as a discontinuity when it is farther than that from the previous one. */
	void Push(double WorldTime, float PawnStamp, const FVeloxHitboxPose& Pose, float TeleportDistanceCm = 0.f);

	/** True if the first shapes of Prev and Cur are farther apart than TeleportDistanceCm (<= 0: never). */
	static bool IsDiscontinuity(const FVeloxHitboxPose& Prev, const FVeloxHitboxPose& Cur, float TeleportDistanceCm);

	/**
	 * Pose at world time T, linearly interpolated between the bracketing samples. Live (optional) is the current
	 * state, treated as the newest point (its WorldTime >= the newest sample's). Older than the oldest sample: the
	 * oldest; newer than the newest point: the newest point. OutWorldTime is the time of the returned pose.
	 * False only if there is neither a sample nor Live.
	 */
	bool PoseAtWorldTime(double T, const FVeloxHitboxSample* Live, FVeloxHitboxPose& OutPose, double& OutWorldTime) const;

	/**
	 * Pose at pawn stamp Stamp (the client's rendered stamp), interpolated between the samples bracketing it by
	 * PawnStamp; a stamp at or beyond the newest point gives the newest point. Equal stamps (pawn at rest) resolve to
	 * the newest sample carrying them. False if Stamp is older than the oldest sample or the resulting world time is
	 * below MinWorldTime (the caller then falls back to world time). OutWorldTime is the interpolated world time.
	 */
	bool PoseAtStamp(float Stamp, double MinWorldTime, const FVeloxHitboxSample* Live, FVeloxHitboxPose& OutPose, double& OutWorldTime) const;

	/**
	 * Shape-wise lerp of centre, axis, radius and half height; differing shape counts, or bDiscontinuity (a teleport
	 * between A and B), take the nearer pose (Alpha < 0.5: A).
	 */
	static void LerpPose(const FVeloxHitboxPose& A, const FVeloxHitboxPose& B, double Alpha, FVeloxHitboxPose& OutPose, bool bDiscontinuity = false);

private:
	/** Number of points (samples plus Live) and point access in time order. */
	const FVeloxHitboxSample& Point(int32 Index, const FVeloxHitboxSample* Live) const { return Index < Count ? Get(Index) : *Live; }

	TArray<FVeloxHitboxSample> Samples;
	int32 Head = 0; // index of the oldest sample
	int32 Count = 0;
};

/** How a pose was chosen for one pawn of one shot. */
enum class EVeloxRewindMode : uint8
{
	Current,   // lag compensation off, or no history
	WorldTime, // bracketing samples of the clamped ViewServerTime
	Stamp,     // the client's hint stamp (hinted pawn only)
};

/** Client-side timing of one shot (ADR 0003 sections 5, 6), built by AVeloxPlayerController::BuildShotTiming. */
struct FVeloxShotTiming
{
	/** Server world time the client was looking at: EstimateServerTime() - interpolation delay. */
	double ViewServerTime = 0.0;
	/** Simulated proxy whose rendered capsule the client's ray hit first (not blocked by world), if any. */
	TWeakObjectPtr<AActor> HintTarget;
	/** That proxy's GetRenderedServerStamp() (PawnStamp domain). */
	float HintTargetStamp = 0.f;
	/**
	 * Measured interpolation delay (s): mean over moving proxies of (SmoothingServerTimeStamp - GetRenderedServerStamp())
	 * + update age, and whether it came from proxies (false: ResolveFallbackDelay, B30). Diagnostics.
	 */
	float InterpolationDelay = 0.f;
	bool bDelayFromProxies = false;
};

/** Server request for one hitscan shot. One entry in Dirs per pellet (unit vectors; the ability builds the pattern). */
struct FVeloxShotRequest
{
	TWeakObjectPtr<AActor> Shooter;
	FVector Origin = FVector::ZeroVector;
	TArray<FVector, TInlineAllocator<1>> Dirs;
	float RangeCm = 20480.f;
	/** Rail: each ray passes through up to MaxPenetrations pawns (nearest first) and stops at world. */
	bool bPenetratePawns = false;
	int32 MaxPenetrations = 1;
	double ViewServerTime = 0.0;
	TWeakObjectPtr<AActor> HintTarget;
	float HintTargetStamp = 0.f;
	/** Tolerances; ApplySettings copies them from the shooter's combat settings. */
	float MaxRewindSeconds = 0.25f;
	float MaxOriginErrorCm = 75.f;
	/** Snap Origin to the shooter's server eye (GetPawnViewLocation) when farther than MaxOriginErrorCm. */
	bool bValidateOrigin = true;
	uint8 ShotSeq = 0;

	void ApplySettings(const UVeloxCombatSettings* Settings);
	void ApplyTiming(const FVeloxShotTiming& Timing);
};

struct FVeloxShotHit
{
	AActor* Pawn = nullptr;
	/** Nearest entry distance over the pellets that hit this pawn. */
	double Dist = 0.0;
	FVector Location = FVector::ZeroVector;
	int32 PelletHits = 0;
};

struct FVeloxShotRay
{
	/** Where the ray stops: world impact, last pawn hit for a blocked rail / the nearest pawn hit otherwise, or range end. */
	FVector End = FVector::ZeroVector;
	FVector ImpactNormal = FVector::ZeroVector;
	bool bHitWorld = false;
	double WorldDist = 0.0;
	/** Indices into FVeloxShotResult::Hits of the pawns this ray hit (nearest first). */
	TArray<int32, TInlineAllocator<4>> HitIndices;
};

struct FVeloxShotResult
{
	/** Origin actually traced (the request's, or the snapped server eye). */
	FVector Origin = FVector::ZeroVector;
	bool bOriginSnapped = false;
	/** Why: "distance", "occluded" (eye to client origin blocked on the Weapon channel) or "nan"; null if not snapped. */
	const TCHAR* OriginSnapReason = nullptr;
	/** One entry per pawn hit, nearest first. */
	TArray<FVeloxShotHit, TInlineAllocator<4>> Hits;
	TArray<FVeloxShotRay, TInlineAllocator<12>> Rays; // a shotgun fires 11 pellets
	/** Now - time of the pose used: the hinted pawn's when its stamp was used, else the clamped ViewServerTime's. */
	float RewindMs = 0.f;
	/** (Now - clamped ViewServerTime) in ms, the world-time estimate alone (0 without lag compensation); B17. */
	float ViewMs = 0.f;
	EVeloxRewindMode HintMode = EVeloxRewindMode::Current; // Stamp if the hint was honoured
};

namespace VeloxLagComp
{
	/** Shared by the subsystem and the tests: one pawn's pose for one shot. */
	struct FPoseQuery
	{
		double Now = 0.0;
		double ViewServerTime = 0.0;
		float MaxRewindSeconds = 0.25f;
		bool bHinted = false;
		float HintStamp = 0.f;
		bool bCompensate = true;
	};

	/**
	 * Hinted pawn: by stamp if that pose is within MaxRewindSeconds, else like everyone else: by world time at
	 * clamp(ViewServerTime, Now - MaxRewindSeconds, Now). OutWorldTime is the time of the chosen pose.
	 */
	VELOX_API EVeloxRewindMode SelectPose(const FVeloxHitboxHistory& History, const FVeloxHitboxSample* Live, const FPoseQuery& Query, FVeloxHitboxPose& OutPose, double& OutWorldTime);

	/** One candidate pawn for CollectHits. */
	struct FCandidate
	{
		AActor* Pawn = nullptr;
		const FVeloxHitboxPose* Pose = nullptr;
	};

	/**
	 * Ray of Origin/Dir against Candidates, hits closer than WorldDist only. Appends to Result.Hits (merging pellets per
	 * pawn) and returns the ray's hit indices nearest first: one pawn, or up to MaxPenetrations with bPenetrate.
	 */
	VELOX_API void CollectRayHits(const FVector& Origin, const FVector& Dir, double WorldDist, TConstArrayView<FCandidate> Candidates, bool bPenetrate, int32 MaxPenetrations, FVeloxShotResult& Result, FVeloxShotRay& OutRay);

	/** Sorts Result.Hits by distance and remaps the rays' indices. */
	VELOX_API void SortHits(FVeloxShotResult& Result);

	/**
	 * The one way Velox code reads server time on a client (ADR 0003 B31): GetServerWorldTimeSeconds(), which lags true
	 * server time by the one-way latency (by design, B13) because AVeloxGameState stamps it in PreReplication.
	 */
	VELOX_API double EstimateServerTime(const class AGameStateBase& GameState);

	/** Last measured interpolation delay of a local player (B30), kept on the player controller. */
	struct FDelayMemory
	{
		float Delay = 0.f;
		/** World time of the measurement; lowest double = none yet. */
		double Time = -TNumericLimits<double>::Max();
	};

	/** A measurement older than this (s) is no longer reused as the fallback delay. */
	inline constexpr double FallbackReuseSeconds = 2.0;

	/**
	 * Fallback interpolation delay when no moving proxy is visible (B30): the last measurement if younger than
	 * FallbackReuseSeconds, else ConstantFallback.
	 */
	VELOX_API float ResolveFallbackDelay(const FDelayMemory& Memory, double Now, float ConstantFallback);

	/**
	 * Client shot timing for the local player of PC (ADR 0003 section 6): interpolation delay from the simulated Velox
	 * proxies, hint from the rendered proxy capsules along Origin/Dir. On the server (listen host) ViewServerTime is
	 * now and there is no hint.
	 */
	VELOX_API FVeloxShotTiming BuildShotTiming(const APlayerController& PC, const FVector& Origin, const FVector& Dir, float RangeCm, FDelayMemory& Memory);
}

/**
 * Records every registered hitbox (authority only) once per server frame at up to SampleRateHz, after all movement of
 * the frame (tickable objects run after the physics tick groups; remote players moved inside the net dispatch before
 * them), and resolves hitscan shots against the rewound poses. Exists in game/PIE worlds that are not clients.
 */
UCLASS()
class VELOX_API UVeloxLagCompensationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVeloxLagCompensationSubsystem* Get(const UObject* WorldContext);

	void RegisterHitbox(UVeloxHitboxComponent* Hitbox);
	void UnregisterHitbox(UVeloxHitboxComponent* Hitbox);
	bool IsRegistered(const AActor* Pawn) const;
	int32 GetNumRegistered() const { return Entries.Num(); }

	/**
	 * Pose of Pawn at Time: a server world time, or with bByStamp a PawnStamp. World time is clamped to
	 * [Now - MaxRewindSeconds, Now]; a stamp whose pose is older than that fails. Interpolates between samples;
	 * newer than the latest sample gives the current pose.
	 */
	bool RewindPoseFor(const AActor* Pawn, double Time, bool bByStamp, FVeloxHitboxPose& OutPose, float MaxRewindSeconds = 0.25f) const;

	/** Server hitscan: world trace on the Weapon channel per ray, then rays against the rewound poses. Logs VeloxShot. */
	void TraceShot(const FVeloxShotRequest& Request, FVeloxShotResult& OutResult);

	//~USubsystem / FTickableGameObject interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return Entries.Num() > 0; }
	virtual TStatId GetStatId() const override;
	//~End of interface

	/** Records one sample of every registered pawn now (Tick calls this at most SampleRateHz). Public for tests. */
	void SampleAll();

	static constexpr float SampleRateHz = 60.f;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FEntry
	{
		TWeakObjectPtr<UVeloxHitboxComponent> Hitbox;
		TWeakObjectPtr<AActor> Pawn;
		FVeloxHitboxHistory History;
		double LastDebugLogTime = -1.0;
	};

	const FEntry* FindEntry(const AActor* Pawn) const;
	/** Current pose and stamp of Entry as a sample at Now; false if the pawn is gone. */
	bool MakeLiveSample(const FEntry& Entry, double Now, FVeloxHitboxSample& OutLive) const;
	static float GetPawnStamp(const AActor* Pawn, double WorldTime);

	TArray<FEntry> Entries;
	/** From the settings CDO, not the configured asset: it only has to cover MaxRewind plus slack. */
	int32 HistoryCapacity = 60;
	double NextSampleTime = -1.0;

	/** Per-shot scratch (no allocation per shot after the first). */
	TArray<FVeloxHitboxPose> ScratchPoses;
	TArray<VeloxLagComp::FCandidate> ScratchCandidates;
};
