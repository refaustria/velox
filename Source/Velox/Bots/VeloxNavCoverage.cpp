// Velox - nav-coverage lint (ADR 0006 section 6; task M5-03). See VeloxNavCoverage.h for the algorithm and output.

#include "Bots/VeloxNavCoverage.h"

#include "Bots/VeloxBotItemRouting.h"
#include "Bots/VeloxBotLog.h"
#include "Bots/VeloxBotSubsystem.h"
#include "Character/VeloxCharacter.h"
#include "CollisionQueryParams.h"
#include "Components/CapsuleComponent.h"
#include "Combat/VeloxHazardSubsystem.h"
#include "Combat/VeloxHazardVolume.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Parse.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"

namespace VeloxNavCoveragePrivate
{
	const TCHAR* const LayoutTagPrefix = TEXT("Layout:");

	struct FOwnerTable
	{
		TMap<const AActor*, int32> Index;
		TArray<FString> Actors;
		TArray<FString> Layouts;

		int32 Get(const AActor* Actor)
		{
			if (const int32* Found = Index.Find(Actor))
			{
				return *Found;
			}
			const int32 New = Actors.Add(Actor ? Actor->GetActorNameOrLabel() : FString(TEXT("none")));
			Layouts.Add(VeloxNavCoverage::LayoutTagOf(Actor));
			Index.Add(Actor, New);
			return New;
		}
	};

	bool InsideAny(const TArray<FBox>& Boxes, const FVector& Point)
	{
		for (const FBox& Box : Boxes)
		{
			if (Box.IsInsideOrOn(Point))
			{
				return true;
			}
		}
		return false;
	}

	bool TouchesAny(const TArray<FBox>& Boxes, const FBox& Other)
	{
		for (const FBox& Box : Boxes)
		{
			if (Box.ExpandBy(1.0).Intersect(Other))
			{
				return true;
			}
		}
		return false;
	}

	/** Does the component take part in nav generation as solid, pawn-blocking geometry (ADR 0006 section 6)? */
	bool IsWalkableCandidate(const UPrimitiveComponent& Comp)
	{
		return Comp.IsRegistered() && CollisionEnabledHasQuery(Comp.GetCollisionEnabled()) && Comp.GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block &&
			Comp.CanEverAffectNavigation() && Comp.IsNavigationRelevant();
	}
}

FName VeloxNavCoverage::IgnoreTag()
{
	static const FName Tag(TEXT("NavLintIgnore"));
	return Tag;
}

FString VeloxNavCoverage::LayoutTagOf(const AActor* Actor)
{
	if (Actor)
	{
		for (const FName& Tag : Actor->Tags)
		{
			const FString TagString = Tag.ToString();
			if (TagString.StartsWith(VeloxNavCoveragePrivate::LayoutTagPrefix))
			{
				return TagString.RightChop(FCString::Strlen(VeloxNavCoveragePrivate::LayoutTagPrefix));
			}
		}
	}
	return TEXT("-");
}

