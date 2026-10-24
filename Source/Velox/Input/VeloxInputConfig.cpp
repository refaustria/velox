// Velox - input config.

#include "Input/VeloxInputConfig.h"

#include "Core/VeloxLog.h"
#include "InputAction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxInputConfig)

namespace VeloxInputConfig
{
	static const UInputAction* FindActionForTag(const TArray<FVeloxInputAction>& Actions, const FGameplayTag& InputTag)
	{
		for (const FVeloxInputAction& Action : Actions)
		{
			if (Action.InputAction && Action.InputTag == InputTag)
			{
				return Action.InputAction;
			}
		}
		return nullptr;
	}
}

const UInputAction* UVeloxInputConfig::FindNativeInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound) const
{
	const UInputAction* Found = VeloxInputConfig::FindActionForTag(NativeInputActions, InputTag);
	if (!Found && bLogNotFound)
	{
		UE_LOG(LogVelox, Warning, TEXT("No native input action for tag [%s] in input config [%s]."), *InputTag.ToString(), *GetNameSafe(this));
	}
	return Found;
}

const UInputAction* UVeloxInputConfig::FindAbilityInputActionForTag(const FGameplayTag& InputTag, bool bLogNotFound) const
{
	const UInputAction* Found = VeloxInputConfig::FindActionForTag(AbilityInputActions, InputTag);
	if (!Found && bLogNotFound)
	{
		UE_LOG(LogVelox, Warning, TEXT("No ability input action for tag [%s] in input config [%s]."), *InputTag.ToString(), *GetNameSafe(this));
	}
	return Found;
}
