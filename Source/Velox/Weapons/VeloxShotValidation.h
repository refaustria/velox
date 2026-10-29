// Velox - server validation of one shot's target data (ADR 0003 section 5, B19, B26, review of task 04 defect 6).
// Validate is world-free: everything the check needs is passed in, so every reject reason is unit-tested
// (Velox.Combat.ShotValidation). Used by OnServerTargetData of the hitscan and the projectile fire abilities (B26).
//
// Order (review of task 03, server step 1): the fire-rate bucket first (cheapest; a malformed shot still costs a
// token), then the payload: struct type, origin, direction, view time / hint stamp / projectile move time stamp.
// ValidateOrigin is the one implementation of the B15 origin rule (TraceShot and the projectile ability); it traces.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class APawn;
class UWorld;
struct FGameplayAbilityTargetData;
struct FVeloxFireRateBucket;

enum class EVeloxShotReject : uint8
{
	None,
	/** Missing or neither FGameplayAbilityTargetData_VeloxShot nor its projectile subclass. */
	Type,
	/** Fire-rate bucket empty. */
	Rate,
	/** Under the minimum spacing after the last accepted shot (B19). */
	Spacing,
	/** Origin contains NaN. */
	Origin,
	/** Direction NaN or zero. */
	Dir,
	/** View time, hint stamp or (projectile) fire move time stamp not finite. */
	Time,
};

/** What ClampFireMoveTimeStamp did with the client's projectile fire time stamp (B25). */
enum class EVeloxFireStampClamp : uint8
{
	/** Within LastMoveStamp +- MaxRewindSeconds: taken as is. */
	None,
	/** Outside that window: clamped to it (logged as VeloxShotTimeClamp; an honest client never triggers it). */
	Clamped,
	/** More than half a reset period from LastMoveStamp: a time stamp reset lies in between, taken as is. */
	Reset,
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
	/** FGameplayAbilityTargetData_VeloxProjectileShot::FireMoveTimeStamp (finite); 0 for a hitscan payload. */
	float FireMoveTimeStamp = 0.f;
	/** The payload was the projectile subclass (carries FireMoveTimeStamp). */
	bool bProjectileShot = false;
	uint8 ShotSeq = 0;
};

namespace VeloxShotValidation
{
	/**
	 * Validates Raw received at Now for a weapon of Interval: bucket (Capacity = FVeloxFireRateBucket::CapacityFor, spacing
	 * Interval - SpacingTolerance) then payload. On None, OutShot holds the shot; OutShot.ShotSeq is filled whenever the
	 * struct type is right (logs), 0 otherwise.
	 */
	VELOX_API EVeloxShotReject Validate(const FGameplayAbilityTargetData* Raw, FVeloxFireRateBucket& Bucket, double Now, float Interval, float Capacity,
		float SpacingTolerance, FVeloxValidatedShot& OutShot);

	/**
	 * ADR 0003 B15 step 1 (shared by TraceShot and the projectile ability, B26): the client origin, or the shooter's
	 * server eye (GetPawnViewLocation) when the origin contains NaN, lies farther than MaxOriginErrorCm from the eye, or
	 * a Weapon-channel world trace from the eye to it is blocked (skipped within 1 cm). OutSnapReason: nullptr when the
	 * client origin stands, else "nan", "distance" or "occluded". Logs nothing (the caller knows the shot).
	 */
	/**
	 * ADR 0003 B25, review of task 05 defect 6 (world-free): the server's view of a projectile's FireMoveTimeStamp. The
	 * client value is trusted within LastMoveStamp (the owner's last processed move) +- MaxRewindSeconds and clamped
	 * outside it, unless the two stamps are more than 0.5 x ResetPeriod (MinTimeBetweenTimeStampResets) apart: then the
	 * client reset its time stamps in between (CharacterMovementComponent.cpp l.8864-8867, the server follows at
	 * l.9395-9408) and the client value is kept, like IsMoveTimeDue's half-period rule.
	 */
	VELOX_API float ClampFireMoveTimeStamp(float ClientStamp, float LastMoveStamp, float MaxRewindSeconds, float ResetPeriod, EVeloxFireStampClamp& OutResult);

	VELOX_API FVector ValidateOrigin(const UWorld& World, const APawn& Shooter, const FVector& ClientOrigin, float MaxOriginErrorCm, const TCHAR*& OutSnapReason);
}