FVeloxNavCoverageReport VeloxNavCoverage::Run(UWorld* World, const FVeloxNavCoverageParams& Params, TArray<FString>* OutLowClearLines, TArray<FString>* OutSampleLines)
{
	using namespace VeloxNavCoveragePrivate;
	FVeloxNavCoverageReport Report;
	const double StartSeconds = FPlatformTime::Seconds();

	UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;
	UVeloxBotSubsystem* Bots = World ? UVeloxBotSubsystem::Get(World) : nullptr;
	const ARecastNavMesh* Recast = Bots ? Cast<ARecastNavMesh>(Bots->GetNavData()) : nullptr;
	if (!NavSys || !Recast)
	{
		Report.Error = TEXT("no_navdata");
		return Report;
	}
	if (Params.GridCm < 1.f)
	{
		Report.Error = TEXT("bad_grid");
		return Report;
	}

	// Agent geometry from the live nav data (ADR 0005 section 3, D17, D32) and the pawn capsule (ADR 0003 section 6 (c)).
	const ENavigationDataResolution Res = ENavigationDataResolution::Default;
	const double AgentRadius = Recast->AgentRadius;
	const double AgentHeight = Recast->AgentHeight;
	const double StepHeight = Recast->GetAgentMaxStepHeight(Res);
	const double CellHeight = Recast->GetCellHeight(Res);
	const double MinWalkableNormalZ = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(Recast->AgentMaxSlope)));
	const UCapsuleComponent* PawnCapsule = GetDefault<AVeloxCharacter>()->GetCapsuleComponent();
	const double PawnRadius = PawnCapsule ? PawnCapsule->GetUnscaledCapsuleRadius() : AgentRadius;
	const double Grid = Params.GridCm;
	const double Lift = Params.SampleLiftCm;
	// Neighbouring samples on one walkable surface differ by at most a step or one grid cell of the steepest slope.
	const double MaxNeighbourDZ = StepHeight + Grid * FMath::Tan(FMath::DegreesToRadians(static_cast<double>(Recast->AgentMaxSlope)));
	// D41 extent (the shared grounded on-mesh projection) and the tight D44-style window for the retry.
	const FVector OnMeshExtent = UVeloxBotItemRouter::GetOnMeshExtent();
	// Each sample stands for its Grid x Grid cell: a pawn may stand anywhere within half a cell of the sample, so the
	// fit test shrinks the capsule by Grid / 2 and the coverage tolerance grows by Grid / 2 (task M5-03: without this
	// every wall face and ledge off the lattice produces a one-cell strip, Recast erodes ledges by about 4 cells).
	const double HalfCell = Grid * 0.5;
	const double CoverRadius = AgentRadius + HalfCell;
	const double FitRadius = FMath::Max(PawnRadius - HalfCell, 1.0);
	const FVector TightExtent(CoverRadius, CoverRadius, StepHeight);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VeloxNavCoverage), false);
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		QueryParams.AddIgnoredActor(*It);
	}

	TArray<FBox> NavBounds;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		NavBounds.Add(It->GetComponentsBoundingBox(true));
	}

	// Hazard volumes (ADR 0008 3.5): samples and nav polys inside one are skipped and reported as hazard_m2.
	TArray<FBox> HazardBoxes;
	if (const UVeloxHazardSubsystem* Hazards = UVeloxHazardSubsystem::Get(World))
	{
		for (const AVeloxHazardVolume* Volume : Hazards->GetVolumes())
		{
			// Task M7 H8 (review F2, ADR 0008 G6): the volume's own box only, never the actor's component bounds (a nav
			// modifier or any other child would widen the exemption past the hazard).
			if (const UBoxComponent* Box = Volume->GetBox())
			{
				HazardBoxes.Add(Box->Bounds.GetBox());
			}
		}
	}

	// Exempt volumes and the surfaces to sample.
	TArray<FBox> ExemptBoxes;
	TArray<UPrimitiveComponent*> Surfaces;
	// Hazard-only surfaces (the lava bed: pawn-blocking but invisible to navigation) inside a hazard volume: sampled only to be
	// counted as hazard_m2, never judged.
	TSet<const UPrimitiveComponent*> HazardOnly;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor->IsA<APawn>())
		{
			continue;
		}
		const bool bIgnored = Actor->ActorHasTag(IgnoreTag());
		TInlineComponentArray<UPrimitiveComponent*> Comps(Actor);
		for (UPrimitiveComponent* Comp : Comps)
		{
			if (!Comp || !Comp->IsRegistered())
			{
				continue;
			}
			if (bIgnored)
			{
				ExemptBoxes.Add(Comp->Bounds.GetBox());
			}
			else if (IsWalkableCandidate(*Comp))
			{
				Surfaces.Add(Comp);
			}
			else if (HazardBoxes.Num() && CollisionEnabledHasQuery(Comp->GetCollisionEnabled()) && Comp->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block &&
				TouchesAny(HazardBoxes, Comp->Bounds.GetBox()))
			{
				Surfaces.Add(Comp);
				HazardOnly.Add(Comp);
			}
		}
	}
	Report.Surfaces = Surfaces.Num();

	auto Accept = [&](const FVector& Sample, const FNavLocation& Nav)
	{
		return FVector::Dist2D(Sample, Nav.Location) <= CoverRadius + KINDA_SMALL_NUMBER && FMath::Abs(Nav.Location.Z - Sample.Z) <= StepHeight + KINDA_SMALL_NUMBER;
	};

	FOwnerTable Owners;
	TArray<FVeloxNavCoverageCell> Cells;
	TSet<FIntVector> Seen;
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(static_cast<float>(FitRadius),
		static_cast<float>(FMath::Max((AgentHeight - StepHeight) * 0.5, FitRadius)));
	const double CapsuleBottom = StepHeight;  // above the floor: lower obstacles are steps the pawn walks over
	for (UPrimitiveComponent* Comp : Surfaces)
	{
		const FBox Box = Comp->Bounds.GetBox();
		const int32 X0 = VeloxNavCoverageMath::CellIndex(Box.Min.X, Grid);
		const int32 X1 = VeloxNavCoverageMath::CellIndex(Box.Max.X, Grid);
		const int32 Y0 = VeloxNavCoverageMath::CellIndex(Box.Min.Y, Grid);
		const int32 Y1 = VeloxNavCoverageMath::CellIndex(Box.Max.Y, Grid);
		const int32 Owner = Owners.Get(Comp->GetOwner());
		for (int32 IX = X0; IX <= X1; ++IX)
		{
			for (int32 IY = Y0; IY <= Y1; ++IY)
			{
				const double X = VeloxNavCoverageMath::CellCenter(IX, Grid);
				const double Y = VeloxNavCoverageMath::CellCenter(IY, Grid);
				FHitResult Hit;
				if (!Comp->LineTraceComponent(Hit, FVector(X, Y, Box.Max.Z + 10.0), FVector(X, Y, Box.Min.Z - 10.0), QueryParams) ||
					Hit.ImpactNormal.Z < MinWalkableNormalZ)
				{
					continue;
				}
				const double FloorZ = Hit.ImpactPoint.Z;
				const FVector Sample(X, Y, FloorZ + Lift);
				// One sample per lattice cell and floor height: overlapping coplanar pieces are sampled once.
				bool bAlreadySampled = false;
				Seen.Add(FIntVector(IX, IY, FMath::RoundToInt32(FloorZ / FMath::Max(CellHeight, 1.0))), &bAlreadySampled);
				if (bAlreadySampled)
				{
					continue;
				}
				if (InsideAny(ExemptBoxes, Sample))
				{
					++Report.DroppedIgnored;
					continue;
				}
				if (InsideAny(HazardBoxes, Sample))
				{
					++Report.DroppedHazard;
					continue;
				}
				if (HazardOnly.Contains(Comp))
				{
					continue;
				}
				if (NavBounds.Num() && !InsideAny(NavBounds, Sample))
				{
					++Report.DroppedOutsideBounds;
					continue;
				}
				// A pawn must be able to stand here: free headroom over the point and room for the capsule above step height.
				const FVector CapsuleCentre(X, Y, FloorZ + CapsuleBottom + Capsule.GetCapsuleHalfHeight());
				if (World->LineTraceTestByChannel(Sample, FVector(X, Y, FloorZ + AgentHeight), ECC_Pawn, QueryParams) ||
					World->OverlapBlockingTestByChannel(CapsuleCentre, FQuat::Identity, ECC_Pawn, Capsule, QueryParams))
				{
					++Report.DroppedHeadroom;
					continue;
				}

				FVeloxNavCoverageCell& Cell = Cells.AddDefaulted_GetRef();
				Cell.X = IX;
				Cell.Y = IY;
				Cell.Z = static_cast<float>(Sample.Z);
				Cell.Owner = Owner;
				FNavLocation Nav;
				Cell.bCovered = (NavSys->ProjectPointToNavigation(Sample, Nav, OnMeshExtent, Recast) && Accept(Sample, Nav)) ||
					(NavSys->ProjectPointToNavigation(Sample, Nav, TightExtent, Recast) && Accept(Sample, Nav));
				Report.Covered += Cell.bCovered ? 1 : 0;
				if (OutSampleLines && !Cell.bCovered)
				{
					FNavLocation First;
					const bool bAny = NavSys->ProjectPointToNavigation(Sample, First, OnMeshExtent, Recast);
					OutSampleLines->Add(FString::Printf(TEXT("VeloxNavCoverage uncovered p=(%.0f,%.0f,%.0f) actor=%s nearest=%s d2=%.0f dz=%.0f"), Sample.X, Sample.Y, Sample.Z,
						*Owners.Actors[Owner], bAny ? *First.Location.ToCompactString() : TEXT("none"), bAny ? FVector::Dist2D(Sample, First.Location) : -1.0,
						bAny ? First.Location.Z - Sample.Z : 0.0));
				}
			}
		}
	}

	Report.Samples = Cells.Num();
	Report.WalkableM2 = VeloxNavCoverageMath::CellsToM2(Report.Samples, Grid);
	Report.CoveredPct = Report.Samples ? 100.0 * Report.Covered / Report.Samples : 100.0;
	Report.Clusters = VeloxNavCoverageMath::BuildClusters(Cells, Grid, MaxNeighbourDZ, Params.MinClusterM2);
	// What is overhead (D41's class: the thin surface over the void), for the report only.
	for (const FVeloxNavCoverageCluster& Cluster : Report.Clusters)
	{
		FHitResult Hit;
		const bool bOver = World->LineTraceSingleByChannel(Hit, Cluster.Center, Cluster.Center + FVector(0, 0, 2.0 * AgentHeight), ECC_Pawn, QueryParams);
		const AActor* Over = bOver ? Hit.GetActor() : nullptr;
		Report.ClusterOverActors.Add(Over ? Over->GetActorNameOrLabel() : FString(TEXT("none")));
		Report.ClusterOverLayouts.Add(LayoutTagOf(Over));
	}

	// Pass 2: nav polys under low clearance (D32's class).
	TArray<FNavTileRef> Tiles;
	Recast->GetAllNavMeshTiles(Tiles);
	TArray<FNavPoly> Polys;
	for (const FNavTileRef& Tile : Tiles)
	{
		Polys.Reset();
		Recast->GetPolysInTile(Tile, Polys);
		for (const FNavPoly& Poly : Polys)
		{
			FVector Surface = Poly.Center;
			Recast->GetClosestPointOnPoly(Poly.Ref, Poly.Center, Surface);  // detail-mesh height at the centre
			if (InsideAny(ExemptBoxes, Surface + FVector(0, 0, Lift)) || InsideAny(HazardBoxes, Surface + FVector(0, 0, Lift)))
			{
				continue;
			}
			++Report.NavPolys;
			FHitResult Hit;
			// Start two cells above the detail surface so a slightly sunken poly does not start inside its own floor.
			if (World->LineTraceSingleByChannel(Hit, Surface + FVector(0, 0, 2.0 * CellHeight), Surface + FVector(0, 0, AgentHeight), ECC_Pawn, QueryParams))
			{
				// inside=1: the trace starts inside solid geometry - a nav island inside a box (M4-04e's class, no fill underneath).
				const bool bInside = Hit.bStartPenetrating || Hit.Time <= 0.f;
				if (OutLowClearLines && OutLowClearLines->Num() < Params.MaxLowClearLines)
				{
					const AActor* Over = Hit.GetActor();
					OutLowClearLines->Add(FString::Printf(TEXT("VeloxNavCoverage lowclear_poly i=%d center=(%.0f,%.0f,%.0f) headroom=%.0f inside=%d actor=%s layout=%s"), Report.LowClearPolys,
						Surface.X, Surface.Y, Surface.Z, bInside ? 0.0 : Hit.ImpactPoint.Z - Surface.Z, bInside ? 1 : 0, Over ? *Over->GetActorNameOrLabel() : TEXT("none"),
						*LayoutTagOf(Over)));
				}
				++Report.LowClearPolys;
				Report.LowClearInside += bInside ? 1 : 0;
			}
		}
	}

	Report.OwnerActors = MoveTemp(Owners.Actors);
	Report.OwnerLayouts = MoveTemp(Owners.Layouts);
	Report.Seconds = FPlatformTime::Seconds() - StartSeconds;
	Report.bValid = true;
	return Report;
}

