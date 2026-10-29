// Velox - health component.

#include "Combat/VeloxHealthComponent.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxHealthSet.h"
#include "GameplayEffectExtension.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxHealthComponent)

namespace VeloxHealthComponentPrivate
{
	AActor* InstigatorOf(const FOnAttributeChangeData& Data)
	{
		return Data.GEModData ? Data.GEModData->EffectSpec.GetEffectContext().GetOriginalInstigator() : nullptr;
	}
}

UVeloxHealthComponent::UVeloxHealthComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

UVeloxHealthComponent* UVeloxHealthComponent::FindHealthComponent(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVeloxHealthComponent>() : nullptr;
}

void UVeloxHealthComponent::InitializeWithAbilitySystem(UVeloxAbilitySystemComponent* InASC)
{
	if (InASC == AbilitySystemComponent)
	{
		return;
	}
	UninitializeFromAbilitySystem();
	if (!InASC)
	{
		return;
	}

	const UVeloxHealthSet* Set = InASC->GetSet<UVeloxHealthSet>();
	if (!Set)
	{
		UE_LOG(LogVeloxCombat, Error, TEXT("%s: %s has no UVeloxHealthSet."), *GetNameSafe(GetOwner()), *GetNameSafe(InASC));
		return;
	}

	AbilitySystemComponent = InASC;
	HealthSet = Set;
	bDeathStarted = false;
	bDeathFinished = false;
	HealthChangedHandle = InASC->GetGameplayAttributeValueChangeDelegate(UVeloxHealthSet::GetHealthAttribute()).AddUObject(this, &ThisClass::HandleHealthChanged);
	ArmorChangedHandle = InASC->GetGameplayAttributeValueChangeDelegate(UVeloxHealthSet::GetArmorAttribute()).AddUObject(this, &ThisClass::HandleArmorChanged);
	DamagedHandle = Set->OnDamaged.AddUObject(this, &ThisClass::HandleDamaged);
	OutOfHealthHandle = Set->OnOutOfHealth.AddUObject(this, &ThisClass::HandleOutOfHealth);

	// Late joiners / respawn: push the current values once.
	OnHealthChanged.Broadcast(this, Set->GetHealth(), Set->GetHealth(), nullptr);
	OnArmorChanged.Broadcast(this, Set->GetArmor(), Set->GetArmor(), nullptr);
}

void UVeloxHealthComponent::UninitializeFromAbilitySystem()
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UVeloxHealthSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(UVeloxHealthSet::GetArmorAttribute()).Remove(ArmorChangedHandle);
	}
	if (HealthSet)
	{
		HealthSet->OnDamaged.Remove(DamagedHandle);
		HealthSet->OnOutOfHealth.Remove(OutOfHealthHandle);
	}
	HealthChangedHandle.Reset();
	ArmorChangedHandle.Reset();
	DamagedHandle.Reset();
	OutOfHealthHandle.Reset();
	AbilitySystemComponent = nullptr;
	HealthSet = nullptr;
}

void UVeloxHealthComponent::OnUnregister()
{
	UninitializeFromAbilitySystem();
	Super::OnUnregister();
}

float UVeloxHealthComponent::GetHealth() const
{
	return HealthSet ? HealthSet->GetHealth() : 0.f;
}

float UVeloxHealthComponent::GetMaxHealth() const
{
	return HealthSet ? HealthSet->GetMaxHealth() : 0.f;
}

float UVeloxHealthComponent::GetArmor() const
{
	return HealthSet ? HealthSet->GetArmor() : 0.f;
}

float UVeloxHealthComponent::GetMaxArmor() const
{
	return HealthSet ? HealthSet->GetMaxArmor() : 0.f;
}

void UVeloxHealthComponent::StartDeath()
{
	if (bDeathStarted)
	{
		return;
	}
	bDeathStarted = true;
	OnDeathStarted.Broadcast(GetOwner());
}

void UVeloxHealthComponent::FinishDeath()
{
	if (!bDeathStarted || bDeathFinished)
	{
		return;
	}
	bDeathFinished = true;
	OnDeathFinished.Broadcast(GetOwner());
}

void UVeloxHealthComponent::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	OnHealthChanged.Broadcast(this, Data.OldValue, Data.NewValue, VeloxHealthComponentPrivate::InstigatorOf(Data));
}

void UVeloxHealthComponent::HandleArmorChanged(const FOnAttributeChangeData& Data)
{
	OnArmorChanged.Broadcast(this, Data.OldValue, Data.NewValue, VeloxHealthComponentPrivate::InstigatorOf(Data));
}

void UVeloxHealthComponent::HandleDamaged(const FVeloxDamageEvent& Event)
{
	OnDamaged.Broadcast(Event);
}

void UVeloxHealthComponent::HandleOutOfHealth(const FVeloxDamageEvent& Event)
{
	UE_LOG(LogVeloxCombat, Log, TEXT("%s: out of health (instigator %s)"), *GetNameSafe(GetOwner()), *GetNameSafe(Event.Instigator.Get()));
	OnOutOfHealth.Broadcast(Event);
}
