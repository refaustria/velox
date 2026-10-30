// Velox - server-side bot world state (ADR 0005 sections 3, 4, 12).

#include "Bots/VeloxBotSubsystem.h"

#include "Bots/VeloxBotCombat.h"
#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotDifficulty.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSettings.h"
#include "Bots/VeloxNavLink.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/VeloxGameMode.h"
#include "GameModes/VeloxGameModeDefinition.h"
#include "GameModes/VeloxPlayerState.h"
#include "HAL/IConsoleManager.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "Items/VeloxPickup.h"
#include "Movement/VeloxJumpPad.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Weapons/VeloxProjectile.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxBotSubsystem)

DEFINE_STAT(STAT_VeloxBots_Think);
DEFINE_STAT(STAT_VeloxBots_Perceive);
DEFINE_STAT(STAT_VeloxBots_Path);
DEFINE_STAT(STAT_VeloxBots_Steer);
DEFINE_STAT(STAT_VeloxBots_Apply);

namespace VeloxBotSubsystemPrivate
{
	/** Cadence of the nav-ready poll until the initial build finished (the generation delegate also triggers a poll). */
	constexpr float NavPollSeconds = 0.1f;
	/** One warning if the initial build has not finished after this long (ADR 0005 section 13: <= 1 s expected). */
	constexpr double NavSlowWarnSeconds = 30.0;
	/** Content folder of the DA_Bot_* presets (Tools/editor/create_bot_content.py). */
	const TCHAR* const BotContentDir = TEXT("/Game/Bots");

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
		World->GetTimerManager().ClearTimer(BudgetHandle);
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

