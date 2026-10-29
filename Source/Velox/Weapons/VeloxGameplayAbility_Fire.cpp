// Velox - fire ability base.

#include "Weapons/VeloxGameplayAbility_Fire.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Combat/VeloxAmmoSet.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffectContext.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameModes/VeloxMatchEffects.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameplayCueInterface.h"
#include "GameFramework/Character.h"
#include "Movement/VeloxMovementComponent.h"
#include "TimerManager.h"
#include "Weapons/VeloxFireTiming.h"
#include "Weapons/VeloxWeaponCheats.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponInventoryComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxGameplayAbility_Fire)

namespace VeloxFireDebug
{
	static int32 GLocalShotCount = 0;

	int32 GetLocalShotCount()
	{
		return GLocalShotCount;
	}

	void NoteLocalShot()
	{
		++GLocalShotCount;
	}
}

UVeloxGameplayAbility_Fire::UVeloxGameplayAbility_Fire(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ActivationPolicy = EVeloxAbilityActivationPolicy::WhileInputActive;
	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VeloxGameplayTags::Ability_Weapon_Fire);
	SetAssetTags(AssetTags);
	// Not State_WeaponSwitching (B18): the replicated GE ends one RTT late on the client; CanActivateAbility gates instead.
	ActivationBlockedTags.AddTag(VeloxGameplayTags::State_Dead);
	// ADR 0004 section 5: no firing while the match is frozen (intermission).
	ActivationBlockedTags.AddTag(VeloxMatchTags::State_MatchFrozen);
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

double UVeloxGameplayAbility_Fire::GetSwitchRemaining(const UAbilitySystemComponent* ASC)
{
	if (!ASC || !ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_WeaponSwitching))
	{
		return 0.0;
	}
	const FGameplayEffectQuery Query = FGameplayEffectQuery::MakeQuery_MatchAnyOwningTags(FGameplayTagContainer(VeloxGameplayTags::State_WeaponSwitching));
	double Remaining = 0.0;
	for (const float Seconds : ASC->GetActiveEffectsTimeRemaining(Query))
	{
		Remaining = FMath::Max(Remaining, static_cast<double>(Seconds));
	}
	return Remaining;
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

	const UWorld* World = ActorInfo->AvatarActor.IsValid() ? ActorInfo->AvatarActor->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	const double Now = World->GetTimeSeconds();
	const bool bRemote = IsServerForRemoteShooter(ActorInfo);

	// B18: switch timing. Shooter's machine: its own switch end; server for a remote shooter: the GE with a tolerance.
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory->GetCombatSettings());
	if (!VeloxFireTiming::IsSwitchFireAllowed(!bRemote, Now, Inventory->GetSwitchEndTime(), bRemote ? GetSwitchRemaining(ASC) : 0.0, Settings.GetSwitchTimingToleranceSeconds()))
	{
		if (OptionalRelevantTags)
		{
			OptionalRelevantTags->AddTag(UAbilitySystemGlobals::Get().ActivateFailTagsBlockedTag);
		}
		return false;
	}
	// The current weapon only; the server also accepts the target of a running switch within the tolerance above (the
	// accepted shot finalises the switch, FinalisePendingSwitch).
	if (Definition != Inventory->GetCurrentDefinition() && !(bRemote && Definition == Inventory->GetPendingDefinition()))
	{
		return false;
	}
	// NextFireTime only on the shooter's machine; the server for a remote shooter has the receive-time bucket (B32).
	return bRemote || Now >= NextFireTime;
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

bool UVeloxGameplayAbility_Fire::IsShooterHasteActive(const FGameplayAbilityActorInfo* ActorInfo)
{
	if (!ActorInfo)
	{
		return false;
	}
	if (IsServerForRemoteShooter(ActorInfo))
	{
		// Server rule: its own tag (exact on the server's timeline; bucket refill and capacity, B32).
		const UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();
		return ASC && ASC->HasMatchingGameplayTag(VeloxGameplayTags::Powerup_Haste);
	}
	// Shooter's machine: the move timeline (B6: shots happen at the end of the last move, GetPowerupClockNow there).
	const ACharacter* Character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
	const UVeloxMovementComponent* Movement = Character ? Cast<UVeloxMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	return Movement && Movement->IsHasteActive();
}

