// Velox - weapon switch ability.

#include "Weapons/VeloxGameplayAbility_WeaponSwitch.h"

#include "AbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameplayPrediction.h"
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
	// D4: an activation must never be refused because an instance is still active. Since B42 the instance ends at
	// activation (the inventory owns the drop), so this only matters for an activation inside another one's frame.
	bRetriggerInstancedAbility = true;
}

void UVeloxGameplayAbility_WeaponSwitch::CompleteSwitchEarly()
{
	if (IsActive())
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, false, false);
	}
}

float UVeloxGameplayAbility_WeaponSwitch::EncodeSwitchEvent(int32 Index, bool bRetarget)
{
	return static_cast<float>(Index + (bRetarget ? UVeloxWeaponTable::MaxWeapons : 0));
}

bool UVeloxGameplayAbility_WeaponSwitch::DecodeSwitchEvent(float Magnitude, int32& OutIndex, bool& bOutRetarget)
{
	OutIndex = INDEX_NONE;
	bOutRetarget = false;
	if (!FMath::IsFinite(Magnitude) || Magnitude < 0.f || Magnitude >= static_cast<float>(2 * UVeloxWeaponTable::MaxWeapons))
	{
		return false;
	}
	const int32 Encoded = FMath::RoundToInt32(Magnitude);
	if (Encoded < 0 || Encoded >= 2 * UVeloxWeaponTable::MaxWeapons)
	{
		return false;
	}
	bOutRetarget = Encoded >= UVeloxWeaponTable::MaxWeapons;
	OutIndex = Encoded % UVeloxWeaponTable::MaxWeapons;
	return true;
}

EVeloxInputActivation UVeloxGameplayAbility_WeaponSwitch::PrepareInputActivation(const FGameplayTag& InputTag, const FGameplayAbilityActorInfo* ActorInfo,
	FGameplayEventData& OutEventData) const
{
	const UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	// Next/Prev cycle from the pending target during a drop (ResolveSelectInput).
	const int32 Index = Inventory ? Inventory->ResolveSelectInput(InputTag) : INDEX_NONE;
	if (Index == INDEX_NONE)
	{
		return EVeloxInputActivation::Skip; // unowned or no weapon in that slot
	}
	// B42: during the own wait/drop a press retargets the switch (also back to the weapon being lowered); the running
	// target again is a no-op. Outside a drop (also during a raise) the current weapon again is a no-op.
	const int32 Pending = Inventory->GetPendingIndex();
	const bool bRetarget = Pending != INDEX_NONE;
	if (bRetarget ? Index == Pending : Index == Inventory->GetCurrentIndex())
	{
		return EVeloxInputActivation::Skip;
	}
	OutEventData.EventTag = InputTag;
	OutEventData.EventMagnitude = EncodeSwitchEvent(Index, bRetarget);
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
		// B37: no receive-time NextFireTime on the server; one interval from the last accepted receive time at most. The
		// interval is the Haste-scaled one the bucket uses (ADR 0004 section 3).
		const UVeloxWeaponDefinition* Definition = UVeloxGameplayAbility_Fire::GetDefinition(Spec);
		return Definition ? VeloxFireTiming::RemoteRefireWait(Fire->GetLastAcceptedReceiveTime(), Fire->GetHasteFireInterval(ActorInfo, Definition->GetSafeFireInterval()), Now) : 0.0;
	}
	return FMath::Max(Fire->GetNextFireTime() - Now, 0.0);
}

double UVeloxGameplayAbility_WeaponSwitch::RunningSwitchRemaining(const FGameplayAbilityActorInfo* ActorInfo)
{
	const UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const UWorld* World = Avatar ? Avatar->GetWorld() : (Inventory ? Inventory->GetWorld() : nullptr);
	if (!Inventory || !World)
	{
		return 0.0;
	}
	if (UVeloxGameplayAbility_Fire::IsServerForRemoteShooter(ActorInfo))
	{
		return UVeloxGameplayAbility_Fire::GetSwitchRemaining(ActorInfo->AbilitySystemComponent.Get());
	}
	return Inventory->GetSwitchEndTime() > 0.0 ? FMath::Max(Inventory->GetSwitchEndTime() - World->GetTimeSeconds(), 0.0) : 0.0;
}