	// Section 13: the budget line (only logged while bots exist).
	BudgetWindowStart = InWorld.GetTimeSeconds();
	BudgetFrameStart = GFrameCounter;
	InWorld.GetTimerManager().SetTimer(BudgetHandle, this, &ThisClass::LogBudget, UVeloxBotCombatSettings::Get().BudgetLogIntervalSeconds, true);

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

void UVeloxBotSubsystem::RegisterBot(AVeloxBotController* Bot)
{
	if (!Bot)
	{
		return;
	}
	Bots.RemoveAll([](const TWeakObjectPtr<AVeloxBotController>& Entry) { return !Entry.IsValid(); });
	if (Bots.Contains(Bot))
	{
		return;
	}
	if (Bot->GetBotIndex() == INDEX_NONE)
	{
		// Lowest free index (a kicked bot's index is reused): stagger slots stay dense.
		int32 Index = 0;
		while (Bots.ContainsByPredicate([Index](const TWeakObjectPtr<AVeloxBotController>& Entry) { return Entry->GetBotIndex() == Index; }))
		{
			++Index;
		}
		Bot->SetBotIndex(Index);
	}
	Bots.Add(Bot);
}

void UVeloxBotSubsystem::UnregisterBot(AVeloxBotController* Bot)
{
	Bots.RemoveAll([Bot](const TWeakObjectPtr<AVeloxBotController>& Entry) { return !Entry.IsValid() || Entry.Get() == Bot; });
}

TArray<AVeloxBotController*> UVeloxBotSubsystem::GetBots() const
{
	TArray<AVeloxBotController*> Result;
	for (const TWeakObjectPtr<AVeloxBotController>& Entry : Bots)
	{
		if (AVeloxBotController* Bot = Entry.Get(); Bot && !Bot->IsActorBeingDestroyed())
		{
			Result.Add(Bot);
		}
	}
	return Result;
}

AVeloxBotController* UVeloxBotSubsystem::FindBot(const FString& Name) const
{
	for (AVeloxBotController* Bot : GetBots())
	{
		if (Bot->GetBotName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Bot;
		}
	}
	return nullptr;
}

const UVeloxBotDifficulty* UVeloxBotSubsystem::LoadDifficulty(const FString& NameOrPath)
{
	if (NameOrPath.IsEmpty())
	{
		return UVeloxBotSettings::Get().DefaultDifficulty.LoadSynchronous();
	}
	FString Path = NameOrPath;
	if (!Path.StartsWith(TEXT("/")))
	{
		// Easy -> /Game/Bots/DA_Bot_Easy.DA_Bot_Easy (Tools/editor/create_bot_content.py); DA_Bot_Hard works too.
		const FString AssetName = Path.StartsWith(TEXT("DA_Bot_"), ESearchCase::IgnoreCase) ? Path : FString::Printf(TEXT("DA_Bot_%s"), *Path);
		Path = FString::Printf(TEXT("%s/%s.%s"), VeloxBotSubsystemPrivate::BotContentDir, *AssetName, *AssetName);
	}
	return TSoftObjectPtr<UVeloxBotDifficulty>(FSoftObjectPath(Path)).LoadSynchronous();
}

AVeloxBotController* UVeloxBotSubsystem::AddBot(const UVeloxBotDifficulty* Difficulty, const FString& Name)
{
	UWorld* World = GetWorld();
	AVeloxGameMode* GameMode = World ? World->GetAuthGameMode<AVeloxGameMode>() : nullptr;
	if (!GameMode)
	{
		UE_LOG(LogVeloxBots, Warning, TEXT("VeloxBot add refused reason=no_game_mode"));
		return nullptr;
	}
	const UVeloxGameModeDefinition* Definition = GameMode->GetGameModeDefinition();
	const int32 Active = GameMode->GetNumActivePlayers();
	if (Definition && Active >= Definition->MaxActivePlayers)
	{
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBot add refused reason=full active=%d max_active=%d"), Active, Definition->MaxActivePlayers);
		return nullptr;
	}
	if (!Difficulty)
	{
		Difficulty = LoadDifficulty(FString());
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVeloxBotController* Bot = World->SpawnActor<AVeloxBotController>(AVeloxBotController::StaticClass(), FTransform::Identity, SpawnParams);
	AVeloxPlayerState* PS = Bot ? Bot->GetPlayerState<AVeloxPlayerState>() : nullptr;
	if (!PS)
	{
		UE_LOG(LogVeloxBots, Warning, TEXT("VeloxBot add refused reason=no_player_state controller=%s"), *GetNameSafe(Bot));
		if (Bot)
		{
			Bot->Destroy();
		}
		return nullptr;
	}
	Bot->SetDifficulty(Difficulty);
	RegisterBot(Bot);

	TArray<FString> Taken;
	if (const AGameStateBase* GameState = World->GetGameState())
	{
		for (const APlayerState* Other : GameState->PlayerArray)
		{
			if (Other && Other != PS)
			{
				Taken.Add(Other->GetPlayerName());
			}
		}
	}
	PS->SetPlayerName(VeloxBotRules::NextBotName(UVeloxBotSettings::Get().BotNames, Taken, Name));
	PS->SetIsABot(true);
	GameMode->AssignPlayerColor(*PS);
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxBot add name=%s difficulty=%s index=%d color=%d active=%d"), *PS->GetPlayerName(), *GetNameSafe(Difficulty), Bot->GetBotIndex(),
		PS->HasPlayerColor() ? static_cast<int32>(PS->GetPlayerColorIndex()) : -1, Active + 1);
	GameMode->StartBot(Bot);
	return Bot;
}

int32 UVeloxBotSubsystem::KickBot(const FString& NameOrAll)
{
	const bool bAll = NameOrAll.Equals(TEXT("all"), ESearchCase::IgnoreCase);
	TArray<AVeloxBotController*> Victims;
	for (AVeloxBotController* Bot : GetBots())
	{
		if (bAll || Bot->GetBotName().Equals(NameOrAll, ESearchCase::IgnoreCase))
		{
			Victims.Add(Bot);
		}
	}
	for (AVeloxBotController* Bot : Victims)
	{
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBot kick name=%s index=%d"), *Bot->GetBotName(), Bot->GetBotIndex());
		APawn* Pawn = Bot->GetPawn();
		Bot->UnPossess();
		if (Pawn)
		{
			Pawn->Destroy();
		}
		// AController::Destroyed -> AGameModeBase::Logout -> the match rules re-evaluate (section 12).
		UnregisterBot(Bot);
		Bot->Destroy();
	}
	if (Victims.IsEmpty())
	{
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBot kick name=%s result=none"), *NameOrAll);
	}
	return Victims.Num();
}

void UVeloxBotSubsystem::RegisterProjectile(AVeloxProjectile* Projectile)
{
	if (Projectile)
	{
		Projectiles.RemoveAll([](const TWeakObjectPtr<AVeloxProjectile>& Entry) { return !Entry.IsValid(); });
		Projectiles.AddUnique(Projectile);
		Projectile->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleProjectileEndPlay);
	}
}

