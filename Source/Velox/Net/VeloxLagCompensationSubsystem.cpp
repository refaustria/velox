// Velox - server-side lag compensation (ADR 0003 section 6).

#include "Net/VeloxLagCompensationSubsystem.h"

#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxHitboxComponent.h"
#include "Core/VeloxCollisionChannels.h"
#include "Core/VeloxLog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Movement/VeloxMovementComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxLagCompensationSubsystem)

namespace VeloxLagCompCVars
{
#if !UE_BUILD_SHIPPING
	static TAutoConsoleVariable<int32> CVarDebug(TEXT("velox.lagcomp.Debug"), 0,
		TEXT("Lag compensation logging (server, non-shipping). 1: per shot the rewind of every pawn, per pawn one sample line per second. 2: every sample."),
		ECVF_Default);
	static TAutoConsoleVariable<int32> CVarLagComp(TEXT("velox.combat.LagComp"), 1,
		TEXT("0: hitscan resolves against current poses (control run, ADR 0003 section 9). Server, non-shipping."),
		ECVF_Default);

	static int32 Debug() { return CVarDebug.GetValueOnGameThread(); }
	static bool Enabled() { return CVarLagComp.GetValueOnGameThread() != 0; }
#else
	static int32 Debug() { return 0; }
	static bool Enabled() { return true; }
#endif

	/** Proxies whose last update is older than this (s) are at rest or stale and say nothing about the delay. */
	constexpr double MaxProxyUpdateAge = 0.5;
	constexpr double MaxInterpolationDelay = 1.0;
}

// ---------------------------------------------------------------------------------------------------------------------
// FVeloxHitboxHistory

void FVeloxHitboxHistory::Init(int32 InCapacity)
{
	Samples.SetNum(FMath::Max(InCapacity, 2));
	Reset();
}

const FVeloxHitboxSample& FVeloxHitboxHistory::Get(int32 IndexFromOldest) const
{
	check(IndexFromOldest >= 0 && IndexFromOldest < Count);
	return Samples[(Head + IndexFromOldest) % Samples.Num()];
}

FVeloxHitboxSample& FVeloxHitboxHistory::Push()
{
	check(Samples.Num() > 0);
	const int32 Cap = Samples.Num();
	if (Count < Cap)
	{
		return Samples[(Head + Count++) % Cap];
	}
	FVeloxHitboxSample& Slot = Samples[Head];
	Head = (Head + 1) % Cap;
	return Slot;
}

void FVeloxHitboxHistory::Push(double WorldTime, float PawnStamp, const FVeloxHitboxPose& Pose)
{
	FVeloxHitboxSample& Slot = Push();
	Slot.WorldTime = WorldTime;
	Slot.PawnStamp = PawnStamp;
	Slot.Pose.Shapes = Pose.Shapes;
}

void FVeloxHitboxHistory::LerpPose(const FVeloxHitboxPose& A, const FVeloxHitboxPose& B, double Alpha, FVeloxHitboxPose& OutPose)
{
	if (A.Shapes.Num() != B.Shapes.Num())
	{
		OutPose.Shapes = Alpha < 0.5 ? A.Shapes : B.Shapes;
		return;
	}
	OutPose.Shapes.Reset();
	for (int32 Index = 0; Index < A.Shapes.Num(); ++Index)
	{
		const FVeloxHitShape& SA = A.Shapes[Index];
		const FVeloxHitShape& SB = B.Shapes[Index];
		FVeloxHitShape& S = OutPose.Shapes.Add_GetRef(SA);
		S.Centre = FMath::Lerp(SA.Centre, SB.Centre, Alpha);
		S.Axis = FMath::Lerp(SA.Axis, SB.Axis, Alpha).GetSafeNormal(UE_SMALL_NUMBER, SA.Axis);
		S.Radius = static_cast<float>(FMath::Lerp(static_cast<double>(SA.Radius), static_cast<double>(SB.Radius), Alpha));
		S.HalfHeight = static_cast<float>(FMath::Lerp(static_cast<double>(SA.HalfHeight), static_cast<double>(SB.HalfHeight), Alpha));
	}
}

