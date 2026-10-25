// Velox - fire ability base.

#include "Weapons/VeloxGameplayAbility_Fire.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayCueInterface.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayAbility_Fire)

UVeloxGameplayAbility_Fire::UVeloxGameplayAbility_Fire(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationPolicy = EVeloxAbilityActivationPolicy::WhileInputActive;
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VeloxGameplayTags::Ability_Weapon_Fire);
	SetAssetTags(AssetTags);
	ActivationBlockedTags.AddTag(VeloxGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(VeloxGameplayTags::State_WeaponSwitching);
	// Safety net: a server instance still waiting for target data never blocks the next activation.
	bRetriggerInstancedAbility = true;
}

const UVeloxWeaponDefinition* UVeloxGameplayAbility_Fire::GetDefinition(const FGameplayAbilitySpec* Spec)
{
	return Spec ? Cast<UVeloxWeaponDefinition>(Spec->SourceObject.Get()) : nullptr;
}

const UVeloxWeaponDefinition* UVeloxGameplayAbility_Fire::GetCurrentDefinition() const
{
	return Cast<UVeloxWeaponDefinition>(GetCurrentSourceObject());
}

bool UVeloxGameplayAbility_Fire::IsServerForRemoteShooter(const FGameplayAbilityActorInfo* ActorInfo)
{
	return ActorInfo && ActorInfo->IsNetAuthority() && !ActorInfo->IsLocallyControlled();
}

APawn* UVeloxGameplayAbility_Fire::GetShooterPawn() const
{
	return CurrentActorInfo ? Cast<APawn>(CurrentActorInfo->AvatarActor.Get()) : nullptr;
}

AController* UVeloxGameplayAbility_Fire::GetShooterController() const
{
	if (!CurrentActorInfo)
	{
		return nullptr;
	}
	if (AController* PC = CurrentActorInfo->PlayerController.Get())
	{
		return PC;
	}
	const APawn* Pawn = GetShooterPawn();
	return Pawn ? Pawn->GetController() : nullptr;
}

UVeloxWeaponInventoryComponent* UVeloxGameplayAbility_Fire::GetInventory() const
{
	return CurrentActorInfo ? UVeloxWeaponInventoryComponent::Find(CurrentActorInfo->OwnerActor.Get()) : nullptr;
}

bool UVeloxGameplayAbility_Fire::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	const UVeloxWeaponDefinition* Definition = GetDefinition(ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr);
	const UVeloxWeaponInventoryComponent* Inventory = UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get());
	if (!Definition || !Inventory)
	{
		return false;
	}

	const bool bRemote = IsServerForRemoteShooter(ActorInfo);
	// The current weapon only; the server also accepts the target of a running switch (its timers run late, see above).
	if (Definition != Inventory->GetCurrentDefinition() && !(bRemote && Definition == Inventory->GetPendingDefinition()))
	{
		return false;
	}
	if (!bRemote)
	{
		const UWorld* World = ActorInfo->AvatarActor.IsValid() ? ActorInfo->AvatarActor->GetWorld() : nullptr;
		if (!World || World->GetTimeSeconds() < NextFireTime)
		{
			return false;
		}
	}
	return true;
}

bool UVeloxGameplayAbility_Fire::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const UVeloxWeaponDefinition* Definition = GetDefinition(ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr);
	if (!Definition)
	{
		return false;
	}
	if (Definition->AmmoPerShot <= 0)
	{
		return true;
	}
	const UVeloxWeaponInventoryComponent* Inventory = UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get());
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory ? Inventory->GetCombatSettings() : nullptr);
	const FGameplayAttribute Attribute = UVeloxAmmoSet::GetAmmoAttribute(Definition->AmmoTag, Settings);
	const bool bAfford = Attribute.IsValid() && UVeloxAmmoSet::CanAfford(ASC->GetNumericAttribute(Attribute), Definition->AmmoPerShot);
	if (!bAfford && OptionalRelevantTags)
	{
		OptionalRelevantTags->AddTag(UAbilitySystemGlobals::Get().ActivateFailCostTag);
	}
	return bAfford;
}

