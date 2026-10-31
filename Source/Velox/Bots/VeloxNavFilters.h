// Velox - nav query filters: which Velox link types a bot may use (ADR 0005 section 4). The bot controller (task 04)
// picks one per tick from its difficulty preset and its state (rocket launcher, rockets, health); velox.bots.DumpNav
// takes the filter by name (Walk | Hop | Full).

#pragma once

#include "NavFilters/NavigationQueryFilter.h"

#include "VeloxNavFilters.generated.h"

/** Presets without strafe-jumping or rocket jumps: excludes JumpFast and RocketJump links. */
UCLASS()
class VELOX_API UVeloxNavFilter_Walk : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UVeloxNavFilter_Walk(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** Strafe-jumping bots that may not (or cannot right now) rocket-jump: excludes RocketJump links. */
UCLASS()
class VELOX_API UVeloxNavFilter_Hop : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UVeloxNavFilter_Hop(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

/** Every link type. */
UCLASS()
class VELOX_API UVeloxNavFilter_Full : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UVeloxNavFilter_Full(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};

namespace VeloxNavFilters
{
	/** Walk | Hop | Full (case-insensitive) -> filter class; null for anything else. */
	VELOX_API TSubclassOf<UNavigationQueryFilter> FilterClassByName(const FString& Name);
}