bool FVeloxHitboxHistory::PoseAtWorldTime(double T, const FVeloxHitboxSample* Live, FVeloxHitboxPose& OutPose, double& OutWorldTime) const
{
	const int32 N = Count + (Live ? 1 : 0);
	if (N == 0)
	{
		return false;
	}
	const FVeloxHitboxSample& First = Point(0, Live);
	const FVeloxHitboxSample& Last = Point(N - 1, Live);
	if (T <= First.WorldTime || N == 1)
	{
		OutPose.Shapes = First.Pose.Shapes;
		OutWorldTime = First.WorldTime;
		return true;
	}
	if (T >= Last.WorldTime)
	{
		OutPose.Shapes = Last.Pose.Shapes;
		OutWorldTime = Last.WorldTime;
		return true;
	}
	// Newest-first: shots rewind ~0.1 s into a 1 s ring.
	for (int32 Index = N - 2; Index >= 0; --Index)
	{
		const FVeloxHitboxSample& A = Point(Index, Live);
		if (A.WorldTime <= T)
		{
			const FVeloxHitboxSample& B = Point(Index + 1, Live);
			const double Span = B.WorldTime - A.WorldTime;
			const double Alpha = Span > UE_DOUBLE_SMALL_NUMBER ? (T - A.WorldTime) / Span : 1.0;
			LerpPose(A.Pose, B.Pose, Alpha, OutPose);
			OutWorldTime = T;
			return true;
		}
	}
	return false; // unreachable: T > First.WorldTime
}

bool FVeloxHitboxHistory::PoseAtStamp(float Stamp, double MinWorldTime, const FVeloxHitboxSample* Live, FVeloxHitboxPose& OutPose, double& OutWorldTime) const
{
	const int32 N = Count + (Live ? 1 : 0);
	if (N == 0 || Stamp < Point(0, Live).PawnStamp)
	{
		return false;
	}
	const FVeloxHitboxSample& Last = Point(N - 1, Live);
	if (Stamp >= Last.PawnStamp)
	{
		// The client renders up to 15 % of a correction delta ahead (engine forward prediction): the newest point.
		if (Last.WorldTime < MinWorldTime)
		{
			return false;
		}
		OutPose.Shapes = Last.Pose.Shapes;
		OutWorldTime = Last.WorldTime;
		return true;
	}
	// Newest sample with PawnStamp <= Stamp; its successor has a larger stamp (Stamp < Last.PawnStamp). A run of equal
	// stamps (pawn at rest, the engine stamps only moves that changed the transform) resolves to the newest of the run.
	for (int32 Index = N - 2; Index >= 0; --Index)
	{
		const FVeloxHitboxSample& A = Point(Index, Live);
		if (A.PawnStamp <= Stamp)
		{
			const FVeloxHitboxSample& B = Point(Index + 1, Live);
			const double Span = static_cast<double>(B.PawnStamp) - static_cast<double>(A.PawnStamp);
			const double Alpha = Span > 0.0 ? (static_cast<double>(Stamp) - static_cast<double>(A.PawnStamp)) / Span : 0.0;
			const double WorldTime = FMath::Lerp(A.WorldTime, B.WorldTime, Alpha);
			if (WorldTime < MinWorldTime)
			{
				return false;
			}
			LerpPose(A.Pose, B.Pose, Alpha, OutPose);
			OutWorldTime = WorldTime;
			return true;
		}
	}
	return false; // unreachable: Stamp >= Point(0).PawnStamp
}

// ---------------------------------------------------------------------------------------------------------------------
// World-free shot resolution

