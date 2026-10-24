// Velox - ability system globals: allocates FVeloxGameplayEffectContext for every spec (ADR 0003 section 2).
// Selected in Config/DefaultGame.ini ([/Script/GameplayAbilities.GameplayAbilitiesDeveloperSettings]).

#pragma once

#include "AbilitySystemGlobals.h"

#include "VeloxAbilitySystemGlobals.generated.h"

UCLASS(Config = Game)
class VELOX_API UVeloxAbilitySystemGlobals : public UAbilitySystemGlobals
{
	GENERATED_BODY()

public:
	//~UAbilitySystemGlobals interface
	virtual FGameplayEffectContext* AllocGameplayEffectContext() const override;
	//~End of UAbilitySystemGlobals interface
};
