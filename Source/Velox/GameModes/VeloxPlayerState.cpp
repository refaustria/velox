// Velox - player state.

#include "GameModes/VeloxPlayerState.h"

#include "Combat/VeloxAbilitySystemComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxPlayerState)

AVeloxPlayerState::AVeloxPlayerState(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemComponent = ObjectInitializer.CreateDefaultSubobject<UVeloxAbilitySystemComponent>(this, TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

void AVeloxPlayerState::PostInitProperties()
{
	Super::PostInitProperties();

	// Config values are available here (loaded into the CDO, copied to instances).
	if (ConfigNetUpdateFrequency > 0.0f)
	{
		SetNetUpdateFrequency(ConfigNetUpdateFrequency);
	}
}

UAbilitySystemComponent* AVeloxPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

UVeloxAbilitySystemComponent* AVeloxPlayerState::GetVeloxAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}