EVeloxRewindMode VeloxLagComp::SelectPose(const FVeloxHitboxHistory& History, const FVeloxHitboxSample* Live, const FPoseQuery& Query, FVeloxHitboxPose& OutPose, double& OutWorldTime)
{
	OutPose.Shapes.Reset();
	OutWorldTime = Query.Now;
	if (!Query.bCompensate)
	{
		if (Live)
		{
			OutPose.Shapes = Live->Pose.Shapes;
		}
		else if (const FVeloxHitboxSample* Newest = History.Newest())
		{
			OutPose.Shapes = Newest->Pose.Shapes;
			OutWorldTime = Newest->WorldTime;
		}
		return EVeloxRewindMode::Current;
	}

	const double MinTime = Query.Now - FMath::Max(Query.MaxRewindSeconds, 0.f);
	if (Query.bHinted && History.PoseAtStamp(Query.HintStamp, MinTime, Live, OutPose, OutWorldTime))
	{
		return EVeloxRewindMode::Stamp;
	}
	const double T = FMath::Clamp(Query.ViewServerTime, MinTime, Query.Now);
	if (History.PoseAtWorldTime(T, Live, OutPose, OutWorldTime))
	{
		return EVeloxRewindMode::WorldTime;
	}
	return EVeloxRewindMode::Current;
}

void VeloxLagComp::CollectRayHits(const FVector& Origin, const FVector& Dir, double WorldDist, TConstArrayView<FCandidate> Candidates, bool bPenetrate, int32 MaxPenetrations, FVeloxShotResult& Result, FVeloxShotRay& OutRay)
{
	struct FRayHit
	{
		double Dist;
		int32 Candidate;
	};
	TArray<FRayHit, TInlineAllocator<8>> RayHits;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FCandidate& C = Candidates[Index];
		double Dist;
		int32 Shape;
		if (C.Pawn && C.Pose && FVeloxHitMath::RayPose(Origin, Dir, WorldDist, *C.Pose, Dist, Shape) && Dist < WorldDist)
		{
			RayHits.Add({Dist, Index});
		}
	}
	RayHits.Sort([](const FRayHit& A, const FRayHit& B) { return A.Dist < B.Dist; });
	const int32 Take = FMath::Min(RayHits.Num(), bPenetrate ? FMath::Max(MaxPenetrations, 1) : 1);
	for (int32 Index = 0; Index < Take; ++Index)
	{
		const FRayHit& RH = RayHits[Index];
		AActor* Pawn = Candidates[RH.Candidate].Pawn;
		const FVector Location = Origin + Dir * RH.Dist;
		int32 HitIndex = Result.Hits.IndexOfByPredicate([Pawn](const FVeloxShotHit& H) { return H.Pawn == Pawn; });
		if (HitIndex == INDEX_NONE)
		{
			HitIndex = Result.Hits.Add({Pawn, RH.Dist, Location, 0});
		}
		FVeloxShotHit& Hit = Result.Hits[HitIndex];
		++Hit.PelletHits;
		if (RH.Dist < Hit.Dist)
		{
			Hit.Dist = RH.Dist;
			Hit.Location = Location;
		}
		OutRay.HitIndices.Add(HitIndex);
	}
	if (Take > 0 && !bPenetrate)
	{
		// A non-penetrating ray stops in the nearest pawn.
		OutRay.End = Origin + Dir * RayHits[0].Dist;
		OutRay.ImpactNormal = -Dir;
		OutRay.bHitWorld = false;
	}
}