void UVeloxBotSubsystem::HandleProjectileEndPlay(AActor* Actor, EEndPlayReason::Type /*EndPlayReason*/)
{
	UnregisterProjectile(Cast<AVeloxProjectile>(Actor));
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
		if (AVeloxProjectile* Projectile = Entry.Get(); Projectile && !Projectile->HasExploded() && !Projectile->IsActorBeingDestroyed())
		{
			Result.Add(Projectile);
		}
	}
	return Result;
}

void UVeloxBotSubsystem::RecordBotTick(const AVeloxBotController& Bot, double Micros, int32 PathQueries, bool bFireTick)
{
	BudgetMicros += Micros;
	++BudgetTicks;
	TotalMicros += Micros;
	++TotalTicks;
	// ADR 0005 D46(a): ticks of any kind above 10 ms are stalls of the thread, not work (reported as stalls=).
	if (Micros > 10000.0)
	{
		++BudgetStalls;
	}
	if (PathQueries > 0)
	{
		// D10: the peak target excludes path-query ticks; their cost is reported separately (max_path_us, paths=/s).
		BudgetPaths += PathQueries;
		BudgetMaxPathMicros = FMath::Max(BudgetMaxPathMicros, Micros);
		return;
	}
	if (bFireTick)
	{
		// The fire ability's activation (TraceShot / projectile spawn, GE, per-shot logs) is the player's server path.
		BudgetMaxFireMicros = FMath::Max(BudgetMaxFireMicros, Micros);
		return;
	}
	BudgetMaxMicros = FMath::Max(BudgetMaxMicros, Micros);
	TotalMaxMicros = FMath::Max(TotalMaxMicros, Micros);
	const float Limit = UVeloxBotSettings::Get().BudgetWarnMicroseconds;
	if (Limit > 0.f && Micros > Limit)
	{
		++BudgetOver;
		if (!bBudgetWarned)
		{
			bBudgetWarned = true;
			UE_LOG(LogVeloxBots, Warning, TEXT("VeloxBotBudget warn bot=%s us=%.0f limit=%.0f (a tick without a path query; further ones in this interval are counted in over=)"), *Bot.GetBotName(), Micros, Limit);
		}
	}
}

void UVeloxBotSubsystem::LogBudget()
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const double Window = FMath::Max(Now - BudgetWindowStart, UE_KINDA_SMALL_NUMBER);
	const int32 NumLive = NumBots();
	if (NumLive > 0 && BudgetTicks > 0)
	{
		// D15: hearing reactions and projectile dodges of the living bots' current lives (report-only in the soak).
		int32 Heard = 0;
		int32 Dodges = 0;
		for (const AVeloxBotController* Bot : GetBots())
		{
			Heard += Bot->GetCombatState().HeardCount;
			Dodges += Bot->GetCombatState().DodgeCount;
		}
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotBudget bots=%d mean_us=%.1f max_us=%.1f max_path_us=%.1f max_fire_us=%.1f paths=%.1f/s traces=%.1f/s navrays=%.1f/s ticks=%lld over=%d stalls=%d frames=%llu window=%.1f s run_mean_us=%.1f run_max_us=%.1f heard=%d dodges=%d"), NumLive,
			BudgetMicros / BudgetTicks, BudgetMaxMicros, BudgetMaxPathMicros, BudgetMaxFireMicros, BudgetPaths / Window, BudgetTraces / Window, BudgetNavRaycasts / Window, BudgetTicks, BudgetOver, BudgetStalls, static_cast<uint64>(GFrameCounter - BudgetFrameStart), Window, GetBudgetMeanMicros(),
			TotalMaxMicros, Heard, Dodges);
	}
	BudgetWindowStart = Now;
	BudgetMicros = 0.0;
	BudgetMaxMicros = 0.0;
	BudgetMaxPathMicros = 0.0;
	BudgetMaxFireMicros = 0.0;
	BudgetTicks = 0;
	BudgetPaths = 0;
	BudgetTraces = 0;
	BudgetNavRaycasts = 0;
	BudgetOver = 0;
	BudgetStalls = 0;
	BudgetFrameStart = GFrameCounter;
	bBudgetWarned = false;
}
