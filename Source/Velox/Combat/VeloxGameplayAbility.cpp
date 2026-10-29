// Velox - gameplay ability base.

#include "Combat/VeloxGameplayAbility.h"

#include "AbilitySystemComponent.h"
#include "Combat/VeloxAbilitySystemComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayAbility)

UVeloxGameplayAbility::UVeloxGameplayAbility(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateNo;
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::LocalPredicted;
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ClientOrServer;
	bServerRespectsRemoteAbilityCancellation = false;
}

UVeloxAbilitySystemComponent* UVeloxGameplayAbility::GetVeloxAbilitySystemComponentFromActorInfo() const
{
	return CurrentActorInfo ? Cast<UVeloxAbilitySystemComponent>(CurrentActorInfo->AbilitySystemComponent.Get()) : nullptr;
}

void UVeloxGameplayAbility::TryActivateAbilityOnSpawn(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec) const
{
	if (!ActorInfo || Spec.IsActive() || ActivationPolicy != EVeloxAbilityActivationPolicy::OnSpawn)
	{
		return;
	}
	UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	const AActor* Avatar = ActorInfo->AvatarActor.Get();
	if (!ASC || !Avatar || Avatar->GetTearOff() || Avatar->GetLifeSpan() > 0.f)
	{
		return;
	}

	const bool bLocalExecution = NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::LocalPredicted || NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::LocalOnly;
	const bool bServerExecution = NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::ServerOnly || NetExecutionPolicy == EGameplayAbilityNetExecutionPolicy::ServerInitiated;
	const bool bClientShouldActivate = ActorInfo->IsLocallyControlled() && bLocalExecution;
	const bool bServerShouldActivate = ActorInfo->IsNetAuthority() && bServerExecution;
	if (bClientShouldActivate || bServerShouldActivate)
	{
		ASC->TryActivateAbility(Spec.Handle);
	}
}

void UVeloxGameplayAbility::OnGiveAbility(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnGiveAbility(ActorInfo, Spec);
	TryActivateAbilityOnSpawn(ActorInfo, Spec);
}

void UVeloxGameplayAbility::OnAvatarSet(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilitySpec& Spec)
{
	Super::OnAvatarSet(ActorInfo, Spec);
	TryActivateAbilityOnSpawn(ActorInfo, Spec);
}