void VeloxLagComp::SortHits(FVeloxShotResult& Result)
{
	const int32 Num = Result.Hits.Num();
	if (Num < 2)
	{
		return;
	}
	TArray<int32, TInlineAllocator<8>> Order;
	for (int32 Index = 0; Index < Num; ++Index)
	{
		Order.Add(Index);
	}
	Order.StableSort([&Result](int32 A, int32 B) { return Result.Hits[A].Dist < Result.Hits[B].Dist; });
	TArray<int32, TInlineAllocator<8>> NewIndexOf;
	NewIndexOf.SetNumUninitialized(Num);
	decltype(Result.Hits) Sorted;
	for (int32 NewIndex = 0; NewIndex < Num; ++NewIndex)
	{
		NewIndexOf[Order[NewIndex]] = NewIndex;
		Sorted.Add(Result.Hits[Order[NewIndex]]);
	}
	Result.Hits = MoveTemp(Sorted);
	for (FVeloxShotRay& Ray : Result.Rays)
	{
		for (int32& Index : Ray.HitIndices)
		{
			Index = NewIndexOf[Index];
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Client shot timing

FVeloxShotTiming VeloxLagComp::BuildShotTiming(const APlayerController& PC, const FVector& Origin, const FVector& Dir, float RangeCm)
{
	FVeloxShotTiming Timing;
	UWorld* World = PC.GetWorld();
	if (!World)
	{
		return Timing;
	}
	const double Now = World->GetTimeSeconds();
	const AGameStateBase* GameState = World->GetGameState();
	const double ServerNow = GameState ? GameState->GetServerWorldTimeSeconds() : Now;
	Timing.ViewServerTime = ServerNow;
	if (PC.GetNetMode() != NM_Client)
	{
		// Listen host / bots: the server's poses are what is drawn. No rewind, no hint.
		return Timing;
	}

	const APawn* OwnPawn = PC.GetPawn();
	double DelaySum = 0.0;
	int32 DelayCount = 0;
	double BestDist = RangeCm;
	const ACharacter* BestTarget = nullptr;
	float BestStamp = 0.f;
	FVeloxHitboxPose Pose;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		const ACharacter* Character = *It;
		if (Character == OwnPawn || Character->GetLocalRole() != ROLE_SimulatedProxy)
		{
			continue;
		}
		const UVeloxMovementComponent* Movement = Cast<UVeloxMovementComponent>(Character->GetCharacterMovement());
		if (!Movement || !Movement->HasPredictionData_Client())
		{
			continue;
		}
		const FNetworkPredictionData_Client_Character* ClientData = Movement->GetPredictionData_Client_Character();
		if (ClientData && ClientData->LastCorrectionTime > 0.0)
		{
			// Rendered pose = the latest received state (sent ~Age ago in the GameState's server-time estimate) minus the
			// smoothing lag still to cover. Proxies at rest stop receiving updates and are left out.
			const double Age = Now - ClientData->LastCorrectionTime;
			if (Age <= VeloxLagCompCVars::MaxProxyUpdateAge && !Movement->Velocity.IsNearlyZero())
			{
				DelaySum += (ClientData->SmoothingServerTimeStamp - ClientData->SmoothingClientTimeStamp) + Age;
				++DelayCount;
			}
		}

		const UVeloxHitboxComponent* Hitbox = UVeloxHitboxComponent::FindHitboxComponent(Character);
		if (!Hitbox)
		{
			continue;
		}
		Hitbox->GetRenderedPose(Pose);
		double Dist;
		int32 Shape;
		if (FVeloxHitMath::RayPose(Origin, Dir, BestDist, Pose, Dist, Shape) && Dist < BestDist)
		{
			const double Stamp = Movement->GetRenderedServerStamp();
			if (Stamp > 0.0)
			{
				BestDist = Dist;
				BestTarget = Character;
				BestStamp = static_cast<float>(Stamp);
			}
		}
	}

	if (DelayCount > 0)
	{
		Timing.InterpolationDelay = static_cast<float>(DelaySum / DelayCount);
		Timing.bDelayFromProxies = true;
	}
	else
	{
		// ADR 0003 section 6 fallback.
		const APlayerState* PlayerState = PC.PlayerState;
		const float HalfPing = PlayerState ? PlayerState->GetPingInMilliseconds() / 2000.f : 0.f;
		Timing.InterpolationDelay = HalfPing + GetDefault<UVeloxMovementComponent>()->NetworkSimulatedSmoothLocationTime;
	}
	Timing.InterpolationDelay = FMath::Clamp(Timing.InterpolationDelay, 0.f, static_cast<float>(VeloxLagCompCVars::MaxInterpolationDelay));
	Timing.ViewServerTime = ServerNow - Timing.InterpolationDelay;

	if (BestTarget)
	{
		// The cosmetic world trace decides too: a proxy behind a wall is no hint.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VeloxShotHint), true, OwnPawn);
		FHitResult WorldHit;
		if (!World->LineTraceSingleByChannel(WorldHit, Origin, Origin + Dir * BestDist, Velox_TraceChannel_Weapon, Params))
		{
			Timing.HintTarget = const_cast<ACharacter*>(BestTarget);
			Timing.HintTargetStamp = BestStamp;
		}
	}
	return Timing;
}

// ---------------------------------------------------------------------------------------------------------------------
// Request helpers

void FVeloxShotRequest::ApplySettings(const UVeloxCombatSettings* Settings)
{
	const UVeloxCombatSettings& S = UVeloxCombatSettings::GetOrDefault(Settings);
	MaxRewindSeconds = S.MaxRewindSeconds;
	MaxOriginErrorCm = S.MaxOriginErrorCm;
}

void FVeloxShotRequest::ApplyTiming(const FVeloxShotTiming& Timing)
{
	ViewServerTime = Timing.ViewServerTime;
	HintTarget = Timing.HintTarget;
	HintTargetStamp = Timing.HintTargetStamp;
}

// ---------------------------------------------------------------------------------------------------------------------
// UVeloxLagCompensationSubsystem

UVeloxLagCompensationSubsystem* UVeloxLagCompensationSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UVeloxLagCompensationSubsystem>() : nullptr;
}

bool UVeloxLagCompensationSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UVeloxLagCompensationSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Best effort: a client world may not know its net mode yet when subsystems are created. Registration is
	// authority-only (UVeloxHitboxComponent::BeginPlay), so a client-side instance stays empty and never ticks.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->GetNetMode() != NM_Client;
}

void UVeloxLagCompensationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(nullptr);
	const float Window = FMath::Max(Settings.HistoryWindowSeconds, Settings.MaxRewindSeconds + 0.1f);
	HistoryCapacity = FMath::Max(FMath::CeilToInt32(Window * SampleRateHz), 2);
}

void UVeloxLagCompensationSubsystem::Deinitialize()
{
	Entries.Reset();
	Super::Deinitialize();
}

TStatId UVeloxLagCompensationSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVeloxLagCompensationSubsystem, STATGROUP_Tickables);
}

void UVeloxLagCompensationSubsystem::RegisterHitbox(UVeloxHitboxComponent* Hitbox)
{
	AActor* Pawn = Hitbox ? Hitbox->GetOwner() : nullptr;
	const UWorld* World = GetWorld();
	if (!Pawn || !World || World->GetNetMode() == NM_Client || !Pawn->HasAuthority())
	{
		return;
	}
	if (Entries.ContainsByPredicate([Hitbox](const FEntry& E) { return E.Hitbox.Get() == Hitbox; }))
	{
		return;
	}
	FEntry& Entry = Entries.AddDefaulted_GetRef();
	Entry.Hitbox = Hitbox;
	Entry.Pawn = Pawn;
	Entry.History.Init(HistoryCapacity);
	UE_LOG(LogVeloxNet, Log, TEXT("VeloxLagCompRegister pawn=%s capacity=%d registered=%d"), *GetNameSafe(Pawn), HistoryCapacity, Entries.Num());
}

