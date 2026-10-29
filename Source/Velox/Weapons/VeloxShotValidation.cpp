// Velox - server shot validation.

#include "Weapons/VeloxShotValidation.h"

#include "CollisionQueryParams.h"
#include "Core/VeloxCollisionChannels.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Movement/VeloxMovementComponent.h"
#include "Weapons/VeloxFireRateBucket.h"
#include "Weapons/VeloxGameplayAbility_FireProjectile.h"
#include "Weapons/VeloxShotTargetData.h"

const TCHAR* LexToString(EVeloxShotReject Reason)
{
	switch (Reason)
	{
	case EVeloxShotReject::None: return TEXT("none");
	case EVeloxShotReject::Type: return TEXT("type");
	case EVeloxShotReject::Rate: return TEXT("rate");
	case EVeloxShotReject::Origin: return TEXT("origin");
	case EVeloxShotReject::Dir: return TEXT("dir");
	case EVeloxShotReject::Time: return TEXT("time");
	}
	return TEXT("unknown");
}

EVeloxShotReject VeloxShotValidation::Validate(const FGameplayAbilityTargetData* Raw, FVeloxFireRateBucket& Bucket, double Now, float Interval, float Capacity,
	FVeloxValidatedShot& OutShot, bool bConsumeBucket)
{
	OutShot = FVeloxValidatedShot();
	// Exact struct types only (no IsChildOf): the payload of a foreign subclass would be read with the wrong layout.
	const UScriptStruct* Type = Raw ? Raw->GetScriptStruct() : nullptr;
	const bool bProjectile = Type == FGameplayAbilityTargetData_VeloxProjectileShot::StaticStruct();
	const FGameplayAbilityTargetData_VeloxShot* ShotData =
		(bProjectile || Type == FGameplayAbilityTargetData_VeloxShot::StaticStruct()) ? static_cast<const FGameplayAbilityTargetData_VeloxShot*>(Raw) : nullptr;
	OutShot.ShotSeq = ShotData ? ShotData->ShotSeq : 0;

	if (bConsumeBucket && Bucket.Consume(Now, Interval, Capacity) != EVeloxFireRateResult::Accepted)
	{
		return EVeloxShotReject::Rate;
	}
	if (!ShotData)
	{
		return EVeloxShotReject::Type;
	}
	if (ShotData->Origin.ContainsNaN())
	{
		return EVeloxShotReject::Origin;
	}
	const FVector Dir(ShotData->Dir);
	if (Dir.ContainsNaN() || Dir.IsNearlyZero())
	{
		return EVeloxShotReject::Dir;
	}
	if (!FMath::IsFinite(ShotData->ViewServerTime) || !FMath::IsFinite(ShotData->HintTargetStamp) || !FMath::IsFinite(ShotData->FireMoveTimeStamp))
	{
		return EVeloxShotReject::Time;
	}
	OutShot.Origin = ShotData->Origin;
	OutShot.Dir = Dir.GetSafeNormal();
	OutShot.ViewServerTime = ShotData->ViewServerTime;
	OutShot.HintTarget = ShotData->bHasHint ? ShotData->HintTarget : TWeakObjectPtr<AActor>();
	OutShot.HintTargetStamp = ShotData->bHasHint ? ShotData->HintTargetStamp : 0.f;
	OutShot.FireMoveTimeStamp = ShotData->FireMoveTimeStamp;
	OutShot.bProjectileShot = bProjectile;
	return EVeloxShotReject::None;
}

float VeloxShotValidation::ClampFireMoveTimeStamp(float ClientStamp, float LastMoveStamp, float MaxRewindSeconds, float ResetPeriod, EVeloxFireStampClamp& OutResult)
{
	if (FMath::Abs(ClientStamp - LastMoveStamp) > 0.5f * ResetPeriod)
	{
		OutResult = EVeloxFireStampClamp::Reset;
		return ClientStamp;
	}
	const float Window = FMath::Max(MaxRewindSeconds, 0.f);
	const float Clamped = FMath::Clamp(ClientStamp, LastMoveStamp - Window, LastMoveStamp + Window);
	OutResult = Clamped != ClientStamp ? EVeloxFireStampClamp::Clamped : EVeloxFireStampClamp::None;
	return Clamped;
}

FVector VeloxShotValidation::ValidateOrigin(const UWorld& World, const APawn& Shooter, const FVector& ClientOrigin, float MaxOriginErrorCm, const TCHAR*& OutSnapReason)
{
	return ValidateOrigin(World, Shooter, Shooter.GetPawnViewLocation(), ClientOrigin, MaxOriginErrorCm, OutSnapReason);
}

FVeloxShotEye VeloxShotValidation::ResolveShotEye(const APawn& Shooter, bool bHasFireMoveTimeStamp, float FireMoveTimeStamp, float MaxRewindSeconds)
{
	FVeloxShotEye Result;
	Result.Eye = Shooter.GetPawnViewLocation();
	const ACharacter* Character = Cast<ACharacter>(&Shooter);
	const UVeloxMovementComponent* Movement = Character ? Cast<UVeloxMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	if (!bHasFireMoveTimeStamp || !Movement || !FMath::IsFinite(FireMoveTimeStamp) || Shooter.GetLocalRole() != ROLE_Authority)
	{
		return Result;
	}
	const float LastMoveStamp = Movement->GetServerClientMoveTimeStamp();
	EVeloxFireStampClamp Clamp = EVeloxFireStampClamp::None;
	const float Stamp = ClampFireMoveTimeStamp(FireMoveTimeStamp, LastMoveStamp, MaxRewindSeconds, Movement->MinTimeBetweenTimeStampResets, Clamp);
	if (Clamp != EVeloxFireStampClamp::Reset)
	{
		Result.bHasLate = true;
		Result.LateMs = (LastMoveStamp - FireMoveTimeStamp) * 1000.f;
	}
	FVector RingEye;
	if (Movement->GetServerEyeAtMoveTime(Stamp, RingEye))
	{
		Result.Eye = RingEye;
		Result.bFromRing = true;
	}
	return Result;
}

FVector VeloxShotValidation::ValidateOrigin(const UWorld& World, const APawn& Shooter, const FVector& Eye, const FVector& ClientOrigin, float MaxOriginErrorCm,
	const TCHAR*& OutSnapReason)
{
	// Within 1 cm of the eye the occlusion trace is skipped (nothing can lie in between).
	constexpr double NoTraceWithinCmSquared = 1.0;
	OutSnapReason = nullptr;
	if (ClientOrigin.ContainsNaN())
	{
		OutSnapReason = TEXT("nan");
	}
	else if (FVector::Dist(ClientOrigin, Eye) > MaxOriginErrorCm)
	{
		OutSnapReason = TEXT("distance");
	}
	else if (FVector::DistSquared(ClientOrigin, Eye) > NoTraceWithinCmSquared)
	{
		const FCollisionQueryParams OccParams(SCENE_QUERY_STAT(VeloxShotOrigin), true, &Shooter);
		if (World.LineTraceTestByChannel(Eye, ClientOrigin, Velox_TraceChannel_Weapon, OccParams))
		{
			OutSnapReason = TEXT("occluded");
		}
	}
	return OutSnapReason ? Eye : ClientOrigin;
}
