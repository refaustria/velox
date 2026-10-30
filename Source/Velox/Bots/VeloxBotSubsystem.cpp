// Velox - server-side bot world state (ADR 0005 sections 3, 4, 12).

#include "Bots/VeloxBotSubsystem.h"

#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxNavLink.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Items/VeloxPickup.h"
#include "Movement/VeloxJumpPad.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Weapons/VeloxProjectile.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotSubsystem)

namespace VeloxBotSubsystemPrivate
{
	/** Cadence of the nav-ready poll until the initial build finished (the generation delegate also triggers a poll). */
	constexpr float NavPollSeconds = 0.1f;
	/** One warning if the initial build has not finished after this long (ADR 0005 section 13: <= 1 s expected). */
	constexpr double NavSlowWarnSeconds = 30.0;

	TAutoConsoleVariable<int32> CVarBotSeed(TEXT("velox.bots.Seed"), 0,
		TEXT("Master random seed of the bots (ADR 0005 section 12). 0 = time-seeded; non-zero = deterministic (functional tests)."),
		ECVF_Default);
}

UVeloxBotSubsystem* UVeloxBotSubsystem::Get(const UWorld* World)
{
	return World ? World->GetSubsystem<UVeloxBotSubsystem>() : nullptr;
}

int32 UVeloxBotSubsystem::FindNearestEnd(const TArray<FVector>& Ends, const FVector& Point, double Radius, double MaxDeltaZ)
{
	int32 Best = INDEX_NONE;
	double BestDistSq = FMath::Square(Radius);
	for (int32 i = 0; i < Ends.Num(); ++i)
	{
		const FVector Delta = Ends[i] - Point;
		const double DistSq = Delta.SizeSquared2D();
		if (FMath::Abs(Delta.Z) <= MaxDeltaZ && DistSq <= BestDistSq)
		{
			Best = i;
			BestDistSq = DistSq;
		}
	}
	return Best;
}

bool UVeloxBotSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->GetNetMode() != NM_Client;
}

bool UVeloxBotSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVeloxBotSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	WorldInitSeconds = FPlatformTime::Seconds();
}

void UVeloxBotSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(NavPollHandle);
		if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
		{
			NavSys->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this, &ThisClass::OnNavigationGenerationFinished);
		}
	}
	NavReadyCallbacks.Reset();
	Super::Deinitialize();
}

void UVeloxBotSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	TimeSeed = static_cast<int32>(FPlatformTime::Cycles());
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBots seed=%d (velox.bots.Seed=%d)"), GetMasterSeed(), VeloxBotSubsystemPrivate::CVarBotSeed.GetValueOnGameThread());

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld);
	if (!NavSys)
	{
		UE_LOG(LogVeloxBots, Warning, TEXT("VeloxNav unavailable: the world has no navigation system (bots cannot path)."));
		return;
	}
	NavSys->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(this, &ThisClass::OnNavigationGenerationFinished);
	PollNavReady();
	if (!bNavReady && !bNavUnavailable)
	{
		InWorld.GetTimerManager().SetTimer(NavPollHandle, this, &ThisClass::PollNavReady, VeloxBotSubsystemPrivate::NavPollSeconds, true);
	}
}

ANavigationData* UVeloxBotSubsystem::GetNavData() const
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	return NavSys ? NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate) : nullptr;
}

void UVeloxBotSubsystem::OnNavigationGenerationFinished(ANavigationData* /*NavData*/)
{
	if (!bNavReady && !bNavUnavailable)
	{
		PollNavReady();
	}
}

