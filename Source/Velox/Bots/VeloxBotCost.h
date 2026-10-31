// Velox - report-only per-category cost counters of the bot controller tick (task M4-08, ADR 0005 section 13 / D14).
// Each scope adds its wall clock to the bot subsystem's window; the subsystem prints them in the `VeloxBotCost` line
// next to `VeloxBotBudget`. Nested categories (OffMesh in Move, Segment in Follow, Fire in Apply, Traces in Perceive)
// are inclusive in their parent. Cost: two FPlatformTime::Cycles64 reads per scope; nothing here changes a decision.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

class UWorld;

enum class EVeloxBotCost : uint8
{
	/** AAIController::Tick (Super). */
	Super,
	/** Perception (FOV + LOS traces, memory, hearing, projectile threats). */
	Perceive,
	/** The LOS traces inside Perceive. */
	PerceiveTraces,
	/** Decide: the item router's UpdateBot (evaluation slots, FindPathSync of the candidates). */
	Decide,
	/** EnsureMove: MoveTo / repath, incl. OffMesh. */
	Move,
	/** UpdateOffMeshRecovery (the on-mesh projection every tick, D17 candidates, sweeps). */
	OffMesh,
	/** Navigate: the path follower step and the stuck rules, incl. Segment. */
	Follow,
	/** The D35 segment line-of-walk check (projection + NavigationRaycast). */
	Segment,
	/** UpdateCombat: the strafe / dodge overlay (incl. its strafe-probe nav raycasts). */
	Overlay,
	/** Act: aim model, weapon choice, fire rule (incl. the aim-ray trace). */
	Act,
	/** ApplyInput: control rotation + pawn yaw, movement input, ability input, incl. Fire. */
	Apply,
	/** ASC::ProcessAbilityInput inside Apply (the fire ability's server path). */
	Fire,
	Num
};

/** Short field name of a category in the `VeloxBotCost` line. */
const TCHAR* LexToString(EVeloxBotCost Cost);

namespace VeloxBotCost
{
	/** Adds Micros to the category of World's bot subsystem window (no-op without a subsystem). */
	void Note(const UWorld* World, EVeloxBotCost Cost, double Micros);
}

/** Adds the scope's wall clock to a category. */
struct FVeloxBotCostScope
{
	FVeloxBotCostScope(const UWorld* InWorld, EVeloxBotCost InCost)
		: World(InWorld), Cost(InCost), Start(FPlatformTime::Cycles64())
	{
	}
	~FVeloxBotCostScope()
	{
		VeloxBotCost::Note(World, Cost, FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start) * 1000.0);
	}
	FVeloxBotCostScope(const FVeloxBotCostScope&) = delete;
	FVeloxBotCostScope& operator=(const FVeloxBotCostScope&) = delete;

private:
	const UWorld* World;
	EVeloxBotCost Cost;
	uint64 Start;
};