bool UVeloxGameplayAbility_Fire::DoesAbilitySatisfyTagRequirements(const UAbilitySystemComponent& AbilitySystemComponent, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const FGameplayAbilityActorInfo* ActorInfo = AbilitySystemComponent.AbilityActorInfo.Get();
	if (!IsServerForRemoteShooter(ActorInfo) || !AbilitySystemComponent.HasMatchingGameplayTag(VeloxGameplayTags::State_WeaponSwitching))
	{
		return Super::DoesAbilitySatisfyTagRequirements(AbilitySystemComponent, SourceTags, TargetTags, OptionalRelevantTags);
	}
	// Server, remote shooter, switching: the same checks without the WeaponSwitching block.
	FGameplayTagContainer Blocked = ActivationBlockedTags;
	Blocked.RemoveTag(VeloxGameplayTags::State_WeaponSwitching);
	const bool bBlocked = AbilitySystemComponent.AreAbilityTagsBlocked(GetAssetTags()) || AbilitySystemComponent.GetOwnedGameplayTags().HasAny(Blocked)
		|| (!ActivationRequiredTags.IsEmpty() && !AbilitySystemComponent.GetOwnedGameplayTags().HasAll(ActivationRequiredTags));
	if (bBlocked && OptionalRelevantTags)
	{
		OptionalRelevantTags->AddTag(UAbilitySystemGlobals::Get().ActivateFailTagsBlockedTag);
	}
	return !bBlocked;
}

void UVeloxGameplayAbility_Fire::AdvanceNextFireTime(double Now, float Interval)
{
	// Fired within one interval of the scheduled time: keep the schedule (exact long-term rate); otherwise restart.
	NextFireTime = (Now - NextFireTime < Interval) ? NextFireTime + Interval : Now + Interval;
}

bool UVeloxGameplayAbility_Fire::ConsumeServerFireToken(double Now, const UVeloxWeaponDefinition& Definition)
{
	const UVeloxWeaponInventoryComponent* Inventory = GetInventory();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory ? Inventory->GetCombatSettings() : nullptr);
	return ServerBucket.TryConsume(Now, Definition.GetSafeFireInterval(), Settings.FireRateBurstAllowance);
}

void UVeloxGameplayAbility_Fire::ApplyAmmoCost(const UVeloxWeaponDefinition& Definition) const
{
	if (Definition.AmmoPerShot <= 0 || !Definition.AmmoTag.IsValid() || !CurrentActorInfo)
	{
		return;
	}
	const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeAmmoCostSpec(CurrentActorInfo->AbilitySystemComponent.Get(), Definition.AmmoTag, Definition.AmmoPerShot);
	if (Spec.IsValid())
	{
		ApplyGameplayEffectSpecToOwner(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, Spec);
	}
}

FGameplayCueParameters UVeloxGameplayAbility_Fire::MakeCueParameters(const FVector& Start, const FVector& End, const FVector& Normal, uint8 ShotSeq) const
{
	FGameplayCueParameters Params;
	Params.Location = End;
	Params.Normal = Normal;
	AActor* Avatar = CurrentActorInfo ? CurrentActorInfo->AvatarActor.Get() : nullptr;
	Params.Instigator = Avatar;
	Params.EffectCauser = Avatar;
	if (const UAbilitySystemComponent* ASC = CurrentActorInfo ? CurrentActorInfo->AbilitySystemComponent.Get() : nullptr)
	{
		Params.EffectContext = ASC->MakeEffectContext();
		Params.EffectContext.AddOrigin(Start);
		if (FVeloxGameplayEffectContext* Context = FVeloxGameplayEffectContext::Get(Params.EffectContext))
		{
			Context->ShotSeq = ShotSeq;
			Context->HitLocation = End;
		}
	}
	return Params;
}
