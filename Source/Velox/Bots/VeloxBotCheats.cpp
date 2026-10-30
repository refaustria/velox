// Velox - non-shipping bot console commands (ADR 0005 section 14). Authority world (dedicated server, standalone, listen
// host). Both wait for the runtime nav build (UVeloxBotSubsystem::CallWhenNavReady), so they work from -ExecCmds.
//   velox.bots.DumpNav x y z x y z [Walk|Hop|Full]   path query (default Full); one header line and one line per path
//                                                     point, link=<type> on the start point of a link segment
//                                                     (FNavMeshNodeFlags::IsNavLink + FindLinkByStart), with the
//                                                     segment's nav area as a cross-check.
//   velox.bots.DumpPadLandings                        every AVeloxJumpPad: ComputeLandingPoint and the end of its
//                                                     JumpPad nav link.
// Task 04 (ADR 0005 sections 12, 14):
//   AddBot [Easy|Medium|Hard|Expert] [Name]           adds a bot (authority world); on a client the command is
//                                                     forwarded through AVeloxPlayerController::ServerAddBot.
//   KickBot [Name|all]                                removes a bot (default all); forwarded like AddBot on a client.
//   velox.bots.List                                   one VeloxBot line per bot.
//   velox.bots.Goal <bot|all> <item tag | point x y z | none>
//                                                     forces a goal (tests): the first pickup with the tag, a point,
//                                                     or back to the brain.
//   velox.bots.Freeze 0/1, velox.bots.Debug 0/1       cvars in VeloxBotController.cpp.
//   velox.bots.Kill <bot>                             1000 environment damage from the local player's pawn (or the
//                                                     world) on the bot: the B8 death, kill feed, auto-respawn.
//   velox.bots.Players                                the PlayerArray as this machine sees it (client or server):
//                                                     name, bot flag, colour, score - the scoreboard's data.
//   velox.bots.After <seconds> <command ...>          runs a console command after Seconds of game-world time (headless
//                                                     scripts; a map change such as a client's connect restarts the count).
// Task 07 (ADR 0005 section 14, D36):
//   velox.bots.Watch [seconds]                        client observer (nettest.sh BotRun / BotDuel): once connected logs
//                                                     VeloxBotWatch lines for match phase changes, replicated kill-feed
//                                                     entries and pickup placeholder changes (with the last taker), a
//                                                     VeloxBotWatch tick line every 10 s, and quits the process after
//                                                     Seconds of wall time (0 / omitted = no quit).

#include "AI/Navigation/NavigationTypes.h"
#include "Bots/VeloxBotController.h"
#include "Bots/VeloxBotDifficulty.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxNavFilters.h"
#include "Bots/VeloxNavLink.h"
#include "Character/VeloxCharacter.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Containers/Ticker.h"
#include "Core/VeloxGameplayTags.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/VeloxPlayerState.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameModes/VeloxGameState.h"
#include "GameModes/VeloxMatchRules.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameplayTagContainer.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Movement/VeloxJumpPad.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavMesh/NavMeshPath.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavigationSystemTypes.h"

namespace VeloxBotCommands
{
	/** Seconds a client keeps retrying to forward AddBot / KickBot while it is still connecting (-ExecCmds). */
	constexpr float ClientForwardTimeoutSeconds = 30.f;

	bool IsDifficultyArg(const FString& Arg)
	{
		static const TCHAR* const Presets[] = { TEXT("Easy"), TEXT("Medium"), TEXT("Hard"), TEXT("Expert") };
		for (const TCHAR* Preset : Presets)
		{
			if (Arg.Equals(Preset, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return Arg.StartsWith(TEXT("DA_Bot_"), ESearchCase::IgnoreCase) || Arg.StartsWith(TEXT("/"));
	}

	/** The client's local Velox player controller of the current game world (null until connected). */
	AVeloxPlayerController* FindClientController()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld() && World->GetNetMode() == NM_Client)
			{
				AVeloxPlayerController* PC = Cast<AVeloxPlayerController>(World->GetFirstPlayerController());
				if (PC && PC->IsLocalController() && PC->PlayerState)
				{
					return PC;
				}
			}
		}
		return nullptr;
	}

