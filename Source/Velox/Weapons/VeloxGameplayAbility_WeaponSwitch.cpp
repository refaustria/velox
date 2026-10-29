// Velox - weapon switch ability.

#include "Weapons/VeloxGameplayAbility_WeaponSwitch.h"

#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "Weapons/VeloxFireTiming.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"
#include "Weapons/VeloxWeaponTable.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayAbility_WeaponSwitch)

UVeloxGameplayAbility_WeaponSwitch::UVeloxGameplayAbility_WeaponSwitch(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationPolicy = EVeloxAbilityActivationPolicy::OnInputTriggered;
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VeloxGameplayTags::Ability_Weapon_Switch);
	SetAssetTags(AssetTags);
	ActivationBlockedTags.AddTag(VeloxGameplayTags::State_Dead);
	// D4: on the server a new switch ends a running one (its drop end lags the client's by the latency) instead of being
	// rejected. The superseded instance's EndAbility only resets SwitchEndTime, which the new activation sets again; its
	// pending switch is completed or the new activation rejected at the top of ActivateAbility (B33); after a rejection
	// the inventory's drop timer still completes the superseded switch (B37).
	bRetriggerInstancedAbility = true;
}

void UVeloxGameplayAbility_WeaponSwitch::CompleteSwitchEarly()
{
	bSwitchTimingPending = false;
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
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
	// D4: retriggering is for the server only; locally a press during the own drop stays ignored (pre-08b behaviour).
	const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
	const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromClass(GetClass()) : nullptr;
	if (Spec && Spec->IsActive())
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
	if (!Fire)
	{
		return 0.0;
	}
	const double Now = Avatar->GetWorld()->GetTimeSeconds();
	if (UVeloxGameplayAbility_Fire::IsServerForRemoteShooter(ActorInfo))
	{
		// B37: no receive-time NextFireTime on the server; one interval from the last accepted receive time at most.
		const UVeloxWeaponDefinition* Definition = UVeloxGameplayAbility_Fire::GetDefinition(Spec);
		return Definition ? VeloxFireTiming::RemoteRefireWait(Fire->GetLastAcceptedReceiveTime(), Definition->GetSafeFireInterval(), Now) : 0.0;
	}
	return FMath::Max(Fire->GetNextFireTime() - Now, 0.0);
}

void UVeloxGameplayAbility_WeaponSwitch::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	// The index arrives as a float from the client: anything but a finite value in the table's range is no index (nit 11).
	const float Magnitude = TriggerEventData ? TriggerEventData->EventMagnitude : -1.f;
	TargetIndex = (FMath::IsFinite(Magnitude) && Magnitude >= 0.f && Magnitude < static_cast<float>(UVeloxWeaponTable::MaxWeapons)) ? FMath::RoundToInt32(Magnitude) : INDEX_NONE;
	bSwitchTimingPending = false;
	// ADR 0003 B33 (server): a switch still pending here was superseded by this activation (bRetriggerInstancedAbility:
	// the engine has already ended its instance). If its drop has ended within the resend tolerance (remaining <= Raise +
	// FireRateJitterToleranceSeconds, B37), which is the only state in which an honest client can have sent this press
	// (its own instance ended at its drop end), its inventory part is completed first, so the validation below sees the
	// weapon the client sees. Otherwise the press came during the drop: rejected (pre-08b outcome); the superseded switch
	// still completes at its drop end through the inventory's own timer (B37 iii, SetPendingIndex).
	if (Inventory && ASC && ActorInfo->IsNetAuthority() && Inventory->GetPendingIndex() != INDEX_NONE)
	{
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory->GetCombatSettings());
		const double Remaining = UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC);
		const double Limit = static_cast<double>(FMath::Max(Settings.WeaponRaiseTime, 0.f)) + Settings.GetFireRateJitterToleranceSeconds();
		if (Remaining > Limit + 1e-4)
		{
			UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxWeaponSwitch supersede rejected target=%d current=%d pending=%d remaining=%.3f limit=%.3f"), TargetIndex,
				Inventory->GetCurrentIndex(), Inventory->GetPendingIndex(), Remaining, Limit);
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch supersede completes pending=%d (current=%d) remaining=%.3f limit=%.3f target=%d"), Inventory->GetPendingIndex(),
			Inventory->GetCurrentIndex(), Remaining, Limit, TargetIndex);
		Inventory->CompletePendingSwitchIndexOnly();
	}
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

	// One predicted GE for the whole switch (applied inside the activation's prediction window).
	const float Duration = static_cast<float>(Wait) + Drop + Raise;
	// B18: this machine's own switch end (local world time); the fire abilities of the shooter's machine gate on it.
	if (const UWorld* World = GetWorld())
	{
		Inventory->SetSwitchEndTime(World->GetTimeSeconds() + Duration);
		bSwitchTimingPending = true;
	}
	if (Duration > 0.f)
	{
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, VeloxEffects::MakeDurationSpec(ASC, UVeloxGE_WeaponSwitching::StaticClass(), Duration));
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch %s -> %s wait=%.3f drop=%.2f raise=%.2f role=%s"), *GetNameSafe(Inventory->GetCurrentDefinition()),
		*GetNameSafe(Inventory->GetDefinition(TargetIndex)), Wait, Drop, Raise, ActorInfo->IsNetAuthority() ? TEXT("server") : TEXT("client"));

	const float DropEnd = static_cast<float>(Wait) + Drop;
	if (ActorInfo->IsNetAuthority())
	{
		// B37 (iii): the inventory owns the server's drop completion (a timer of its own), so a superseding activation
		// that ends this instance cannot orphan the pending switch. DropEnd <= 0 completes it at once.
		Inventory->SetPendingIndex(TargetIndex, DropEnd);
	}
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
	bSwitchTimingPending = false;
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	// Authority: nothing to do for the index, the inventory's PendingDropTimer (armed by SetPendingIndex with the same
	// delay) completes it (B37 iii). Owning client: the predicted index.
	UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	if (Inventory && !ActorInfo->IsNetAuthority())
	{
		Inventory->PredictCurrentIndex(TargetIndex);
	}
	// Each side ends at its own drop end: a replicated end could reach the server before its own timer and cancel it.
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
}

void UVeloxGameplayAbility_WeaponSwitch::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bSwitchTimingPending)
	{
		// Rejected by the server (ClientActivateAbilityFailed ends the instance) or cancelled before the drop end: the
		// weapon did not change, so this machine must not stay blocked until the switch end it predicted.
		bSwitchTimingPending = false;
		if (UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr)
		{
			Inventory->SetSwitchEndTime(0.0);
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
