// Velox - ability system globals.

#include "Combat/VeloxAbilitySystemGlobals.h"

#include "Combat/VeloxGameplayEffectContext.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAbilitySystemGlobals)

FGameplayEffectContext* UVeloxAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FVeloxGameplayEffectContext();
}
