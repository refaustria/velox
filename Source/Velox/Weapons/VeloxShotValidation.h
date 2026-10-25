// Velox - server validation of one shot's target data (ADR 0003 section 5, B19, review of task 04 defect 6).
// World-free: everything the check needs is passed in, so every reject reason is unit-tested
// (Velox.Combat.ShotValidation). Used by the hitscan fire ability's OnServerTargetData (and task 05's projectiles).
//
// Order (review of task 03, server step 1): the fire-rate bucket first (cheapest; a malformed shot still costs a
// token), then the payload: struct type, origin, direction, view time / hint stamp. Nothing here touches the world.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
struct FGameplayAbilityTargetData;
struct FVeloxFireRateBucket;

enum class EVeloxShotReject : uint8
{
	None,
	/** Missing or not FGameplayAbilityTargetData_VeloxShot. */
	Type,
	/** Fire-rate bucket empty. */
	Rate,
	/** Under the minimum spacing after the last accepted shot (B19). */
	Spacing,
	/** Origin contains NaN. */
	Origin,
	/** Direction NaN or zero. */
	Dir,
	/** View time or hint stamp not finite. */
	Time,
};

/** Log / test name of a reason ("none", "type", "rate", "spacing", "origin", "dir", "time"). */
VELOX_API const TCHAR* LexToString(EVeloxShotReject Reason);

/** A shot that passed validation (Dir normalised, hint cleared unless sent). */
struct FVeloxValidatedShot
{
	FVector Origin = FVector::ZeroVector;
	FVector Dir = FVector::ForwardVector;
	double ViewServerTime = 0.0;
	TWeakObjectPtr<AActor> HintTarget;
	float HintTargetStamp = 0.f;
	uint8 ShotSeq = 0;
};

namespace VeloxShotValidation
{
	/**
	 * Validates Raw received at Now for a weapon of Interval: bucket (Capacity = FireRateBurstAllowance, spacing
	 * Interval - SpacingTolerance) then payload. On None, OutShot holds the shot; OutShot.ShotSeq is filled whenever the
	 * struct type is right (logs), 0 otherwise.
	 */
	VELOX_API EVeloxShotReject Validate(const FGameplayAbilityTargetData* Raw, FVeloxFireRateBucket& Bucket, double Now, float Interval, float Capacity,
		float SpacingTolerance, FVeloxValidatedShot& OutShot);
}
