// Velox - server shot validation.

#include "Weapons/VeloxShotValidation.h"

#include "Weapons/VeloxFireRateBucket.h"
#include "Weapons/VeloxShotTargetData.h"

const TCHAR* LexToString(EVeloxShotReject Reason)
{
	switch (Reason)
	{
	case EVeloxShotReject::None: return TEXT("none");
	case EVeloxShotReject::Type: return TEXT("type");
	case EVeloxShotReject::Rate: return TEXT("rate");
	case EVeloxShotReject::Spacing: return TEXT("spacing");
	case EVeloxShotReject::Origin: return TEXT("origin");
	case EVeloxShotReject::Dir: return TEXT("dir");
	case EVeloxShotReject::Time: return TEXT("time");
	}
	return TEXT("unknown");
}

EVeloxShotReject VeloxShotValidation::Validate(const FGameplayAbilityTargetData* Raw, FVeloxFireRateBucket& Bucket, double Now, float Interval, float Capacity,
	float SpacingTolerance, FVeloxValidatedShot& OutShot)
{
	OutShot = FVeloxValidatedShot();
	const FGameplayAbilityTargetData_VeloxShot* ShotData =
		(Raw && Raw->GetScriptStruct() == FGameplayAbilityTargetData_VeloxShot::StaticStruct()) ? static_cast<const FGameplayAbilityTargetData_VeloxShot*>(Raw) : nullptr;
	OutShot.ShotSeq = ShotData ? ShotData->ShotSeq : 0;

	switch (Bucket.Consume(Now, Interval, Capacity, FVeloxFireRateBucket::MinSpacingFor(Interval, SpacingTolerance)))
	{
	case EVeloxFireRateResult::Rate: return EVeloxShotReject::Rate;
	case EVeloxFireRateResult::Spacing: return EVeloxShotReject::Spacing;
	case EVeloxFireRateResult::Accepted: break;
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
	if (!FMath::IsFinite(ShotData->ViewServerTime) || !FMath::IsFinite(ShotData->HintTargetStamp))
	{
		return EVeloxShotReject::Time;
	}
	OutShot.Origin = ShotData->Origin;
	OutShot.Dir = Dir.GetSafeNormal();
	OutShot.ViewServerTime = ShotData->ViewServerTime;
	OutShot.HintTarget = ShotData->bHasHint ? ShotData->HintTarget : TWeakObjectPtr<AActor>();
	OutShot.HintTargetStamp = ShotData->bHasHint ? ShotData->HintTargetStamp : 0.f;
	return EVeloxShotReject::None;
}
