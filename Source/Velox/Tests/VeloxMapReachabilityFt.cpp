// Velox - FT_Map_Reachability (ADR 0006 section 7, task M5-05; map-generic since ADR 0008 section 1.6, task M7 P5).
//
//   velox.bots.FtMapReachability [WalkExempt=<item ids>] [quit]
//                                  console FT on any map (`UnrealEditor-Cmd Velox.uproject <Map> -game -nullrhi
//                                  -UseFixedTimeStep -FPS=60 -ExecCmds="velox.bots.Seed 1, velox.bots.FtMapReachability quit"`):
//                                  once the runtime nav is built, a synchronous FindPath with the Full filter must succeed
//                                  (complete path, end within PathEndToleranceCm of the goal) from every PlayerStart to every
//                                  pickup, and between every ordered pair of major items (Mega, Red / Yellow Armor, Quad, ... and
//                                  the Railgun). Every nav link start and end must project onto the nav mesh with
//                                  UVeloxBotItemRouter::GetOnMeshExtent(), and every jump-pad link ends within PadLinkToleranceCm of
//                                  AVeloxJumpPad::ComputeLandingPoint. When WalkExempt=<ids> is given (comma separated item tag,
//                                  tag suffix or definition name, e.g. Quad,Railgun), the Walk filter must also reach every pickup
//                                  NOT listed from every PlayerStart. The DM1 maps (name starts with L_DM1) keep their ADR item
//                                  set: exactly 8 PlayerStarts and >= 30 pickups; other maps need >= 2 / >= 8.
//                                  Logs `VeloxReachabilityFt check=... ok=` lines and `VeloxReachabilityFt test=FT_Map_Reachability
//                                  result=PASS|FAIL`; quit exits 0 / 1.
//   velox.bots.FtDm1Reachability   the old name, kept as an alias (same code, same lines).

#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotItemRouting.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxNavFilters.h"
#include "Bots/VeloxNavLink.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "Movement/VeloxJumpPad.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavMesh/NavMeshPath.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

#if !UE_BUILD_SHIPPING
namespace VeloxMapFt
{
	constexpr TCHAR TestName[] = TEXT("FT_Map_Reachability");
	/** The path's last point must be this close to the goal (a partial path ends short). */
	constexpr double PathEndToleranceCm = 150.0;
	/** A pad link's end must be this close to ComputeLandingPoint (ADR 0008 1.6). */
	constexpr double PadLinkToleranceCm = 60.0;
	/** DM1 (ADR 0006): exactly 8 PlayerStarts, >= 30 pickups. Other maps: the generic minimums. */
	constexpr int32 Dm1PlayerStarts = 8;
	constexpr int32 Dm1MinPickups = 30;
	constexpr int32 MinPlayerStartsOther = 2;
	constexpr int32 MinPickupsOther = 8;