void UVeloxLagCompensationSubsystem::UnregisterHitbox(UVeloxHitboxComponent* Hitbox)
{
	const int32 Removed = Entries.RemoveAllSwap([Hitbox](const FEntry& E) { return !E.Hitbox.IsValid() || E.Hitbox.Get() == Hitbox; });
	if (Removed > 0)
	{
		UE_LOG(LogVeloxNet, Log, TEXT("VeloxLagCompUnregister pawn=%s registered=%d"), *GetNameSafe(Hitbox ? Hitbox->GetOwner() : nullptr), Entries.Num());
	}
}

bool UVeloxLagCompensationSubsystem::IsRegistered(const AActor* Pawn) const
{
	return FindEntry(Pawn) != nullptr;
}

const UVeloxLagCompensationSubsystem::FEntry* UVeloxLagCompensationSubsystem::FindEntry(const AActor* Pawn) const
{
	return Pawn ? Entries.FindByPredicate([Pawn](const FEntry& E) { return E.Pawn.Get() == Pawn; }) : nullptr;
}

float UVeloxLagCompensationSubsystem::GetPawnStamp(const AActor* Pawn, double WorldTime)
{
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return Movement ? Movement->GetServerLastTransformUpdateTimeStamp() : static_cast<float>(WorldTime);
}

bool UVeloxLagCompensationSubsystem::MakeLiveSample(const FEntry& Entry, double Now, FVeloxHitboxSample& OutLive) const
{
	const UVeloxHitboxComponent* Hitbox = Entry.Hitbox.Get();
	if (!Hitbox)
	{
		return false;
	}
	Hitbox->GetCurrentPose(OutLive.Pose);
	OutLive.WorldTime = Now;
	OutLive.PawnStamp = GetPawnStamp(Entry.Pawn.Get(), Now);
	if (const FVeloxHitboxSample* Newest = Entry.History.Newest())
	{
		// Keep the history invariants for the virtual newest point.
		OutLive.WorldTime = FMath::Max(OutLive.WorldTime, Newest->WorldTime);
		OutLive.PawnStamp = FMath::Max(OutLive.PawnStamp, Newest->PawnStamp);
	}
	return OutLive.Pose.Shapes.Num() > 0;
}

void UVeloxLagCompensationSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	// 60 Hz on average on a faster server (a fixed schedule with 10 % slack for frame-time jitter, so a 60 Hz server
	// samples every frame); a slower server samples every frame.
	const double Interval = 1.0 / SampleRateHz;
	if (NextSampleTime >= 0.0 && Now + 0.1 * Interval < NextSampleTime)
	{
		return;
	}
	NextSampleTime = NextSampleTime < 0.0 ? Now + Interval : FMath::Max(NextSampleTime + Interval, Now);
	SampleAll();
}

void UVeloxLagCompensationSubsystem::SampleAll()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const int32 DebugLevel = VeloxLagCompCVars::Debug();
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		FEntry& Entry = Entries[Index];
		const UVeloxHitboxComponent* Hitbox = Entry.Hitbox.Get();
		const AActor* Pawn = Entry.Pawn.Get();
		if (!Hitbox || !Pawn)
		{
			Entries.RemoveAtSwap(Index);
			continue;
		}
		const FVeloxHitboxSample* Newest = Entry.History.Newest();
		if (Newest && Newest->WorldTime >= Now)
		{
			continue; // already sampled this frame
		}
		const float Stamp = FMath::Max(GetPawnStamp(Pawn, Now), Newest ? Newest->PawnStamp : -FLT_MAX);
		FVeloxHitboxSample& Slot = Entry.History.Push();
		Slot.WorldTime = Now;
		Slot.PawnStamp = Stamp;
		Hitbox->GetCurrentPose(Slot.Pose); // reuses the slot's inline storage: no allocation

		if (DebugLevel >= 2 || (DebugLevel >= 1 && Now - Entry.LastDebugLogTime >= 1.0))
		{
			Entry.LastDebugLogTime = Now;
			const FVector Centre = Slot.Pose.Shapes.Num() > 0 ? Slot.Pose.Shapes[0].Centre : FVector::ZeroVector;
			const float HalfHeight = Slot.Pose.Shapes.Num() > 0 ? Slot.Pose.Shapes[0].HalfHeight : 0.f;
			const FVeloxHitboxSample& Oldest = Entry.History.Get(0);
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxLagCompSample pawn=%s t=%.3f stamp=%.3f n=%d span_ms=%.0f loc=(%.1f,%.1f,%.1f) hh=%.1f"),
				*Pawn->GetName(), Now, Stamp, Entry.History.Num(), (Now - Oldest.WorldTime) * 1000.0, Centre.X, Centre.Y, Centre.Z, HalfHeight);
		}
	}
}