void UVeloxBotSubsystem::PollNavReady()
{
	if (bNavReady || bNavUnavailable)
	{
		return;
	}
	UWorld* World = GetWorld();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(GetNavData());
	const int32 Tiles = Recast ? Recast->GetNumActiveTiles() : 0;
	const bool bBuilding = NavSys && NavSys->IsNavigationBuildInProgress();
	if (Tiles > 0 && !bBuilding)
	{
		MarkNavReady();
		return;
	}
	if (!Recast && NavSys && NavSys->GetNavigationBounds().Num() == 0)
	{
		// Maps without a NavMeshBoundsVolume (menu, L_CombatTest) never get nav data in 5.8: say so once and stop polling.
		bNavUnavailable = true;
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxNav unavailable: no NavMeshBoundsVolume in %s (bots cannot path on this map)"), World ? *World->GetMapName() : TEXT("?"));
		if (World)
		{
			World->GetTimerManager().ClearTimer(NavPollHandle);
		}
		return;
	}
	const double Elapsed = FPlatformTime::Seconds() - WorldInitSeconds;
	if (!bLoggedBuilding)
	{
		bLoggedBuilding = true;
		const TArray<FNavDataConfig> NoAgents;
		const TArray<FNavDataConfig>& Agents = NavSys ? NavSys->GetSupportedAgents() : NoAgents;
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxNav building... navdata=%s tiles=%d agents=%d agent0=%s class0=%s nav_bounds=%d"), *GetNameSafe(Recast), Tiles, Agents.Num(),
			Agents.Num() ? *Agents[0].Name.ToString() : TEXT("none"), Agents.Num() ? *GetNameSafe(Agents[0].GetNavDataClass<ANavigationData>()) : TEXT("none"),
			NavSys ? NavSys->GetNavigationBounds().Num() : 0);
	}
	else if (!bWarnedSlowBuild && Elapsed > VeloxBotSubsystemPrivate::NavSlowWarnSeconds)
	{
		bWarnedSlowBuild = true;
		UE_LOG(LogVeloxBots, Warning, TEXT("VeloxNav not ready after %.0f s: navdata=%s tiles=%d building=%d"), Elapsed, *GetNameSafe(Recast), Tiles, bBuilding ? 1 : 0);
	}
}

int32 UVeloxBotSubsystem::CountNavRelevantTriggers() const
{
	int32 Relevant = 0;
	auto CountActor = [&Relevant](const AActor* Actor)
	{
		TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
		for (const UPrimitiveComponent* Component : Components)
		{
			if (Component && Component->IsNavigationRelevant())
			{
				++Relevant;
				UE_LOG(LogVeloxBots, Warning, TEXT("VeloxNav relevant trigger: %s.%s exports to the nav mesh"), *Actor->GetName(), *Component->GetName());
			}
		}
	};
	for (TActorIterator<AVeloxJumpPad> It(GetWorld()); It; ++It)
	{
		CountActor(*It);
	}
	for (TActorIterator<AVeloxPickup> It(GetWorld()); It; ++It)
	{
		CountActor(*It);
	}
	return Relevant;
}

void UVeloxBotSubsystem::MarkNavReady()
{
	bNavReady = true;
	NavBuildSeconds = FPlatformTime::Seconds() - WorldInitSeconds;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(NavPollHandle);
	}

	const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(GetNavData());
	const int32 Tiles = Recast ? Recast->GetNumActiveTiles() : 0;
	const int32 Links = GetNavLinks().Num();
	const int32 RelevantTriggers = CountNavRelevantTriggers();
	if (Recast)
	{
		const ENavigationDataResolution Res = ENavigationDataResolution::Default;
		const UEnum* GenEnum = StaticEnum<ERuntimeGenerationType>();
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxNav built tiles=%d seconds=%.3f links=%d relevant_triggers=%d runtime=%s agent_radius=%.1f agent_height=%.1f step=%.1f cell=%.1f cell_height=%.1f slope=%.1f tile_size=%.0f"),
			Tiles, NavBuildSeconds, Links, RelevantTriggers,
			GenEnum ? *GenEnum->GetNameStringByValue(static_cast<int64>(Recast->GetRuntimeGenerationMode())) : TEXT("?"),
			Recast->AgentRadius, Recast->AgentHeight, Recast->GetAgentMaxStepHeight(Res), Recast->GetCellSize(Res), Recast->GetCellHeight(Res),
			Recast->AgentMaxSlope, Recast->TileSizeUU);
	}
	if (RelevantTriggers > 0)
	{
		UE_LOG(LogVeloxBots, Warning, TEXT("VeloxNav: %d pad/pickup trigger components are nav-relevant (ADR 0005 section 3 expects none)"), RelevantTriggers);
	}

	TArray<TFunction<void()>> Callbacks = MoveTemp(NavReadyCallbacks);
	NavReadyCallbacks.Reset();
	for (TFunction<void()>& Callback : Callbacks)
	{
		Callback();
	}
}

