// Velox - health component.

#include "Combat/VeloxHealthComponent.h"

#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxHazardRules.h"
#include "Combat/VeloxHealthSet.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameModes/VeloxPlayerState.h"
#include "GameplayEffectExtension.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxHealthComponent)

namespace VeloxHealthComponentPrivate
{
	AActor* InstigatorOf(const FOnAttributeChangeData& Data)
	{
		return Data.GEModData ? Data.GEModData->EffectSpec.GetEffectContext().GetOriginalInstigator() : nullptr;
	}

	/** The instigator as a player state: the ASC owner (player state), or a pawn's player state (same rule as the death path). */
	AVeloxPlayerState* PlayerStateOf(AActor* Instigator)
	{
		if (AVeloxPlayerState* PS = Cast<AVeloxPlayerState>(Instigator))
		{
			return PS;
		}
		const APawn* Pawn = Cast<APawn>(Instigator);
		return Pawn ? Pawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
	}

	/** First Velox.Damage.<Type> tag of the event other than Velox.Damage.Self. */
	FGameplayTag DamageTypeOf(const FGameplayTagContainer& Tags)
	{
		static const FGameplayTag DamageRoot = FGameplayTag::RequestGameplayTag(TEXT("Velox.Damage"), false);
		for (const FGameplayTag& Tag : Tags)
		{
			if (Tag != VeloxGameplayTags::Damage_Self && DamageRoot.IsValid() && Tag.MatchesTag(DamageRoot))
			{
				return Tag;
			}
		}
		return FGameplayTag();
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
	LastAttacker.Reset();
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
	LastAttacker.Reset();
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
	// Before the broadcast, so a listener that resolves a death or a hazard credit already sees this hit.
	if (const UWorld* World = GetWorld())
	{
		NoteDamageForAttackerCredit(Event, World->GetTimeSeconds());
	}
	OnDamaged.Broadcast(Event);
}

bool UVeloxHealthComponent::NoteDamageForAttackerCredit(const FVeloxDamageEvent& Event, double Now)
{
	const AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
	{
		return false;
	}

	const AVeloxPlayerState* VictimPS = AbilitySystemComponent ? Cast<AVeloxPlayerState>(AbilitySystemComponent->GetOwnerActor()) : nullptr;
	if (!VictimPS)
	{
		const APawn* OwnerPawn = Cast<APawn>(Owner);
		VictimPS = OwnerPawn ? OwnerPawn->GetPlayerState<AVeloxPlayerState>() : nullptr;
	}
	AVeloxPlayerState* AttackerPS = VeloxHealthComponentPrivate::PlayerStateOf(Event.Instigator.Get());
	const bool bOtherPlayer = AttackerPS && AttackerPS != VictimPS;

	if (!VeloxHazardRules::ShouldRecordAttacker(bOtherPlayer, Event.DamageTags, Event.HealthLoss, Event.ArmorLoss, !Event.Knockback.IsNearlyZero()))
	{
		return false;
	}

	LastAttacker.Attacker = AttackerPS;
	LastAttacker.ServerTime = Now;
	LastAttacker.DamageTag = VeloxHealthComponentPrivate::DamageTypeOf(Event.DamageTags);
	UE_LOG(LogVeloxCombat, Verbose, TEXT("%s: last attacker %s (%s) at %.2f"), *GetNameSafe(Owner), *GetNameSafe(AttackerPS),
		*LastAttacker.DamageTag.ToString(), Now);
	return true;
}

AVeloxPlayerState* UVeloxHealthComponent::GetCreditedAttacker(double Now, float WindowSeconds) const
{
	AVeloxPlayerState* Attacker = LastAttacker.Attacker.Get();
	return VeloxHazardRules::IsCredited(LastAttacker.HasRecord(), Attacker != nullptr, LastAttacker.ServerTime, Now, WindowSeconds) ? Attacker : nullptr;
}

void UVeloxHealthComponent::HandleOutOfHealth(const FVeloxDamageEvent& Event)
{
	UE_LOG(LogVeloxCombat, Log, TEXT("%s: out of health (instigator %s)"), *GetNameSafe(GetOwner()), *GetNameSafe(Event.Instigator.Get()));
	OnOutOfHealth.Broadcast(Event);
}
