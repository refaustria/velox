// Velox - every hazard volume of a world (ADR 0008 section 3.3). Volumes register in BeginPlay and unregister in EndPlay,
// on the server and on clients (bNetLoadOnClient). Bots (ADR 0008 3.6: the edge and lava behaviours run only when
// HasHazards()) and lint (VeloxNavCoverage's hazard exemption) query it; it never applies damage itself.

#pragma once

#include "Subsystems/WorldSubsystem.h"

#include "VeloxHazardSubsystem.generated.h"

class AVeloxHazardVolume;
enum class EVeloxHazardType : uint8;

UCLASS()
class VELOX_API UVeloxHazardSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVeloxHazardSubsystem* Get(const UWorld* World);

	void RegisterVolume(AVeloxHazardVolume* Volume);
	void UnregisterVolume(AVeloxHazardVolume* Volume);

	/** True if the world has at least one hazard volume. */
	bool HasHazards() const;

	/** The first volume containing Point, or null. */
	AVeloxHazardVolume* FindHazardAt(const FVector& Point) const;

	/** True if Point lies inside any hazard volume. */
	bool IsInHazard(const FVector& Point) const { return FindHazardAt(Point) != nullptr; }

	/** True if Point lies inside a hazard volume of Type. */
	bool IsInHazard(const FVector& Point, EVeloxHazardType Type) const;

	/** Every registered volume (stale entries skipped). */
	TArray<AVeloxHazardVolume*> GetVolumes() const;

private:
	TArray<TWeakObjectPtr<AVeloxHazardVolume>> Volumes;
};
