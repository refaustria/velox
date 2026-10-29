// Velox - gameplay effect context.

#include "Combat/VeloxGameplayEffectContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayEffectContext)

const FVeloxGameplayEffectContext* FVeloxGameplayEffectContext::Get(const FGameplayEffectContextHandle& Handle)
{
	const FGameplayEffectContext* Base = Handle.Get();
	if (Base && Base->GetScriptStruct()->IsChildOf(FVeloxGameplayEffectContext::StaticStruct()))
	{
		return static_cast<const FVeloxGameplayEffectContext*>(Base);
	}
	return nullptr;
}

FVeloxGameplayEffectContext* FVeloxGameplayEffectContext::Get(FGameplayEffectContextHandle& Handle)
{
	FGameplayEffectContext* Base = Handle.Get();
	if (Base && Base->GetScriptStruct()->IsChildOf(FVeloxGameplayEffectContext::StaticStruct()))
	{
		return static_cast<FVeloxGameplayEffectContext*>(Base);
	}
	return nullptr;
}

FGameplayEffectContext* FVeloxGameplayEffectContext::Duplicate() const
{
	FVeloxGameplayEffectContext* NewContext = new FVeloxGameplayEffectContext();
	*NewContext = *this;
	if (GetHitResult())
	{
		NewContext->AddHitResult(*GetHitResult(), true);
	}
	return NewContext;
}

bool FVeloxGameplayEffectContext::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	bool bSuccess = FGameplayEffectContext::NetSerialize(Ar, Map, bOutSuccess);

	bool bLocalSuccess = true;
	HitDirection.NetSerialize(Ar, Map, bLocalSuccess);
	bSuccess &= bLocalSuccess;
	HitLocation.NetSerialize(Ar, Map, bLocalSuccess);
	bSuccess &= bLocalSuccess;
	Ar << KnockbackScale;
	Ar << ShotSeq;
	Ar << PatternSeq;
	Ar << Flags;

	bOutSuccess = bOutSuccess && bSuccess;
	return true;
}

FString FVeloxGameplayEffectContext::ToString() const
{
	return FString::Printf(TEXT("%s dir=%s loc=%s kbscale=%.2f seq=%u pattern=%u flags=0x%02x"), *FGameplayEffectContext::ToString(),
		*HitDirection.ToCompactString(), *HitLocation.ToCompactString(), KnockbackScale, ShotSeq, PatternSeq, Flags);
}
