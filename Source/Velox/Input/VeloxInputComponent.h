// Velox - enhanced input component with tag-based binding helpers.

#pragma once

#include "EnhancedInputComponent.h"
#include "Input/VeloxInputConfig.h"

#include "VeloxInputComponent.generated.h"

UCLASS(Config = Input)
class VELOX_API UVeloxInputComponent : public UEnhancedInputComponent
{
	GENERATED_BODY()

public:
	UVeloxInputComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Binds the native action registered for InputTag to Func. */
	template <class UserClass, typename FuncType>
	void BindNativeAction(const UVeloxInputConfig* InputConfig, const FGameplayTag& InputTag, ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func, bool bLogIfNotFound);

	/** Binds every ability action: Started -> PressedFunc(InputTag), Completed -> ReleasedFunc(InputTag). */
	template <class UserClass, typename PressedFuncType, typename ReleasedFuncType>
	void BindAbilityActions(const UVeloxInputConfig* InputConfig, UserClass* Object, PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc, TArray<uint32>& OutBindHandles);

	/** Removes bindings created by BindAbilityActions and empties BindHandles. */
	void RemoveBinds(TArray<uint32>& BindHandles);
};

template <class UserClass, typename FuncType>
void UVeloxInputComponent::BindNativeAction(const UVeloxInputConfig* InputConfig, const FGameplayTag& InputTag, ETriggerEvent TriggerEvent, UserClass* Object, FuncType Func, bool bLogIfNotFound)
{
	check(InputConfig);
	if (const UInputAction* Action = InputConfig->FindNativeInputActionForTag(InputTag, bLogIfNotFound))
	{
		BindAction(Action, TriggerEvent, Object, Func);
	}
}

template <class UserClass, typename PressedFuncType, typename ReleasedFuncType>
void UVeloxInputComponent::BindAbilityActions(const UVeloxInputConfig* InputConfig, UserClass* Object, PressedFuncType PressedFunc, ReleasedFuncType ReleasedFunc, TArray<uint32>& OutBindHandles)
{
	check(InputConfig);
	for (const FVeloxInputAction& Action : InputConfig->AbilityInputActions)
	{
		if (!Action.InputAction || !Action.InputTag.IsValid())
		{
			continue;
		}
		if (PressedFunc)
		{
			OutBindHandles.Add(BindAction(Action.InputAction, ETriggerEvent::Started, Object, PressedFunc, Action.InputTag).GetHandle());
		}
		if (ReleasedFunc)
		{
			OutBindHandles.Add(BindAction(Action.InputAction, ETriggerEvent::Completed, Object, ReleasedFunc, Action.InputTag).GetHandle());
		}
	}
}
