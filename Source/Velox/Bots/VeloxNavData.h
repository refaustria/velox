// Velox - per-map nav tile size (task M7 H12). Map generator helper, editor scripting only.
//
// Detour connects an off-mesh link only when its end lies in the start's tile or one of its 8 neighbours
// (dtNavMesh::connectExtOffMeshLinks); a map whose pad links are longer than the project's 1000 cm tile (Orbita: 22-35 m)
// carries its own ARecastNavMesh with a larger TileSizeUU (spec field NAV_TILE_SIZE_UU, Tools/editor/mapgen.py). ARecastNavMesh
// is notplaceable, so the Python editor subsystem cannot spawn it; this library does. At load the nav system registers the
// placed instance for the single supported agent (UNavigationSystemV1::RegisterNavData) instead of auto-creating
// RecastNavMesh-Default; bForceRebuildOnLoad keeps the runtime build of ADR 0005 section 3. Clients never load it.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"

#include "VeloxNavData.generated.h"

class ARecastNavMesh;

UCLASS()
class VELOX_API UVeloxNavDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Spawn a RecastNavMesh in WorldContext's persistent level with TileSizeUU and bForceRebuildOnLoad set; every other
	 * property comes from [/Script/NavigationSystem.RecastNavMesh]. Returns null (and logs) outside the editor or on failure.
	 */
	UFUNCTION(BlueprintCallable, Category = "Velox|Nav", meta = (WorldContext = "WorldContext"))
	static ARecastNavMesh* SpawnMapNavMesh(UObject* WorldContext, float TileSizeUU);
};