bool UVeloxLagCompensationSubsystem::RewindPoseFor(const AActor* Pawn, double Time, bool bByStamp, FVeloxHitboxPose& OutPose, float MaxRewindSeconds) const
{
	const FEntry* Entry = FindEntry(Pawn);
	const UWorld* World = GetWorld();
	if (!Entry || !World)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	FVeloxHitboxSample Live;
	const bool bHasLive = MakeLiveSample(*Entry, Now, Live);
	const double MinTime = Now - FMath::Max(MaxRewindSeconds, 0.f);
	double PoseTime;
	if (bByStamp)
	{
		return Entry->History.PoseAtStamp(static_cast<float>(Time), MinTime, bHasLive ? &Live : nullptr, OutPose, PoseTime);
	}
	return Entry->History.PoseAtWorldTime(FMath::Clamp(Time, MinTime, Now), bHasLive ? &Live : nullptr, OutPose, PoseTime);
}

void UVeloxLagCompensationSubsystem::TraceShot(const FVeloxShotRequest& Request, FVeloxShotResult& OutResult)
{
	OutResult = FVeloxShotResult();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	AActor* Shooter = Request.Shooter.Get();
	const AActor* Hint = Request.HintTarget.Get();
	const int32 DebugLevel = VeloxLagCompCVars::Debug();

	// Origin validation (ADR 0003 section 5): the fire RPC and the move RPC of one client frame may arrive in either order.
	OutResult.Origin = Request.Origin;
	if (const APawn* ShooterPawn = Cast<APawn>(Shooter); ShooterPawn && Request.bValidateOrigin)
	{
		const FVector Eye = ShooterPawn->GetPawnViewLocation();
		if (FVector::Dist(Request.Origin, Eye) > Request.MaxOriginErrorCm)
		{
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotOriginSnap seq=%u shooter=%s err_cm=%.1f"), Request.ShotSeq, *GetNameSafe(Shooter), FVector::Dist(Request.Origin, Eye));
			OutResult.Origin = Eye;
			OutResult.bOriginSnapped = true;
		}
	}
	const FVector Origin = OutResult.Origin;

	// Poses: one per pawn per shot, shared by all pellets.
	VeloxLagComp::FPoseQuery Query;
	Query.Now = Now;
	Query.ViewServerTime = Request.ViewServerTime;
	Query.MaxRewindSeconds = Request.MaxRewindSeconds;
	Query.HintStamp = Request.HintTargetStamp;
	Query.bCompensate = VeloxLagCompCVars::Enabled();
	const double ClampedViewTime = FMath::Clamp(Request.ViewServerTime, Now - FMath::Max(Request.MaxRewindSeconds, 0.f), Now);
	double RewindSeconds = Query.bCompensate ? Now - ClampedViewTime : 0.0;

	ScratchPoses.SetNum(Entries.Num(), EAllowShrinking::No);
	ScratchCandidates.Reset();
	FVeloxHitboxSample Live;
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const FEntry& Entry = Entries[Index];
		AActor* Pawn = Entry.Pawn.Get();
		if (!Pawn || Pawn == Shooter || !MakeLiveSample(Entry, Now, Live))
		{
			continue;
		}
		Query.bHinted = Pawn == Hint;
		double PoseTime;
		const EVeloxRewindMode Mode = VeloxLagComp::SelectPose(Entry.History, &Live, Query, ScratchPoses[Index], PoseTime);
		if (Query.bHinted)
		{
			OutResult.HintMode = Mode;
			if (Mode == EVeloxRewindMode::Stamp)
			{
				RewindSeconds = Now - PoseTime;
			}
		}
		ScratchCandidates.Add({Pawn, &ScratchPoses[Index]});

		if (DebugLevel >= 1)
		{
			const FVeloxHitboxPose& Pose = ScratchPoses[Index];
			const double Offset = Pose.Shapes.Num() > 0 ? FVector::Dist(Pose.Shapes[0].Centre, Live.Pose.Shapes[0].Centre) : 0.0;
			const TCHAR* ModeName = Mode == EVeloxRewindMode::Stamp ? TEXT("stamp") : Mode == EVeloxRewindMode::WorldTime ? TEXT("time") : TEXT("current");
			UE_LOG(LogVeloxNet, Log, TEXT("VeloxLagCompRewind seq=%u pawn=%s mode=%s rewind_ms=%.1f offset_cm=%.1f hinted=%d hint_stamp=%.3f live_stamp=%.3f samples=%d"),
				Request.ShotSeq, *Pawn->GetName(), ModeName, (Now - PoseTime) * 1000.0, Offset, Query.bHinted ? 1 : 0,
				Request.HintTargetStamp, Live.PawnStamp, Entry.History.Num());
		}
	}
	OutResult.RewindMs = static_cast<float>(RewindSeconds * 1000.0);

	// Rays: world first (pawn capsules ignore the Weapon channel), then the rewound poses closer than the world.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VeloxShot), true, Shooter);
	const double Range = FMath::Max(Request.RangeCm, 0.f);
	for (const FVector& RawDir : Request.Dirs)
	{
		const FVector Dir = RawDir.GetSafeNormal();
		if (Dir.IsZero())
		{
			continue;
		}
		FVeloxShotRay& Ray = OutResult.Rays.AddDefaulted_GetRef();
		FHitResult WorldHit;
		if (World->LineTraceSingleByChannel(WorldHit, Origin, Origin + Dir * Range, Velox_TraceChannel_Weapon, Params))
		{
			Ray.bHitWorld = true;
			Ray.WorldDist = WorldHit.Distance;
			Ray.End = WorldHit.ImpactPoint;
			Ray.ImpactNormal = WorldHit.ImpactNormal;
		}
		else
		{
			Ray.WorldDist = Range;
			Ray.End = Origin + Dir * Range;
		}
		VeloxLagComp::CollectRayHits(Origin, Dir, Ray.WorldDist, ScratchCandidates, Request.bPenetratePawns, Request.MaxPenetrations, OutResult, Ray);
	}
	VeloxLagComp::SortHits(OutResult);

	const TCHAR* HintName = !Hint ? TEXT("none") : OutResult.HintMode == EVeloxRewindMode::Stamp ? TEXT("stamp") : TEXT("fallback");
	const double DistCm = OutResult.Hits.Num() > 0 ? OutResult.Hits[0].Dist : (OutResult.Rays.Num() > 0 ? OutResult.Rays[0].WorldDist : 0.0);
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShot seq=%u shooter=%s hit=%s rewind_ms=%.1f hint=%s dist_cm=%.1f hits=%d rays=%d snap=%d"),
		Request.ShotSeq, *GetNameSafe(Shooter), OutResult.Hits.Num() > 0 ? *GetNameSafe(OutResult.Hits[0].Pawn) : TEXT("none"),
		OutResult.RewindMs, HintName, DistCm, OutResult.Hits.Num(), OutResult.Rays.Num(), OutResult.bOriginSnapped ? 1 : 0);
}