FVeloxNavCoverageReport VeloxNavCoverage::RunAndLog(UWorld* World, const FVeloxNavCoverageParams& Params)
{
	TArray<FString> LowClearLines;
	TArray<FString> SampleLines;
	const FVeloxNavCoverageReport Report = Run(World, Params, &LowClearLines, Params.bVerbose ? &SampleLines : nullptr);
	if (!Report.bValid)
	{
		UE_LOG(LogVeloxBots, Warning, TEXT("VeloxNavCoverage result=error reason=%s"), *Report.Error);
		return Report;
	}
	int32 WalkIn = 0;
	int32 Edge = 0;
	for (const FVeloxNavCoverageCluster& C : Report.Clusters)
	{
		Edge += C.bEdge ? 1 : 0;
		WalkIn += (C.bWalkIn && !C.bEdge) ? 1 : 0;
	}
	// Task M7 H12: nav memory beside the lint (a per-map TileSizeUU trades tile count for tile size). LogMemUsed also prints the
	// engine's own LogNavigation line; the returned total is the Detour tile data plus the generator's caches.
	if (UVeloxBotSubsystem* Bots = UVeloxBotSubsystem::Get(World))
	{
		if (const ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Bots->GetNavData()))
		{
			UE_LOG(LogVeloxBots, Log, TEXT("VeloxNavCoverage navmem navdata=%s tiles=%d tile_size=%.0f mem_kb=%.1f"), *Recast->GetName(), Recast->GetNumActiveTiles(),
				Recast->TileSizeUU, static_cast<double>(Recast->LogMemUsed()) / 1024.0);
		}
	}
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxNavCoverage walkable_m2=%.2f covered_pct=%.2f clusters=%d walkin=%d isolated=%d edge=%d grid=%.0f min_cluster_m2=%.2f samples=%d covered=%d surfaces=%d dropped_headroom=%d dropped_outside=%d dropped_ignored=%d seconds=%.3f hazard_m2=%.2f"),
		Report.WalkableM2, Report.CoveredPct, Report.Clusters.Num(), WalkIn, Report.Clusters.Num() - WalkIn - Edge, Edge, Params.GridCm, Params.MinClusterM2, Report.Samples, Report.Covered,
		Report.Surfaces, Report.DroppedHeadroom, Report.DroppedOutsideBounds, Report.DroppedIgnored, Report.Seconds, VeloxNavCoverageMath::CellsToM2(Report.DroppedHazard, Params.GridCm));
	for (int32 i = 0; i < Report.Clusters.Num(); ++i)
	{
		const FVeloxNavCoverageCluster& C = Report.Clusters[i];
		const bool bOwner = Report.OwnerActors.IsValidIndex(C.Owner);
		UE_LOG(LogVeloxBots, Log, TEXT("VeloxNavCoverage cluster i=%d m2=%.2f center=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f) actor=%s layout=%s cells=%d kind=%s over=%s over_layout=%s"), i, C.AreaM2,
			C.Center.X, C.Center.Y, C.Center.Z, C.Extent.X, C.Extent.Y, bOwner ? *Report.OwnerActors[C.Owner] : TEXT("none"), bOwner ? *Report.OwnerLayouts[C.Owner] : TEXT("-"), C.NumCells,
			C.bEdge ? TEXT("edge") : (C.bWalkIn ? TEXT("walkin") : TEXT("isolated")), *Report.ClusterOverActors[i], *Report.ClusterOverLayouts[i]);
	}
	UE_LOG(LogVeloxBots, Log, TEXT("VeloxNavCoverage lowclear polys=%d inside=%d nav_polys=%d"), Report.LowClearPolys, Report.LowClearInside, Report.NavPolys);
	for (const FString& Line : LowClearLines)
	{
		UE_LOG(LogVeloxBots, Log, TEXT("%s"), *Line);
	}
	for (const FString& Line : SampleLines)
	{
		UE_LOG(LogVeloxBots, Log, TEXT("%s"), *Line);
	}
	return Report;
}