float UVeloxGameplayAbility_Fire::GetHasteFireInterval(const FGameplayAbilityActorInfo* ActorInfo, float Interval) const
{
	const UVeloxWeaponInventoryComponent* Inventory = ActorInfo ? UVeloxWeaponInventoryComponent::Find(ActorInfo->OwnerActor.Get()) : nullptr;
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory ? Inventory->GetCombatSettings() : nullptr);
	return static_cast<float>(VeloxFireTiming::HasteFireInterval(Interval, IsShooterHasteActive(ActorInfo), Settings.HasteFireRateScale));
}

void UVeloxGameplayAbility_Fire::AdvanceNextFireTime(double Now, float Interval)
{
	const UWorld* World = GetWorld();
	// ADR 0004 section 3: Haste divides the interval (client rule; the server's remote path runs the bucket instead).
	const float HasteInterval = GetHasteFireInterval(CurrentActorInfo, Interval);
	NextFireTime = VeloxFireTiming::AdvanceNextFireTime(NextFireTime, Now, HasteInterval, World ? World->GetDeltaSeconds() : 0.0);
}

bool UVeloxGameplayAbility_Fire::ConsumeServerFireToken(double Now, const UVeloxWeaponDefinition& Definition)
{
	const UVeloxWeaponInventoryComponent* Inventory = GetInventory();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory ? Inventory->GetCombatSettings() : nullptr);
	// ADR 0004 section 3: the server's Haste tag sets the bucket's refill interval and capacity (B32).
	const float Interval = GetHasteFireInterval(CurrentActorInfo, Definition.GetSafeFireInterval());
	return ServerBucket.TryConsume(Now, Interval, FVeloxFireRateBucket::CapacityFor(Interval, Settings.GetFireRateJitterToleranceSeconds()));
}

EVeloxShotReject UVeloxGameplayAbility_Fire::ValidateServerShot(const FGameplayAbilityTargetData* Raw, double Now, const UVeloxWeaponDefinition& Definition, FVeloxValidatedShot& OutShot,
	bool bConsumeBucket)
{
	const UVeloxWeaponInventoryComponent* Inventory = GetInventory();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(Inventory ? Inventory->GetCombatSettings() : nullptr);
	const float Interval = GetHasteFireInterval(CurrentActorInfo, Definition.GetSafeFireInterval());
	return VeloxShotValidation::Validate(Raw, ServerBucket, Now, Interval, FVeloxFireRateBucket::CapacityFor(Interval, Settings.GetFireRateJitterToleranceSeconds()),
		OutShot, bConsumeBucket);
}

bool UVeloxGameplayAbility_Fire::DebugHoldServerTargetData(TFunction<void()>&& Resume)
{
#if UE_BUILD_SHIPPING
	return false;
#else
	const UVeloxWeaponDefinition* Definition = GetCurrentDefinition();
	UWorld* World = GetWorld();
	float HoldSeconds = 0.f;
	if (bReleasingHeldTargetData || !Definition || !World || !VeloxWeaponCheats::ConsumeShotRpcHold(Definition->Slot, HoldSeconds))
	{
		return false;
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotRpcHeld weapon=%s slot=%d ms=%.0f t=%.3f"), *GetNameSafe(Definition), Definition->Slot, HoldSeconds * 1000.f, World->GetTimeSeconds());
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Resume = MoveTemp(Resume)]()
	{
		bReleasingHeldTargetData = true;
		Resume();
		bReleasingHeldTargetData = false;
	}), FMath::Max(HoldSeconds, UE_KINDA_SMALL_NUMBER), false);
	return true;
#endif
}

void UVeloxGameplayAbility_Fire::FinalisePendingSwitch(const UVeloxWeaponDefinition& Definition) const
{
	UVeloxWeaponInventoryComponent* Inventory = GetInventory();
	if (Inventory && Inventory->GetPendingIndex() != INDEX_NONE && Inventory->GetPendingDefinition() == &Definition)
	{
		UE_LOG(LogVeloxCombat, Verbose, TEXT("VeloxWeaponSwitch finalised by a shot of %s"), *GetNameSafe(&Definition));
		Inventory->CompletePendingSwitch(); // D4: also ends the switch instance and its GE
	}
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
