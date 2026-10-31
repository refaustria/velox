// Velox - FT_DM1_Reachability (ADR 0006 section 7, task M5-05).
//
//   velox.bots.FtDm1Reachability   console FT on L_DM1_Blockout (`UnrealEditor-Cmd Velox.uproject L_DM1_Blockout -game -nullrhi
//                                  -UseFixedTimeStep -FPS=60 -ExecCmds="velox.bots.Seed 1, velox.bots.FtDm1Reachability quit"`):
//                                  once the runtime nav is built, a synchronous FindPath with the Full filter must succeed
//                                  (complete path, end within PathEndToleranceCm of the goal) from every PlayerStart to every
//                                  pickup, and between every ordered pair of major items (Mega, Red / Yellow Armor, Quad, ...)
//                                  and the Railgun. The map must carry 8 PlayerStarts and the ADR item set. Logs
//                                  `VeloxReachabilityFt check=... ok=` lines and `VeloxReachabilityFt test=FT_DM1_Reachability
//                                  result=PASS|FAIL`; quit exits 0 / 1. (The jump-pad landing check is
//                                  velox.bots.DumpPadLandings, run by the task notes.)

#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxNavFilters.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Items/VeloxItemDefinition.h"
#include "Items/VeloxItemRegistrySubsystem.h"
#include "Items/VeloxPickup.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavMesh/NavMeshPath.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

#if !UE_BUILD_SHIPPING
namespace VeloxDm1Ft
{
	constexpr TCHAR TestName[] = TEXT("FT_DM1_Reachability");
	/** The path's last point must be this close to the goal (a partial path ends short). */
	constexpr double PathEndToleranceCm = 150.0;
	constexpr int32 ExpectedPlayerStarts = 8;
	constexpr int32 MinPickups = 30;

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

	void Run(TWeakObjectPtr<UWorld> WeakWorld, bool bQuit)
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
			Run.Check(StartLocations.Num() == ExpectedPlayerStarts, TEXT("playerstarts"), FString::Printf(TEXT("n=%d expected=%d"), StartLocations.Num(), ExpectedPlayerStarts));
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
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.FtDm1Reachability: needs the authority world of L_DM1_Blockout (-game)."));
			return;
		}
		bool bQuit = false;
		for (const FString& Arg : Args)
		{
			bQuit |= Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase);
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxReachabilityFt test=%s waiting for the nav build"), TestName);
		Bots->CallWhenNavReady([WeakWorld, bQuit]() { Run(WeakWorld, bQuit); });
	}

	FAutoConsoleCommandWithWorldAndArgs Cmd(TEXT("velox.bots.FtDm1Reachability"),
		TEXT("velox.bots.FtDm1Reachability [quit]: FT_DM1_Reachability on L_DM1_Blockout (-game -nullrhi -UseFixedTimeStep -FPS=60): Full-filter paths from every PlayerStart to every pickup and between the major items. Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Command));
}
#endif // !UE_BUILD_SHIPPING