bool UVeloxGameplayAbility_WeaponSwitch::ApplyRetarget(const FGameplayAbilityActorInfo* ActorInfo, UVeloxWeaponInventoryComponent& Inventory, int32 Target) const
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const TCHAR* Role = ActorInfo->IsNetAuthority() ? TEXT("server") : TEXT("client");
	const int32 From = Inventory.GetPendingIndex();
	if (!UVeloxGameplayAbility_Fire::IsServerForRemoteShooter(ActorInfo))
	{
		// Shooter's machine (owning client, listen host, bot): PrepareInputActivation saw the pending switch this frame.
		if (From == INDEX_NONE || !Inventory.RetargetPendingIndex(Target))
		{
			return false;
		}
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch retarget %s -> %s mode=local drop_left=%.3f switch_left=%.3f role=%s"), *GetNameSafe(Inventory.GetDefinition(From)),
			*GetNameSafe(Inventory.GetDefinition(Target)), Inventory.GetPendingDropRemaining(), RunningSwitchRemaining(ActorInfo), Role);
		return true;
	}
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory.GetCombatSettings());
	const double Tolerance = Settings.GetFireRateJitterToleranceSeconds();
	switch (VeloxFireTiming::ClassifyRemoteRetarget(From != INDEX_NONE, Now, Inventory.GetLastDropEndTime(), Tolerance))
	{
	case VeloxFireTiming::ERemoteRetarget::Pending:
		Inventory.RetargetPendingIndex(Target);
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch retarget %s -> %s mode=pending drop_left=%.3f switch_left=%.3f role=%s"), *GetNameSafe(Inventory.GetDefinition(From)),
			*GetNameSafe(Inventory.GetDefinition(Target)), Inventory.GetPendingDropRemaining(), RunningSwitchRemaining(ActorInfo), Role);
		return true;
	case VeloxFireTiming::ERemoteRetarget::Late:
	{
		// The server's drop ended within the resend tolerance before the press arrived: the raise runs on for Target.
		const int32 Current = Inventory.GetAuthoritativeIndex();
		Inventory.SetCurrentIndex(Target);
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch retarget %s -> %s mode=late since_drop=%.3f switch_left=%.3f role=%s"), *GetNameSafe(Inventory.GetDefinition(Current)),
			*GetNameSafe(Inventory.GetDefinition(Target)), Now - Inventory.GetLastDropEndTime(), RunningSwitchRemaining(ActorInfo), Role);
		return true;
	}
	case VeloxFireTiming::ERemoteRetarget::Fresh:
	default:
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch retarget -> %s mode=fresh since_drop=%.3f tol=%.3f role=%s"), *GetNameSafe(Inventory.GetDefinition(Target)),
			Inventory.GetLastDropEndTime() >= 0.0 ? Now - Inventory.GetLastDropEndTime() : -1.0, Tolerance, Role);
		return false;
	}
}

void UVeloxGameplayAbility_WeaponSwitch::BindLocalRejection(const FGameplayAbilityActivationInfo& ActivationInfo, UVeloxWeaponInventoryComponent& Inventory) const
{
	const FPredictionKey& Key = ActivationInfo.GetActivationPredictionKey();
	if (!Key.IsValidKey() || !Key.IsLocalClientKey())
	{
		return;
	}
	const int16 KeyValue = Key.Current;
	Inventory.NoteLocalSwitch(KeyValue);
	// The instance has ended long before a rejection arrives (ClientActivateAbilityFailed would end an active one): the
	// local switch state lives in the inventory and is rolled back there.
	TWeakObjectPtr<UVeloxWeaponInventoryComponent> WeakInventory(&Inventory);
	FPredictionKey KeyCopy = Key;
	KeyCopy.NewRejectedDelegate().BindLambda([WeakInventory, KeyValue]()
	{
		if (UVeloxWeaponInventoryComponent* Inv = WeakInventory.Get())
		{
			Inv->CancelLocalSwitch(KeyValue);
		}
	});
}

