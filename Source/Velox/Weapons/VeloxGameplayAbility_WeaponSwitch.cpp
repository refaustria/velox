// Velox - weapon switch ability.

#include "Weapons/VeloxGameplayAbility_WeaponSwitch.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayAbility_WeaponSwitch)

UVeloxGameplayAbility_WeaponSwitch::UVeloxGameplayAbility_WeaponSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationPolicy = EVeloxAbilityActivationPolicy::OnInputTriggered;
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VeloxGameplayTags::Ability_Weapon_Switch);
	SetAssetTags(AssetTags);
	ActivationBlockedTags.AddTag(VeloxGameplayTags::State_Dead);
}

EVeloxInputActivation UVeloxGameplayAbility_WeaponSwitch::PrepareInputActivation(const FGameplayTag& InputTag, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayEventData& OutEventData) const
{
	const UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	const int32 Index = Inventory ? Inventory->ResolveSelectInput(InputTag) : INDEX_NONE;
	if (Index == INDEX_NONE || Index == Inventory->GetCurrentIndex())
	{
		return EVeloxInputActivation::Skip;
	}
	OutEventData.EventTag = InputTag;
	OutEventData.EventMagnitude = static_cast<float>(Index);
	return EVeloxInputActivation::ActivateWithEvent;
}

double UVeloxGameplayAbility_WeaponSwitch::RefireWait(const FGameplayAbilityActorInfo* ActorInfo, bool bSwitchWaitsForRefire)
{
	const UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!bSwitchWaitsForRefire || !Inventory || !Avatar || !Avatar->GetWorld())
	{
		return 0.0;
	}
	const FGameplayAbilitySpec* Spec = Inventory->FindFireSpec(Inventory->GetCurrentIndex());
	const UVeloxGameplayAbility_Fire* Fire = Spec ? Cast<UVeloxGameplayAbility_Fire>(Spec->GetPrimaryInstance()) : nullptr;
	return Fire ? FMath::Max(Fire->GetNextFireTime() - Avatar->GetWorld()->GetTimeSeconds(), 0.0) : 0.0;
}

void UVeloxGameplayAbility_WeaponSwitch::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	TargetIndex = TriggerEventData ? FMath::RoundToInt32(TriggerEventData->EventMagnitude) : INDEX_NONE;
	// Server validation: owned (ammo is not required, Q3) and a real change.
	if (!Inventory || !ASC || !Inventory->OwnsIndex(TargetIndex) || TargetIndex == Inventory->GetCurrentIndex() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxWeaponSwitch rejected target=%d current=%d"), TargetIndex, Inventory ? Inventory->GetCurrentIndex() : -1);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory->GetCombatSettings());
	const double Wait = RefireWait(ActorInfo, Settings.bSwitchWaitsForRefire);
	const float Drop = FMath::Max(Settings.WeaponDropTime, 0.f);
	const float Raise = FMath::Max(Settings.WeaponRaiseTime, 0.f);
	if (ActorInfo->IsNetAuthority())
	{
		Inventory->SetPendingIndex(TargetIndex);
	}

	// One predicted GE for the whole switch (applied inside the activation's prediction window).
	const float Duration = static_cast<float>(Wait) + Drop + Raise;
	if (Duration > 0.f)
	{
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, VeloxEffects::MakeDurationSpec(ASC, UVeloxGE_WeaponSwitching::StaticClass(), Duration));
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch %s -> %s wait=%.3f drop=%.2f raise=%.2f role=%s"), *GetNameSafe(Inventory->GetCurrentDefinition()),
		*GetNameSafe(Inventory->GetDefinition(TargetIndex)), Wait, Drop, Raise, ActorInfo->IsNetAuthority() ? TEXT("server") : TEXT("client"));

	const float DropEnd = static_cast<float>(Wait) + Drop;
	if (DropEnd <= 0.f)
	{
		OnDropFinished();
		return;
	}
	UAbilityTask_WaitDelay* Task = UAbilityTask_WaitDelay::WaitDelay(this, DropEnd);
	Task->OnFinish.AddDynamic(this, &ThisClass::OnDropFinished);
	Task->ReadyForActivation();
}

void UVeloxGameplayAbility_WeaponSwitch::OnDropFinished()
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	if (UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr)
	{
		if (ActorInfo->IsNetAuthority())
		{
			Inventory->SetCurrentIndex(TargetIndex);
		}
		else
		{
			Inventory->PredictCurrentIndex(TargetIndex);
		}
	}
	// Each side ends at its own drop end: a replicated end could reach the server before its own timer and cancel it.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
}
