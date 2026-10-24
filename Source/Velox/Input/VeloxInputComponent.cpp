// Velox - enhanced input component.

#include "Input/VeloxInputComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxInputComponent)

UVeloxInputComponent::UVeloxInputComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UVeloxInputComponent::RemoveBinds(TArray<uint32>& BindHandles)
{
	for (const uint32 Handle : BindHandles)
	{
		RemoveBindingByHandle(Handle);
	}
	BindHandles.Reset();
}