	/** True when WalkExempt lists this definition: exact item tag, a ".Suffix" of it, or the definition's object name. */
	bool IsWalkExempt(const TArray<FString>& Exempt, const UVeloxItemDefinition* Def)
	{
		if (!Def)
		{
			return false;
		}
		const FString Tag = Def->ItemTag.ToString();
		for (const FString& Id : Exempt)
		{
			if (Tag.Equals(Id, ESearchCase::IgnoreCase) || Tag.EndsWith(TEXT(".") + Id, ESearchCase::IgnoreCase) || GetNameSafe(Def).Equals(Id, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	struct FRun
	{
		int32 Checks = 0;
		int32 Failed = 0;
		bool bPassed = true;

		void Check(bool bOk, const FString& What, const FString& Detail = FString())
		{
			++Checks;
			if (!bOk)
			{
				++Failed;
				bPassed = false;
			}
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxReachabilityFt check=%s ok=%d %s"), *What, bOk ? 1 : 0, *Detail);
		}
	};

	bool PathExists(UNavigationSystemV1& NavSys, ANavigationData& NavData, const TSubclassOf<UNavigationQueryFilter>& Filter, const FVector& From, const FVector& To, double& OutLength, FString& OutWhy)
	{
		FNavLocation FromNav, ToNav;
		if (!NavSys.ProjectPointToNavigation(From, FromNav, INVALID_NAVEXTENT, &NavData))
		{
			OutWhy = TEXT("start_off_mesh");
			return false;
		}
		if (!NavSys.ProjectPointToNavigation(To, ToNav, INVALID_NAVEXTENT, &NavData))
		{
			OutWhy = TEXT("end_off_mesh");
			return false;
		}
		FPathFindingQuery Query(nullptr, NavData, FromNav.Location, ToNav.Location, UNavigationQueryFilter::GetQueryFilter(NavData, nullptr, Filter));
		Query.SetAllowPartialPaths(true);
		const FPathFindingResult Result = NavSys.FindPathSync(Query);
		const FNavigationPath* Path = Result.Path.Get();
		if (!Result.IsSuccessful() || !Path || Path->GetPathPoints().Num() == 0)
		{
			OutWhy = TEXT("no_path");
			return false;
		}
		OutLength = Path->GetLength();
		if (Result.IsPartial() || Path->IsPartial())
		{
			OutWhy = TEXT("partial");
			return false;
		}
		const double EndGap = FVector::Dist(Path->GetPathPoints().Last().Location, ToNav.Location);
		if (EndGap > PathEndToleranceCm)
		{
			OutWhy = FString::Printf(TEXT("end_gap=%.0f"), EndGap);
			return false;
		}
		return true;
	}

	void Run(TWeakObjectPtr<UWorld> WeakWorld, bool bQuit, TArray<FString> WalkExempt, bool bWalkCheck)
	{
		UWorld* World = WeakWorld.Get();
		UVeloxBotSubsystem* Bots = World ? UVeloxBotSubsystem::Get(World) : nullptr;
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		ANavigationData* NavData = Bots ? Bots->GetNavData() : nullptr;
		const UVeloxItemRegistrySubsystem* Registry = UVeloxItemRegistrySubsystem::Get(World);
		FRun Run;
		if (!NavSys || !NavData || !Registry)
		{
			Run.Check(false, TEXT("nav_available"));
		}
		else
		{
			const TSubclassOf<UNavigationQueryFilter> Filter = VeloxNavFilters::FilterClassByName(TEXT("Full"));
			TArray<FString> StartNames;
			TArray<FVector> StartLocations;
			for (TActorIterator<APlayerStart> It(World); It; ++It)
			{
				StartNames.Add(It->GetName());
				StartLocations.Add(It->GetActorLocation());
			}
			const TArray<AVeloxPickup*> Pickups = Registry->GetPickups();
			const bool bDm1 = World->GetMapName().StartsWith(TEXT("L_DM1"));
			Run.Check(bDm1 ? StartLocations.Num() == Dm1PlayerStarts : StartLocations.Num() >= MinPlayerStartsOther, TEXT("playerstarts"),
				FString::Printf(TEXT("map=%s n=%d %s=%d"), *World->GetMapName(), StartLocations.Num(), bDm1 ? TEXT("expected") : TEXT("min"), bDm1 ? Dm1PlayerStarts : MinPlayerStartsOther));
			const int32 MinPickups = bDm1 ? Dm1MinPickups : MinPickupsOther;
			Run.Check(Pickups.Num() >= MinPickups, TEXT("pickups"), FString::Printf(TEXT("n=%d min=%d"), Pickups.Num(), MinPickups));

			int32 PathChecks = 0;
			for (int32 s = 0; s < StartLocations.Num(); ++s)
			{
				for (const AVeloxPickup* Pickup : Pickups)
				{
					double Length = 0.0;
					FString Why;
					const bool bOk = PathExists(*NavSys, *NavData, Filter, StartLocations[s], Pickup->GetActorLocation(), Length, Why);
					++PathChecks;
					if (!bOk)
					{
						Run.Check(false, TEXT("start_to_pickup"), FString::Printf(TEXT("from=%s to=%s at=%s why=%s"), *StartNames[s], *GetNameSafe(Pickup->GetDefinition()), *Pickup->GetActorLocation().ToCompactString(), *Why));
					}
				}
			}
			Run.Check(Run.Failed == 0, TEXT("start_to_pickup_all"), FString::Printf(TEXT("pairs=%d failed=%d"), PathChecks, Run.Failed));

			// Major items (and the Railgun on E's top slab) between each other, both directions.
			TArray<const AVeloxPickup*> Majors;
			for (const AVeloxPickup* Pickup : Pickups)
			{
				const UVeloxItemDefinition* Def = Pickup->GetDefinition();
				if (Def && (Def->bMajorItem || (Def->Type == EVeloxItemType::Weapon && Def->ItemTag.ToString().EndsWith(TEXT("Railgun")))))
				{
					Majors.Add(Pickup);
				}
			}
			Run.Check(Majors.Num() >= 5, TEXT("major_items"), FString::Printf(TEXT("n=%d"), Majors.Num()));
			const int32 FailedBefore = Run.Failed;
			int32 MajorPairs = 0;
			for (const AVeloxPickup* From : Majors)
			{
				for (const AVeloxPickup* To : Majors)
				{
					if (From == To)
					{
						continue;
					}
					double Length = 0.0;
					FString Why;
					const bool bOk = PathExists(*NavSys, *NavData, Filter, From->GetActorLocation(), To->GetActorLocation(), Length, Why);
					++MajorPairs;
					if (!bOk)
					{
						Run.Check(false, TEXT("major_to_major"), FString::Printf(TEXT("from=%s to=%s why=%s"), *GetNameSafe(From->GetDefinition()), *GetNameSafe(To->GetDefinition()), *Why));
					}
				}
			}
			Run.Check(Run.Failed == FailedBefore, TEXT("major_to_major_all"), FString::Printf(TEXT("pairs=%d failed=%d"), MajorPairs, Run.Failed - FailedBefore));

			// Nav links: every start and end projects onto the mesh (the bots' on-mesh extent); a pad link ends at its landing point.
			const FVector OnMeshExtent = UVeloxBotItemRouter::GetOnMeshExtent();
			const int32 LinkFailedBefore = Run.Failed;
			int32 LinkCount = 0;
			int32 PadLinks = 0;
			for (const AVeloxNavLink* Link : Bots->GetNavLinks())
			{
				if (!Link)
				{
					continue;
				}
				++LinkCount;
				const FVector LinkStart = Link->GetStartWorld();
				const FVector LinkEnd = Link->GetEndWorld();
				FNavLocation Projected;
				if (!NavSys->ProjectPointToNavigation(LinkStart, Projected, OnMeshExtent, NavData))
				{
					Run.Check(false, TEXT("link_start_on_mesh"), FString::Printf(TEXT("link=%s at=%s"), *Link->GetName(), *LinkStart.ToCompactString()));
				}
				if (!NavSys->ProjectPointToNavigation(LinkEnd, Projected, OnMeshExtent, NavData))
				{
					Run.Check(false, TEXT("link_end_on_mesh"), FString::Printf(TEXT("link=%s at=%s"), *Link->GetName(), *LinkEnd.ToCompactString()));
				}
				if (Link->GetLinkType() == EVeloxNavLinkType::JumpPad)
				{
					++PadLinks;
					const AVeloxJumpPad* Pad = Link->GetJumpPad();
					if (!Pad)
					{
						Run.Check(false, TEXT("pad_link_pad"), FString::Printf(TEXT("link=%s has no pad"), *Link->GetName()));
					}
					else
					{
						FVector Landing = FVector::ZeroVector;
						Pad->TryComputeLandingPoint(World, Landing);
						const double Dist = FVector::Dist(LinkEnd, Landing);
						if (Dist > PadLinkToleranceCm)
						{
							Run.Check(false, TEXT("pad_link_landing"), FString::Printf(TEXT("link=%s pad=%s landing=%s link_end=%s dist=%.1f max=%.0f"),
								*Link->GetName(), *Pad->GetName(), *Landing.ToCompactString(), *LinkEnd.ToCompactString(), Dist, PadLinkToleranceCm));
						}
					}
				}
			}
			Run.Check(Run.Failed == LinkFailedBefore, TEXT("links_all"), FString::Printf(TEXT("links=%d pad_links=%d failed=%d"), LinkCount, PadLinks, Run.Failed - LinkFailedBefore));

			// WalkExempt given: the Walk filter (no links, no hops) reaches every pickup that is not listed.
			if (bWalkCheck)
			{
				const TSubclassOf<UNavigationQueryFilter> WalkFilter = VeloxNavFilters::FilterClassByName(TEXT("Walk"));
				const int32 WalkFailedBefore = Run.Failed;
				int32 WalkPairs = 0;
				int32 Exempted = 0;
				for (const AVeloxPickup* Pickup : Pickups)
				{
					if (IsWalkExempt(WalkExempt, Pickup->GetDefinition()))
					{
						++Exempted;
						continue;
					}
					for (int32 s = 0; s < StartLocations.Num(); ++s)
					{
						double Length = 0.0;
						FString Why;
						++WalkPairs;
						if (!PathExists(*NavSys, *NavData, WalkFilter, StartLocations[s], Pickup->GetActorLocation(), Length, Why))
						{
							Run.Check(false, TEXT("walk_start_to_pickup"), FString::Printf(TEXT("from=%s to=%s at=%s why=%s"), *StartNames[s], *GetNameSafe(Pickup->GetDefinition()), *Pickup->GetActorLocation().ToCompactString(), *Why));
						}
					}
				}
				Run.Check(Run.Failed == WalkFailedBefore, TEXT("walk_all"), FString::Printf(TEXT("pairs=%d exempt_pickups=%d failed=%d"), WalkPairs, Exempted, Run.Failed - WalkFailedBefore));
			}
		}
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxReachabilityFt test=%s result=%s checks=%d failed=%d"), TestName, Run.bPassed ? TEXT("PASS") : TEXT("FAIL"), Run.Checks, Run.Failed);
		if (bQuit)
		{
			FPlatformMisc::RequestExitWithStatus(false, Run.bPassed ? 0 : 1);
		}
	}

	void Command(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		if (!Bots)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtMapReachability: needs the authority world of the map under test (-game)."));
			return;
		}
		bool bQuit = false;
		bool bWalkCheck = false;
		TArray<FString> WalkExempt;
		const FString WalkPrefix(TEXT("WalkExempt="));
		for (const FString& Arg : Args)
		{
			bQuit |= Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase);
			if (Arg.StartsWith(WalkPrefix, ESearchCase::IgnoreCase))
			{
				bWalkCheck = true;
				Arg.Mid(WalkPrefix.Len()).ParseIntoArray(WalkExempt, TEXT(","), true);
			}
		}
		for (FString& Id : WalkExempt)
		{
			Id.TrimStartAndEndInline();
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxReachabilityFt test=%s waiting for the nav build map=%s walk_exempt=%d"), TestName, *World->GetMapName(), WalkExempt.Num());
		Bots->CallWhenNavReady([WeakWorld, bQuit, WalkExempt, bWalkCheck]() { Run(WeakWorld, bQuit, WalkExempt, bWalkCheck); });
	}

	FAutoConsoleCommandWithWorldAndArgs Cmd(TEXT("velox.bots.FtMapReachability"),
		TEXT("velox.bots.FtMapReachability [WalkExempt=<item ids>] [quit]: FT_Map_Reachability on the loaded map (-game -nullrhi -UseFixedTimeStep -FPS=60): Full-filter paths from every PlayerStart to every pickup and between the major items, link ends on the nav mesh, pad link ends at the landing points, Walk paths to every pickup not exempt. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));

	/** The pre-ADR 0008 name, kept as an alias (fts.sh rows, stored task notes). */
	FAutoConsoleCommandWithWorldAndArgs LegacyCmd(TEXT("velox.bots.FtDm1Reachability"),
		TEXT("velox.bots.FtDm1Reachability [quit]: alias of velox.bots.FtMapReachability. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
}
#endif // !UE_BUILD_SHIPPING
