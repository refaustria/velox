// Velox - custom character movement component.

#include "Movement/VeloxMovementComponent.h"

#include "Movement/VeloxMovementSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxMovementComponent)

UVeloxMovementComponent::UVeloxMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWantsJumpHeld = false;

	SetNetworkMoveDataContainer(VeloxNetworkMoveDataContainer);
}

const UVeloxMovementSettings* UVeloxMovementComponent::GetMovementSettings() const
{
	return MovementSettings;
}

void UVeloxMovementComponent::SetMovementSettings(const UVeloxMovementSettings* InMovementSettings)
{
	MovementSettings = InMovementSettings;
}

FNetworkPredictionData_Client* UVeloxMovementComponent::GetPredictionData_Client() const
{
	if (ClientPredictionData == nullptr)
	{
		// Same lazy-allocation pattern as UCharacterMovementComponent; the base class owns and deletes it.
		UVeloxMovementComponent* MutableThis = const_cast<UVeloxMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_Velox(*this);
	}

	return ClientPredictionData;
}

void UVeloxMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);

	bWantsJumpHeld = (Flags & FSavedMove_Velox::FLAG_JumpHeld) != 0;
}
