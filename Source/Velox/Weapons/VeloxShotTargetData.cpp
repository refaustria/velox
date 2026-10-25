// Velox - shot target data.

#include "Weapons/VeloxShotTargetData.h"

#include "Engine/PackageMapClient.h"
#include "GameFramework/Actor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxShotTargetData)

bool FGameplayAbilityTargetData_VeloxShot::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bOk = true;
	bool bPart = true;
	Origin.NetSerialize(Ar, Map, bPart);
	bOk &= bPart;
	Dir.NetSerialize(Ar, Map, bPart);
	bOk &= bPart;
	Ar << ViewServerTime;
	Ar << ShotSeq;

	// The hint needs the package map to name the actor; without one (tests, a broken connection) it is dropped.
	uint8 bHint = (bHasHint && Map && HintTarget.IsValid()) ? 1 : 0;
	Ar.SerializeBits(&bHint, 1);
	bHasHint = bHint != 0;
	if (bHasHint)
	{
		UObject* Object = Ar.IsSaving() ? HintTarget.Get() : nullptr;
		if (Map)
		{
			bOk &= Map->SerializeObject(Ar, AActor::StaticClass(), Object);
		}
		else
		{
			bOk = false;
		}
		Ar << HintTargetStamp;
		if (Ar.IsLoading())
		{
			HintTarget = Cast<AActor>(Object);
		}
	}
	else if (Ar.IsLoading())
	{
		HintTarget.Reset();
		HintTargetStamp = 0.f;
	}

	bOutSuccess = bOk && !Ar.IsError();
	return true;
}

FString FGameplayAbilityTargetData_VeloxShot::ToString() const
{
	return FString::Printf(TEXT("VeloxShot seq=%u origin=%s dir=%s view=%.3f hint=%s stamp=%.3f"), ShotSeq, *Origin.ToCompactString(), *Dir.ToCompactString(),
		ViewServerTime, bHasHint ? *GetNameSafe(HintTarget.Get()) : TEXT("none"), HintTargetStamp);
}