#if !UE_BUILD_SHIPPING
namespace VeloxNavCoverageCommand
{
	void NavCoverageCommand(const TArray<FString>& Args, UWorld* World)
	{
		UVeloxBotSubsystem* Bots = World && World->GetNetMode() != NM_Client ? UVeloxBotSubsystem::Get(World) : nullptr;
		if (!Bots)
		{
			UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.NavCoverage: needs the authority world (dedicated server, standalone or listen host)."));
			return;
		}
		FVeloxNavCoverageParams Params;
		bool bQuit = false;
		for (const FString& Arg : Args)
		{
			float Grid = 0.f;
			double MinM2 = 0.0;
			if (FParse::Value(*Arg, TEXT("Grid="), Grid))
			{
				Params.GridCm = FMath::Max(Grid, 1.f);
			}
			else if (FParse::Value(*Arg, TEXT("MinClusterM2="), MinM2))
			{
				Params.MinClusterM2 = FMath::Max(MinM2, 0.0);
			}
			else if (Arg.Equals(TEXT("verbose"), ESearchCase::IgnoreCase))
			{
				Params.bVerbose = true;
			}
			else if (Arg.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
			{
				bQuit = true;
			}
			else
			{
				UE_LOG(LogVeloxBots, Warning, TEXT("velox.bots.NavCoverage [Grid=50] [MinClusterM2=1] [Verbose] [Quit]: unknown argument %s"), *Arg);
			}
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		Bots->CallWhenNavReady([WeakWorld, Params, bQuit]()
		{
			const FVeloxNavCoverageReport Report = VeloxNavCoverage::RunAndLog(WeakWorld.Get(), Params);
			if (bQuit)
			{
				FPlatformMisc::RequestExitWithStatus(false, Report.bValid ? 0 : 1);
			}
		});
	}

	FAutoConsoleCommandWithWorldAndArgs NavCoverageCmd(TEXT("velox.bots.NavCoverage"),
		TEXT("velox.bots.NavCoverage [Grid=50] [MinClusterM2=1] [Verbose] [Quit]: nav-coverage lint (ADR 0006 section 6) after the nav build: walkable floor without nav mesh as clusters, nav polys under low clearance. Quit exits (0, 1 on error). Non-shipping."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NavCoverageCommand));
}
#endif // !UE_BUILD_SHIPPING