void UVeloxBotSubsystem::CallWhenNavReady(TFunction<void()> Callback)
{
	if (bNavReady)
	{
		Callback();
	}
	else if (bNavUnavailable)
	{
		UE_LOG(LogVeloxBots, Warning, TEXT("VeloxNav unavailable on this map: nav request dropped."));
	}
	else
	{
		NavReadyCallbacks.Add(MoveTemp(Callback));
	}
}

void UVeloxBotSubsystem::RegisterNavLink(AVeloxNavLink* Link)
{
	if (Link)
	{
		NavLinks.AddUnique(Link);
	}
}

void UVeloxBotSubsystem::UnregisterNavLink(AVeloxNavLink* Link)
{
	NavLinks.RemoveAll([Link](const TWeakObjectPtr<AVeloxNavLink>& Entry) { return !Entry.IsValid() || Entry.Get() == Link; });
}

TArray<AVeloxNavLink*> UVeloxBotSubsystem::GetNavLinks() const
{
	TArray<AVeloxNavLink*> Result;
	Result.Reserve(NavLinks.Num());
	for (const TWeakObjectPtr<AVeloxNavLink>& Entry : NavLinks)
	{
		if (AVeloxNavLink* Link = Entry.Get())
		{
			Result.Add(Link);
		}
	}
	return Result;
}

FVeloxNavLinkMatch UVeloxBotSubsystem::FindLinkByStart(const FVector& Point, double Radius) const
{
	FVeloxNavLinkMatch Match;
	TArray<FVector> Ends;
	TArray<TPair<AVeloxNavLink*, bool>> Owners;
	double MaxSnapHeight = 0.0;
	for (AVeloxNavLink* Link : GetNavLinks())
	{
		if (!Link->IsLinkEnabled())
		{
			continue;
		}
		MaxSnapHeight = FMath::Max(MaxSnapHeight, static_cast<double>(Link->GetSnapHeight()));
		Ends.Add(Link->GetStartWorld());
		Owners.Emplace(Link, false);
		if (Link->IsBothWays())
		{
			Ends.Add(Link->GetEndWorld());
			Owners.Emplace(Link, true);
		}
	}
	const int32 Index = FindNearestEnd(Ends, Point, Radius, MaxSnapHeight);
	if (Index != INDEX_NONE)
	{
		Match.Link = Owners[Index].Key;
		Match.bReversed = Owners[Index].Value;
		Match.Distance2D = FVector::Dist2D(Ends[Index], Point);
	}
	return Match;
}

int32 UVeloxBotSubsystem::GetMasterSeed() const
{
	const int32 Forced = VeloxBotSubsystemPrivate::CVarBotSeed.GetValueOnGameThread();
	return Forced != 0 ? Forced : TimeSeed;
}

void UVeloxBotSubsystem::RegisterBot(AController* Bot)
{
	if (Bot)
	{
		Bots.AddUnique(Bot);
	}
}

void UVeloxBotSubsystem::UnregisterBot(AController* Bot)
{
	Bots.RemoveAll([Bot](const TWeakObjectPtr<AController>& Entry) { return !Entry.IsValid() || Entry.Get() == Bot; });
}

TArray<AController*> UVeloxBotSubsystem::GetBots() const
{
	TArray<AController*> Result;
	for (const TWeakObjectPtr<AController>& Entry : Bots)
	{
		if (AController* Bot = Entry.Get())
		{
			Result.Add(Bot);
		}
	}
	return Result;
}

void UVeloxBotSubsystem::RegisterProjectile(AVeloxProjectile* Projectile)
{
	if (Projectile)
	{
		Projectiles.AddUnique(Projectile);
	}
}

void UVeloxBotSubsystem::UnregisterProjectile(AVeloxProjectile* Projectile)
{
	Projectiles.RemoveAll([Projectile](const TWeakObjectPtr<AVeloxProjectile>& Entry) { return !Entry.IsValid() || Entry.Get() == Projectile; });
}

TArray<AVeloxProjectile*> UVeloxBotSubsystem::GetProjectiles() const
{
	TArray<AVeloxProjectile*> Result;
	for (const TWeakObjectPtr<AVeloxProjectile>& Entry : Projectiles)
	{
		if (AVeloxProjectile* Projectile = Entry.Get())
		{
			Result.Add(Projectile);
		}
	}
	return Result;
}
