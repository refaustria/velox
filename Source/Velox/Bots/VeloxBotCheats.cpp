// Velox - non-shipping bot console commands (ADR 0005 section 14). Authority world (dedicated server, standalone, listen
// host). Both wait for the runtime nav build (UVeloxBotSubsystem::CallWhenNavReady), so they work from -ExecCmds.
//   velox.bots.DumpNav x y z x y z [Walk|Hop|Full]   path query (default Full); one header line and one line per path
//                                                     point, link=<type> on the start point of a link segment
//                                                     (FNavMeshNodeFlags::IsNavLink + FindLinkByStart), with the
//                                                     segment's nav area as a cross-check.
//   velox.bots.DumpPadLandings                        every AVeloxJumpPad: ComputeLandingPoint and the end of its
//                                                     JumpPad nav link.

#include "AI/Navigation/NavigationTypes.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Bots/VeloxNavFilters.h"
#include "Bots/VeloxNavLink.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Movement/VeloxJumpPad.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "NavMesh/NavMeshPath.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavigationSystemTypes.h"

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
