// Velox - the short-lived movable lights of muzzle flashes (and later rockets), capped client-wide at
// UVeloxAudioSettings::MaxMovableLights (ADR 0006 section 9).

#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;

namespace VeloxMovableLights
{
	/**
	 * Spawns a movable point light at Location that lives Seconds, unless MaxMovableLights are alive already (all pawns,
	 * one list per client process, pruned lazily). Returns whether a light was created. Owner owns the component.
	 */
	VELOX_API bool Acquire(UWorld* World, AActor* Owner, const FVector& Location, const FLinearColor& Color, float Intensity, float Radius, float Seconds);

	/** Lights alive right now (pruned; tests, logs). */
	VELOX_API int32 NumActive();
}