	/** Client: forwards through the server RPC once the local controller exists (retries on the core ticker). */
	void ForwardFromClient(bool bAdd, FString Difficulty, FString Name)
	{
		const double Deadline = FPlatformTime::Seconds() + ClientForwardTimeoutSeconds;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([bAdd, Difficulty, Name, Deadline](float)
		{
			if (AVeloxPlayerController* PC = FindClientController())
			{
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBot %s forwarded to the server via %s args=[%s %s]"), bAdd ? TEXT("AddBot") : TEXT("KickBot"),
					bAdd ? TEXT("ServerAddBot") : TEXT("ServerKickBot"), *Difficulty, *Name);
				bAdd ? PC->ServerAddBot(Difficulty, Name) : PC->ServerKickBot(Name);
				return false;
			}
			if (FPlatformTime::Seconds() > Deadline)
			{
				UE_LOG(LogVeloxBots, Warning, TEXT("VeloxBot %s: no connected local player controller to forward through"), bAdd ? TEXT("AddBot") : TEXT("KickBot"));
				return false;
			}
			return true;
		}), 0.1f);
	}

	void AddBotCommand(const TArray<FString>& Args, UWorld* World)
	{
		FString Difficulty;
		FString Name;
		int32 Next = 0;
		if (Args.IsValidIndex(0) && IsDifficultyArg(Args[0]))
		{
			Difficulty = Args[0];
			Next = 1;
		}
		if (Args.IsValidIndex(Next))
		{
			Name = Args[Next];
		}
		if (World && World->GetNetMode() == NM_Client)
		{
			ForwardFromClient(true, Difficulty, Name);
			return;
		}
		UVeloxBotSubsystem* Bots = World ? UVeloxBotSubsystem::Get(World) : nullptr;
		const UVeloxBotDifficulty* Preset = UVeloxBotSubsystem::LoadDifficulty(Difficulty);
		if (!Bots || !Preset)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("AddBot [Easy|Medium|Hard|Expert] [Name]: %s"), !Bots ? TEXT("needs the authority world") : TEXT("unknown difficulty"));
			return;
		}
		Bots->AddBot(Preset, Name);
	}

	void KickBotCommand(const TArray<FString>& Args, UWorld* World)
	{
		const FString Name = Args.IsValidIndex(0) ? Args[0] : FString(TEXT("all"));
		if (World && World->GetNetMode() == NM_Client)
		{
			ForwardFromClient(false, FString(), Name);
			return;
		}
		if (UVeloxBotSubsystem* Bots = World ? UVeloxBotSubsystem::Get(World) : nullptr)
		{
			Bots->KickBot(Name);
		}
		else
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("KickBot [Name|all]: needs the authority world (or a client to forward from)"));
		}
	}

	FAutoConsoleCommandWithWorldAndArgs AddBotCmd(TEXT("AddBot"),
		TEXT("AddBot [Easy|Medium|Hard|Expert] [Name]: adds a bot on the server (ADR 0005 section 12); a client forwards it through ServerAddBot (non-shipping)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AddBotCommand));

	FAutoConsoleCommandWithWorldAndArgs KickBotCmd(TEXT("KickBot"),
		TEXT("KickBot [Name|all]: removes a bot (default all); a client forwards it through ServerKickBot (non-shipping)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&KickBotCommand));
}

#if !UE_BUILD_SHIPPING
namespace VeloxBotCheats
{
	UVeloxBotSubsystem* AuthoritySubsystem(UWorld* World, const TCHAR* Command)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		if (!Bots)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("%s: needs the authority world (dedicated server, standalone or listen host)."), Command);
		}
		return Bots;
	}

	const TCHAR* LinkLabel(const UVeloxBotSubsystem& Bots, const ANavigationData& NavData, const FNavPathPoint& Point, FString& OutDetail)
	{
		OutDetail.Reset();
		if (!FNavMeshNodeFlags(Point.Flags).IsNavLink())
		{
			return TEXT("none");
		}
		// Area of the link segment: Recast stores "area after this node" in the point flags.
		const UClass* AreaClass = NavData.GetAreaClass(FNavMeshNodeFlags(Point.Flags).Area);
		// Nearest link end at any distance, accepted within that link's snap radius (match_cm shows the actual offset).
		const FVeloxNavLinkMatch Snapped = Bots.FindLinkByStart(Point.Location, TNumericLimits<float>::Max());
		const bool bWithinSnap = Snapped.Link && Snapped.Distance2D <= Snapped.Link->GetSnapRadius();
		EVeloxNavLinkType AreaType;
		const bool bVeloxArea = AVeloxNavLink::LinkTypeForArea(AreaClass, AreaType);
		OutDetail = FString::Printf(TEXT(" area=%s actor=%s match_cm=%.1f reversed=%d"), *GetNameSafe(AreaClass),
			bWithinSnap ? *Snapped.Link->GetName() : TEXT("none"), Snapped.Link ? Snapped.Distance2D : -1.0, bWithinSnap && Snapped.bReversed ? 1 : 0);
		if (bWithinSnap)
		{
			if (bVeloxArea && AreaType != Snapped.Link->GetLinkType())
			{
				OutDetail += FString::Printf(TEXT(" area_mismatch=%s"), AVeloxNavLink::LinkTypeName(AreaType));
			}
			return AVeloxNavLink::LinkTypeName(Snapped.Link->GetLinkType());
		}
		return bVeloxArea ? TEXT("unmatched") : TEXT("engine");
	}

	void RunDumpNav(TWeakObjectPtr<UWorld> WeakWorld, FVector From, FVector To, FString FilterName)
	{
		UWorld* World = WeakWorld.Get();
		UVeloxBotSubsystem* Bots = World ? UVeloxBotSubsystem::Get(World) : nullptr;
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
		if (!Bots || !NavSys || !NavData)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("VeloxNavDump result=none reason=no_navdata"));
			return;
		}
		const TSubclassOf<UNavigationQueryFilter> FilterClass = VeloxNavFilters::FilterClassByName(FilterName);
		FNavLocation FromNav, ToNav;
		const bool bFromOk = NavSys->ProjectPointToNavigation(From, FromNav, INVALID_NAVEXTENT, NavData);
		const bool bToOk = NavSys->ProjectPointToNavigation(To, ToNav, INVALID_NAVEXTENT, NavData);
		if (!bFromOk || !bToOk)
		{
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxNavDump from=%s to=%s filter=%s result=none reason=%s"), *From.ToCompactString(), *To.ToCompactString(), *FilterName,
				!bFromOk ? TEXT("start_off_mesh") : TEXT("end_off_mesh"));
			return;
		}

		FPathFindingQuery Query(nullptr, *NavData, FromNav.Location, ToNav.Location, UNavigationQueryFilter::GetQueryFilter(*NavData, nullptr, FilterClass));
		Query.SetAllowPartialPaths(true);
		const FPathFindingResult Result = NavSys->FindPathSync(Query);
		const FNavigationPath* Path = Result.Path.Get();
		if (!Result.IsSuccessful() || !Path || Path->GetPathPoints().Num() == 0)
		{
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxNavDump from=%s to=%s filter=%s result=none reason=no_path"), *FromNav.Location.ToCompactString(), *ToNav.Location.ToCompactString(), *FilterName);
			return;
		}

		const TArray<FNavPathPoint>& Points = Path->GetPathPoints();
		TArray<FString> LinkTypes;
		TArray<FString> Lines;
		for (int32 i = 0; i < Points.Num(); ++i)
		{
			FString Detail;
			const TCHAR* Label = i + 1 < Points.Num() ? LinkLabel(*Bots, *NavData, Points[i], Detail) : TEXT("none");
			if (FCString::Strcmp(Label, TEXT("none")) != 0)
			{
				LinkTypes.Add(Label);
			}
			Lines.Add(FString::Printf(TEXT("VeloxNavDump i=%d p=%s link=%s%s"), i, *Points[i].Location.ToCompactString(), Label, *Detail));
		}
		const double EndGap = FVector::Dist(Points.Last().Location, ToNav.Location);
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxNavDump from=%s to=%s filter=%s result=%s points=%d length=%.0f cost=%.0f end_gap=%.0f links=%s"),
			*FromNav.Location.ToCompactString(), *ToNav.Location.ToCompactString(), *FilterName, Result.IsPartial() || Path->IsPartial() ? TEXT("partial") : TEXT("ok"),
			Points.Num(), static_cast<double>(Path->GetLength()), static_cast<double>(Path->GetCost()), EndGap, LinkTypes.Num() ? *FString::Join(LinkTypes, TEXT(",")) : TEXT("none"));
		for (const FString& Line : Lines)
		{
			UE_LOG(LogVeloxBots, Log, TEXT("%s"), *Line);
		}
	}

	void DumpNavCommand(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = AuthoritySubsystem(World, TEXT("velox.bots.DumpNav"));
		if (!Bots)
		{
			return;
		}
		const FString FilterName = Args.Num() >= 7 ? Args[6] : FString(TEXT("Full"));
		if (Args.Num() < 6 || !VeloxNavFilters::FilterClassByName(FilterName))
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.DumpNav x y z x y z [Walk|Hop|Full]"));
			return;
		}
		const FVector From(FCString::Atod(*Args[0]), FCString::Atod(*Args[1]), FCString::Atod(*Args[2]));
		const FVector To(FCString::Atod(*Args[3]), FCString::Atod(*Args[4]), FCString::Atod(*Args[5]));
		TWeakObjectPtr<UWorld> WeakWorld(World);
		Bots->CallWhenNavReady([WeakWorld, From, To, FilterName]() { RunDumpNav(WeakWorld, From, To, FilterName); });
	}

	void RunDumpPadLandings(TWeakObjectPtr<UWorld> WeakWorld)
	{
		UWorld* World = WeakWorld.Get();
		UVeloxBotSubsystem* Bots = World ? UVeloxBotSubsystem::Get(World) : nullptr;
		if (!Bots)
		{
			return;
		}
		int32 Count = 0;
		for (TActorIterator<AVeloxJumpPad> It(World); It; ++It)
		{
			const AVeloxJumpPad* Pad = *It;
			FVector Landing;
			const bool bHit = Pad->TryComputeLandingPoint(World, Landing);
			const AVeloxNavLink* PadLink = nullptr;
			for (const AVeloxNavLink* Link : Bots->GetNavLinks())
			{
				if (Link->GetLinkType() == EVeloxNavLinkType::JumpPad && Link->GetJumpPad() == Pad)
				{
					PadLink = Link;
					break;
				}
			}
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxPadLanding pad=%s origin=%s landing=%s hit=%d link=%s link_end=%s dist_to_link_end=%.1f"),
				*Pad->GetName(), *Pad->GetLaunchOrigin().ToCompactString(), *Landing.ToCompactString(), bHit ? 1 : 0, *GetNameSafe(PadLink),
				PadLink ? *PadLink->GetEndWorld().ToCompactString() : TEXT("none"), PadLink ? FVector::Dist(PadLink->GetEndWorld(), Landing) : -1.0);
			++Count;
		}
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxPadLanding pads=%d"), Count);
	}

	void DumpPadLandingsCommand(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = AuthoritySubsystem(World, TEXT("velox.bots.DumpPadLandings"));
		if (!Bots)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		Bots->CallWhenNavReady([WeakWorld]() { RunDumpPadLandings(WeakWorld); });
	}

	void ListCommand(const TArray<FString>& /*Args*/, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = AuthoritySubsystem(World, TEXT("velox.bots.List"));
		if (!Bots)
		{
			return;
		}
		const TArray<AVeloxBotController*> All = Bots->GetBots();
		for (const AVeloxBotController* Bot : All)
		{
			UE_LOG(LogVeloxBots, Log, TEXT("%s"), *Bot->DescribeForDebug());
		}
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxBot list count=%d nav_ready=%d"), All.Num(), Bots->IsNavReady() ? 1 : 0);
	}

	void GoalCommand(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = AuthoritySubsystem(World, TEXT("velox.bots.Goal"));
		if (!Bots)
		{
			return;
		}
		if (Args.Num() < 2)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.Goal <bot|all> <item tag | point x y z | none>"));
			return;
		}
		TArray<AVeloxBotController*> Targets;
		if (Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			Targets = Bots->GetBots();
		}
		else if (AVeloxBotController* Bot = Bots->FindBot(Args[0]))
		{
			Targets.Add(Bot);
		}
		if (Targets.IsEmpty())
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.Goal: no bot named %s"), *Args[0]);
			return;
		}
		if (Args[1].Equals(TEXT("none"), ESearchCase::IgnoreCase))
		{
			for (AVeloxBotController* Bot : Targets)
			{
				Bot->ClearForcedGoal();
			}
			return;
		}
		if (Args[1].Equals(TEXT("point"), ESearchCase::IgnoreCase))
		{
			if (Args.Num() < 5)
			{
				UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.Goal <bot|all> point x y z"));
				return;
			}
			const FVector Point(FCString::Atod(*Args[2]), FCString::Atod(*Args[3]), FCString::Atod(*Args[4]));
			for (AVeloxBotController* Bot : Targets)
			{
				Bot->SetForcedPointGoal(Point);
			}
			return;
		}
		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Args[1]), false);
		const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		const TArray<AVeloxPickup*> Pickups = Tag.IsValid() && Registry ? Registry->FindByTag(Tag) : TArray<AVeloxPickup*>();
		if (Pickups.IsEmpty())
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.Goal: no pickup with tag %s"), *Args[1]);
			return;
		}
		for (AVeloxBotController* Bot : Targets)
		{
			Bot->SetForcedItemGoal(Pickups[0]);
		}
	}

	void KillCommand(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = AuthoritySubsystem(World, TEXT("velox.bots.Kill"));
		AVeloxBotController* Bot = Bots && Args.IsValidIndex(0) ? Bots->FindBot(Args[0]) : nullptr;
		AVeloxCharacter* Victim = Bot ? Cast<AVeloxCharacter>(Bot->GetPawn()) : nullptr;
		UVeloxAbilitySystemComponent* VictimASC = Victim ? Victim->GetVeloxAbilitySystemComponent() : nullptr;
		if (!VictimASC)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.Kill <bot>: no living bot named %s"), Args.IsValidIndex(0) ? *Args[0] : TEXT("?"));
			return;
		}
		// The dealer is the local player's pawn when there is one (a frag in the kill feed), else the bot itself (a suicide).
		const APlayerController* PC = World->GetFirstPlayerController();
		AVeloxCharacter* Dealer = PC && PC->IsLocalController() ? Cast<AVeloxCharacter>(PC->GetPawn()) : nullptr;
		UVeloxAbilitySystemComponent* DealerASC = Dealer ? Dealer->GetVeloxAbilitySystemComponent() : VictimASC;
		const AVeloxPlayerState* VictimPS = Bot->GetPlayerState<AVeloxPlayerState>();
		FVeloxDamageSpecParams Params;
		Params.Damage = UVeloxCombatSettings::GetOrDefault(VictimPS ? VictimPS->GetCombatSettings() : nullptr).EnvironmentKillDamage;
		Params.DamageTag = VeloxGameplayTags::Damage_Environment;
		Params.HitLocation = Victim->GetActorLocation();
		Params.bNoKnockback = true;
		Params.Causer = Dealer ? static_cast<AActor*>(Dealer) : static_cast<AActor*>(Victim);
		UE_LOG(LogVeloxBots, Log, TEXT("velox.bots.Kill: %s kills %s (environment damage %d)"), Dealer ? *Dealer->GetName() : TEXT("world"), *Bot->GetBotName(), Params.Damage);
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeDamageSpec(DealerASC, Params);
		if (Spec.IsValid())
		{
			DealerASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), VictimASC);
		}
	}

	void PlayersCommand(const TArray<FString>& /*Args*/, UWorld* World)
	{
		const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
		if (!GameState)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.Players: no game state"));
			return;
		}
		int32 NumBots = 0;
		for (const APlayerState* PS : GameState->PlayerArray)
		{
			const AVeloxPlayerState* VeloxPS = Cast<AVeloxPlayerState>(PS);
			NumBots += PS && PS->IsABot() ? 1 : 0;
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxPlayers net=%s name=%s bot=%d color=%d score=%.0f kills=%d deaths=%d spectator=%d"), World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"),
				PS ? *PS->GetPlayerName() : TEXT("-"), PS && PS->IsABot() ? 1 : 0, VeloxPS && VeloxPS->HasPlayerColor() ? static_cast<int32>(VeloxPS->GetPlayerColorIndex()) : -1,
				PS ? PS->GetScore() : 0.f, VeloxPS ? VeloxPS->GetKills() : 0, VeloxPS ? VeloxPS->GetDeaths() : 0, PS && PS->IsOnlyASpectator() ? 1 : 0);
		}
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxPlayers net=%s count=%d bots=%d"), World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"), GameState->PlayerArray.Num(), NumBots);
	}

	/** The game world a delayed command runs in: the first game world with a game state (a client after its travel). */
	UWorld* CurrentGameWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld() && World->GetGameState())
			{
				return World;
			}
		}
		return nullptr;
	}

	void AfterCommand(const TArray<FString>& Args, UWorld* /*World*/)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.After <seconds> <command ...>"));
			return;
		}
		const double Seconds = FMath::Max(0.0, FCString::Atod(*Args[0]));
		TArray<FString> Rest(Args.GetData() + 1, Args.Num() - 1);
		const FString Command = FString::Join(Rest, TEXT(" "));
		// World time of the current game world (fixed-step runs are not real time); a map change (a client's connect
		// travel) restarts the count in the new world, so client -ExecCmds can be scripted too.
		TWeakObjectPtr<UWorld> CountingWorld;
		double StartTime = 0.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Seconds, Command, CountingWorld, StartTime](float) mutable
		{
			UWorld* World = CurrentGameWorld();
			if (!World)
			{
				return true;
			}
			if (CountingWorld.Get() != World)
			{
				CountingWorld = World;
				StartTime = World->GetTimeSeconds();
			}
			if (World->GetTimeSeconds() - StartTime + KINDA_SMALL_NUMBER < Seconds)
			{
				return true;
			}
			UE_LOG(LogVeloxBots, Log, TEXT("velox.bots.After: %s"), *Command);
			GEngine->Exec(World, *Command);
			return false;
		}));
	}

	// ---------------------------------------------------------------------------------------- Watch (task 07)

	/** State of one velox.bots.Watch run; shared by the ticker and the kill-feed delegate. */
	struct FWatchState
	{
		double Seconds = 0.0;
		double Started = -1.0;
		double NextTick = 0.0;
		bool bHavePhase = false;
		bool bFeedBound = false;
		EVeloxMatchPhase Phase = EVeloxMatchPhase::Warmup;
		int32 FeedEntries = 0;
		int32 PickupTakes = 0;
		TMap<TWeakObjectPtr<AVeloxPickup>, bool> Visible;
	};

	/** The client game world with a game state (null until connected). */
	UWorld* FindClientWorldWithState()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (World && World->IsGameWorld() && World->GetNetMode() == NM_Client && World->GetGameState())
			{
				return World;
			}
		}
		return nullptr;
	}

	void WatchCommand(const TArray<FString>& Args, UWorld* /*World*/)
	{
		TSharedRef<FWatchState> State = MakeShared<FWatchState>();
		State->Seconds = Args.IsValidIndex(0) ? FMath::Max(0.0, FCString::Atod(*Args[0])) : 0.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float) -> bool
		{
			UWorld* World = FindClientWorldWithState();
			AVeloxGameState* GameState = World ? World->GetGameState<AVeloxGameState>() : nullptr;
			if (!World || !GameState)
			{
				return true;
			}
			const double Now = FPlatformTime::Seconds();
			if (State->Started < 0.0)
			{
				State->Started = Now;
				State->NextTick = Now;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotWatch started seconds=%.0f"), State->Seconds);
			}
			if (!State->bFeedBound)
			{
				State->bFeedBound = true;
				// Replicated entries, including the ones a late joiner receives on connect.
				GameState->OnKillFeedEntry.AddLambda([State](const FVeloxKillFeedEntry& Entry)
				{
					++State->FeedEntries;
					UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotWatch feed seq=%u killer=%s victim=%s tag=%s suicide=%d"), static_cast<uint32>(Entry.Seq), *Entry.KillerName,
						*Entry.VictimName, *Entry.DamageTag.ToString(), Entry.bSuicide ? 1 : 0);
				});
			}
			if (!State->bHavePhase || State->Phase != GameState->GetPhase())
			{
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotWatch phase=%s old=%s"), LexToString(GameState->GetPhase()), State->bHavePhase ? LexToString(State->Phase) : TEXT("-"));
				State->Phase = GameState->GetPhase();
				State->bHavePhase = true;
			}
			if (const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World))
			{
				for (AVeloxPickup* Pickup : Registry->GetPickups())
				{
					const bool bVisible = Pickup->IsPlaceholderVisible();
					const bool* Known = State->Visible.Find(Pickup);
					if (Known && *Known && !bVisible)
					{
						++State->PickupTakes;
						UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotWatch pickup item=%s taken_by=%s"), *GetNameSafe(Pickup->GetDefinition()),
							*AVeloxPlayerState::VeloxPlayerLabel(Pickup->GetLastTakenBy()));
					}
					State->Visible.Add(Pickup, bVisible);
				}
			}
			if (Now >= State->NextTick)
			{
				State->NextTick = Now + 10.0;
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotWatch t=%.0f phase=%s players=%d feed=%d pickups_taken=%d"), Now - State->Started, LexToString(State->Phase),
					GameState->PlayerArray.Num(), State->FeedEntries, State->PickupTakes);
			}
			if (State->Seconds > 0.0 && Now - State->Started >= State->Seconds)
			{
				UE_LOG(LogVeloxBots, Log, TEXT("VeloxBotWatch done seconds=%.0f phase=%s feed=%d pickups_taken=%d"), Now - State->Started, LexToString(State->Phase), State->FeedEntries,
					State->PickupTakes);
				FPlatformMisc::RequestExitWithStatus(false, 0);
				return false;
			}
			return true;
		}), 0.25f);
	}

	FAutoConsoleCommandWithWorldAndArgs WatchCmd(TEXT("velox.bots.Watch"),
		TEXT("velox.bots.Watch [seconds]: client observer; logs VeloxBotWatch phase / feed / pickup lines once connected and quits after Seconds of wall time (soak, BotDuel spectator). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WatchCommand));

	FAutoConsoleCommandWithWorldAndArgs KillCmd(TEXT("velox.bots.Kill"), TEXT("velox.bots.Kill <bot>: environment-damage kill of a bot by the local player (authority world). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&KillCommand));

	FAutoConsoleCommandWithWorldAndArgs PlayersCmd(TEXT("velox.bots.Players"), TEXT("velox.bots.Players: logs the PlayerArray (name, bot flag, colour, score) as this machine sees it. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PlayersCommand));

	FAutoConsoleCommandWithWorldAndArgs AfterCmd(TEXT("velox.bots.After"), TEXT("velox.bots.After <seconds> <command ...>: runs the command after Seconds of game-world time (headless scripts; restarts on a map change). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AfterCommand));

	FAutoConsoleCommandWithWorldAndArgs ListCmd(TEXT("velox.bots.List"), TEXT("velox.bots.List: one VeloxBot line per bot (authority world). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ListCommand));

	FAutoConsoleCommandWithWorldAndArgs GoalCmd(TEXT("velox.bots.Goal"),
		TEXT("velox.bots.Goal <bot|all> <item tag | point x y z | none>: forces a bot goal (tests, debugging). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GoalCommand));

	FAutoConsoleCommandWithWorldAndArgs DumpNavCmd(
		TEXT("velox.bots.DumpNav"),
		TEXT("velox.bots.DumpNav x y z x y z [Walk|Hop|Full]: logs the nav path points with link=<type> on link segments (waits for the nav build). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpNavCommand));

	FAutoConsoleCommandWithWorldAndArgs DumpPadLandingsCmd(
		TEXT("velox.bots.DumpPadLandings"),
		TEXT("velox.bots.DumpPadLandings: logs AVeloxJumpPad::ComputeLandingPoint and the pad's nav link end for every pad. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DumpPadLandingsCommand));
}
#endif // !UE_BUILD_SHIPPING
