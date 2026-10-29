// Velox - attribute set base.

#include "Combat/VeloxAttributeSet.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatSettings.h"
#include "GameModes/VeloxPlayerState.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAttributeSet)

UVeloxAbilitySystemComponent* UVeloxAttributeSet::GetVeloxAbilitySystemComponent() const
{
	return Cast<UVeloxAbilitySystemComponent>(GetOwningAbilitySystemComponent());
}

const UVeloxCombatSettings& UVeloxAttributeSet::GetCombatSettings() const
{
	const AVeloxPlayerState* PlayerState = Cast<AVeloxPlayerState>(GetOwningActor());
	return UVeloxCombatSettings::GetOrDefault(PlayerState ? PlayerState->GetCombatSettings() : nullptr);
}
