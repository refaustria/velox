// Velox - nav-coverage lint (ADR 0006 section 6, owed from ADR 0005 D17 / D32 / D41; task M5-03). Server-side only.
//
// Pass 1 (D41's class, walkable floor without nav mesh): every primitive component that blocks ECC_Pawn and affects
// navigation is sampled by downward traces on a Grid cm world lattice (cell centres) over its bounds; a hit whose
// slope is <= the nav agent's max slope is a floor sample 5 cm above the surface. Samples are dropped when a pawn
// cannot stand there (a line of AgentHeight free headroom, plus a capsule between step height and AgentHeight whose
// radius is the pawn's minus Grid / 2 - "sealed by rule, fine"), outside every NavMeshBoundsVolume, or inside a
// NavLintIgnore actor. Each remaining sample is projected to the nav mesh with the D41 extent (AirborneProjectRadiusCm,
// AirborneProjectRadiusCm, OnMeshHalfHeightCm) and counts as covered only when the projected point lies within
// AgentRadius + Grid / 2 (2-D; the sample stands for its cell) and StepHeight (vertically) of the sample; because Detour
// ranks candidates by 2-D distance and uses Z only as a filter (D44), a rejected projection is retried once with the
// tight window (AgentRadius + Grid / 2, same, StepHeight) so a poly overhead or below cannot shadow an acceptable one.
// Uncovered samples are clustered (VeloxNavCoverageMath.h); a cluster next to covered floor is kind=walkin (a walking
// bot can enter it - D41 / D17), otherwise kind=isolated (reached only by a jump, pad or knockback).
//
// Pass 2 (D32's class, nav under low clearance): every ground poly of the Recast mesh whose centre (detail surface)
// has less than AgentHeight of free headroom (ECC_Pawn line trace from two cells above it); inside=1 when the trace
// starts in solid geometry (a nav island inside a box without fill_collision_underneath_for_navmesh, M4-04e).
//
// Output (LogVeloxBots, parsed by infra/ci/navlint.sh):
//   VeloxNavCoverage walkable_m2=<x> covered_pct=<y> clusters=<n> walkin=<w> isolated=<i> grid=<g> min_cluster_m2=<m> ...
//   VeloxNavCoverage cluster i=<k> m2=<a> center=(x,y,z) extent=(dx,dy) actor=<name> layout=<tag> cells=<c>
//                    kind=walkin|isolated over=<name> over_layout=<tag>
//     (actor = the surface the cluster lies on; over = the first pawn-blocking surface above its centre, D41's thin surface)
//   VeloxNavCoverage lowclear polys=<n> inside=<m> nav_polys=<p>
//   VeloxNavCoverage lowclear_poly i=<k> center=(x,y,z) headroom=<cm> inside=0|1 actor=<name> layout=<tag>   (first 50)
//   VeloxNavCoverage uncovered p=(x,y,z) actor=<name> nearest=<nav point|none> d2=<cm> dz=<cm>              (Verbose only)
//
// Console command (non-shipping, authority world, waits for "VeloxNav built"):
//   velox.bots.NavCoverage [Grid=50] [MinClusterM2=1] [Verbose] [Quit]

#pragma once

#include "CoreMinimal.h"
#include "Bots/VeloxNavCoverageMath.h"

class AActor;
class UWorld;

struct FVeloxNavCoverageParams
{
	/** Lattice spacing (cm); ADR 0006 section 6 default. */
	float GridCm = 50.f;
	/** Clusters smaller than this are not reported (m^2); ADR 0006 section 6 default. */
	double MinClusterM2 = 1.0;
	/** Samples sit this far above the traced surface (cm); ADR 0006 section 6. */
	float SampleLiftCm = 5.f;
	/** Upper limit of lowclear_poly detail lines. */
	int32 MaxLowClearLines = 50;
	/** Log one line per uncovered sample (debugging a cluster). */
	bool bVerbose = false;
};

struct FVeloxNavCoverageReport
{
	bool bValid = false;
	FString Error;
	int32 Samples = 0;
	int32 Covered = 0;
	int32 DroppedHeadroom = 0;
	int32 DroppedOutsideBounds = 0;
	int32 DroppedIgnored = 0;
	int32 Surfaces = 0;
	double WalkableM2 = 0.0;
	double CoveredPct = 0.0;
	double Seconds = 0.0;
	TArray<FVeloxNavCoverageCluster> Clusters;
	/** Owner index -> actor label / Layout tag, indexed by Clusters[i].Owner. */
	TArray<FString> OwnerActors;
	TArray<FString> OwnerLayouts;
	/** Per cluster: label / Layout tag of the first pawn-blocking surface above the cluster centre within 2 x AgentHeight ("none" / "-"). */
	TArray<FString> ClusterOverActors;
	TArray<FString> ClusterOverLayouts;
	int32 LowClearPolys = 0;
	/** Of LowClearPolys: the headroom trace started inside solid geometry. */
	int32 LowClearInside = 0;
	int32 NavPolys = 0;
};

namespace VeloxNavCoverage
{
	/** Actor tag that exempts an actor's surfaces and the volume of its components from the lint (ADR 0006 section 6). */
	FName IgnoreTag();

	/** Runs both passes on a world whose nav mesh is built; logs nothing. Optional detail lines for the log. */
	FVeloxNavCoverageReport Run(UWorld* World, const FVeloxNavCoverageParams& Params, TArray<FString>* OutLowClearLines = nullptr, TArray<FString>* OutSampleLines = nullptr);

	/** Runs and logs the report lines above. */
	FVeloxNavCoverageReport RunAndLog(UWorld* World, const FVeloxNavCoverageParams& Params);

	/** The Layout:<id> tag value of an actor (task M5-02 tags), "-" if it has none. */
	FString LayoutTagOf(const AActor* Actor);
}
