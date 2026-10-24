// Velox - ability system component.

#include "Combat/VeloxAbilitySystemComponent.h"

#include "GameplayTagContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxAbilitySystemComponent)

UVeloxAbilitySystemComponent::UVeloxAbilitySystemComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UVeloxAbilitySystemComponent::AbilityInputTagPressed(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

		if (Spec.IsActive())
		{
			AbilitySpecInputPressed(Spec);
		}
		else
		{
			Spec.InputPressed = true;
			TryActivateAbility(Spec.Handle);
		}
	}
}

void UVeloxAbilitySystemComponent::AbilityInputTagReleased(const FGameplayTag& InputTag)
{
	if (!InputTag.IsValid())
	{
		return;
	}

	ABILITYLIST_SCOPE_LOCK();
	for (FGameplayAbilitySpec& Spec : ActivatableAbilities.Items)
	{
		if (!Spec.Ability || !Spec.GetDynamicSpecSourceTags().HasTagExact(InputTag))
		{
			continue;
		}

		Spec.InputPressed = false;
		if (Spec.IsActive())
		{
			AbilitySpecInputReleased(Spec);
		}
	}
}
