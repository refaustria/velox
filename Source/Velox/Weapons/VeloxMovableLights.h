// Velox - the short-lived movable lights of muzzle flashes (and later rockets), capped client-wide at
// UVeloxAudioSettings::MaxMovableLights (ADR 0006 section 9).

#pragma once

#include "CoreMinimal.h"

class UWorld;
class AActor;
class UPointLightComponent;
class USceneComponent;

namespace VeloxMovableLights
{
	/**
	 * Spawns a movable point light at Location that lives Seconds, unless MaxMovableLights are alive already (all pawns,
	 * one list per client process, pruned lazily). Returns whether a light was created. Owner owns the component.
	 */
	VELOX_API bool Acquire(UWorld* World, AActor* Owner, const FVector& Location, const FLinearColor& Color, float Intensity, float Radius, float Seconds);

	/**
	 * A light that lives as long as Owner (a rocket in flight, ADR 0006 E1.3): attached to Parent (the owner's root when null),
	 * counted in the same list, so muzzle flashes and flight lights share MaxMovableLights. Null when the budget is full.
	 */
	VELOX_API UPointLightComponent* AcquireAttached(AActor* Owner, USceneComponent* Parent, const FLinearColor& Color, float Intensity, float Radius);

	/** Lights alive right now (pruned; tests, logs). */
	VELOX_API int32 NumActive();
}