void UVeloxGameplayAbility_WeaponSwitch::RejectActivation(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo& ActivationInfo, UVeloxWeaponInventoryComponent* Inventory)
{
	// B42: the activation was already confirmed to the client; tell its inventory to roll the predicted switch back.
	if (Inventory && UVeloxGameplayAbility_Fire::IsServerForRemoteShooter(ActorInfo) && ActivationInfo.GetActivationPredictionKey().IsValidKey())
	{
		Inventory->ClientRejectSwitch(ActivationInfo.GetActivationPredictionKey().Current);
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
}

void UVeloxGameplayAbility_WeaponSwitch::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	// The index arrives as a float from the client: anything but a finite encoded index is no index (nit 11).
	bool bRetarget = false;
	int32 Target = INDEX_NONE;
	DecodeSwitchEvent(TriggerEventData ? TriggerEventData->EventMagnitude : -1.f, Target, bRetarget);
	TargetIndex = Target;
	if (!Inventory || !ASC || !Inventory->OwnsIndex(TargetIndex) || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch rejected target=%d current=%d retarget=%d"), TargetIndex, Inventory ? Inventory->GetCurrentIndex() : -1, bRetarget ? 1 : 0);
		RejectActivation(Handle, ActorInfo, ActivationInfo, Inventory);
		return;
	}

	// B42: a press during the own wait/drop changes the running switch's target; drop end and switch end stay.
	if (bRetarget && ApplyRetarget(ActorInfo, *Inventory, TargetIndex))
	{
		if (!ActorInfo->IsNetAuthority())
		{
			BindLocalRejection(ActivationInfo, *Inventory);
		}
		EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
		return;
	}

	// B42: the running switch's remaining time (a raise) is part of the new switch's wait. Read before B33 below
	// completes (and removes) a superseded switch.
	const double Running = RunningSwitchRemaining(ActorInfo);

	// ADR 0003 B33 (server): a switch still pending here is superseded by this (non-retarget) activation, which the
	// shooter made after its own drop end. If the superseded drop has ended within the resend tolerance (remaining <=
	// Raise + FireRateJitterToleranceSeconds, B37) its inventory part is completed first, so the validation below sees
	// the weapon the client sees. Otherwise the press came during the drop without the retarget flag (a modified client):
	// rejected; the superseded switch still completes at its drop end through the inventory's own timer (B37 iii).
	if (ActorInfo->IsNetAuthority() && Inventory->GetPendingIndex() != INDEX_NONE)
	{
		const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory->GetCombatSettings());
		const double Remaining = UVeloxGameplayAbility_Fire::GetSwitchRemaining(ASC);
		const double Limit = static_cast<double>(FMath::Max(Settings.WeaponRaiseTime, 0.f)) + Settings.GetFireRateJitterToleranceSeconds();
		if (Remaining > Limit + 1e-4)
		{
			UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch supersede rejected target=%d current=%d pending=%d remaining=%.3f limit=%.3f"), TargetIndex,
				Inventory->GetCurrentIndex(), Inventory->GetPendingIndex(), Remaining, Limit);
			RejectActivation(Handle, ActorInfo, ActivationInfo, Inventory);
			return;
		}
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch supersede completes pending=%d (current=%d) remaining=%.3f limit=%.3f target=%d"), Inventory->GetPendingIndex(),
			Inventory->GetCurrentIndex(), Remaining, Limit, TargetIndex);
		Inventory->CompletePendingSwitchIndexOnly();
	}
	// A real change. A retarget the server runs as a fresh switch onto the weapon it already holds is a no-op, not a
	// rejection (the shooter lands on that weapon too).
	if (TargetIndex == Inventory->GetCurrentIndex())
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch %s target=%d current=%d"), bRetarget ? TEXT("no-op") : TEXT("rejected"), TargetIndex, Inventory->GetCurrentIndex());
		if (bRetarget)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
		}
		else
		{
			RejectActivation(Handle, ActorInfo, ActivationInfo, Inventory);
		}
		return;
	}

	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory->GetCombatSettings());
	const double Wait = VeloxFireTiming::SwitchStartWait(RefireWait(ActorInfo, Settings.bSwitchWaitsForRefire), Running);
	const float Drop = FMath::Max(Settings.WeaponDropTime, 0.f);
	const float Raise = FMath::Max(Settings.WeaponRaiseTime, 0.f);

	// One predicted GE for the whole switch (applied inside the activation's prediction window).
	const float Duration = static_cast<float>(Wait) + Drop + Raise;
	// B18: this machine's own switch end (local world time); the fire abilities of the shooter's machine gate on it.
	if (const UWorld* World = GetWorld())
	{
		Inventory->SetSwitchEndTime(World->GetTimeSeconds() + Duration);
	}
	if (Duration > 0.f)
	{
		ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, VeloxEffects::MakeDurationSpec(ASC, UVeloxGE_WeaponSwitching::StaticClass(), Duration));
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxWeaponSwitch %s -> %s wait=%.3f running=%.3f drop=%.2f raise=%.2f role=%s"), *GetNameSafe(Inventory->GetCurrentDefinition()),
		*GetNameSafe(Inventory->GetDefinition(TargetIndex)), Wait, Running, Drop, Raise, ActorInfo->IsNetAuthority() ? TEXT("server") : TEXT("client"));

	// B37 (iii), B42: the inventory owns the drop end on every machine (server: SetCurrentIndex, owning client: the
	// predicted index), so the instance ends now and a press during the drop reaches PrepareInputActivation.
	Inventory->SetPendingIndex(TargetIndex, static_cast<float>(Wait) + Drop);
	if (!ActorInfo->IsNetAuthority())
	{
		BindLocalRejection(ActivationInfo, *Inventory);
	}
	// Not replicated: the server ends its own instance (bServerRespectsRemoteAbilityCancellation is off anyway).
	EndAbility(Handle, ActorInfo, ActivationInfo, false, false);
}
