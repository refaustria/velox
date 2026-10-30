// Velox - weapon inventory.

#include "Weapons/VeloxWeaponInventoryComponent.h"

#include "AbilitySystemComponent.h"
#include "Combat/VeloxAbilitySystemComponent.h"
#include "Combat/VeloxCombatLog.h"
#include "Combat/VeloxCombatSettings.h"
#include "Combat/VeloxGameplayEffects.h"
#include "Core/VeloxGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameModes/VeloxMatchEffects.h"
#include "GameModes/VeloxPlayerController.h"
#include "GameModes/VeloxPlayerState.h"
#include "GameplayPrediction.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Movement/VeloxMovementComponent.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxGameplayAbility_FireProjectile.h"
#include "Weapons/VeloxGameplayAbility_WeaponSwitch.h"
#include "Weapons/VeloxProjectile.h"
#include "Weapons/VeloxShotValidation.h"
#include "Weapons/VeloxWeaponDefinition.h"
#include "Weapons/VeloxWeaponTable.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxWeaponInventoryComponent)

UVeloxWeaponInventoryComponent::UVeloxWeaponInventoryComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	SwitchAbilityClass = UVeloxGameplayAbility_WeaponSwitch::StaticClass();
}

void UVeloxWeaponInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UVeloxWeaponInventoryComponent, OwnedMask, COND_OwnerOnly);
	DOREPLIFETIME(UVeloxWeaponInventoryComponent, CurrentIndex);
}

void UVeloxWeaponInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearPendingDropTimer();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BeamTimer);
	}
	Super::EndPlay(EndPlayReason);
}

UVeloxWeaponInventoryComponent* UVeloxWeaponInventoryComponent::Find(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}
	const APlayerState* PlayerState = Cast<APlayerState>(Actor);
	if (!PlayerState)
	{
		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			PlayerState = Pawn->GetPlayerState();
		}
		else if (const AController* Controller = Cast<AController>(Actor))
		{
			PlayerState = Controller->PlayerState;
		}
	}
	return PlayerState ? PlayerState->FindComponentByClass<UVeloxWeaponInventoryComponent>() : nullptr;
}

const UVeloxCombatSettings* UVeloxWeaponInventoryComponent::GetCombatSettings() const
{
	const AVeloxPlayerState* PlayerState = Cast<AVeloxPlayerState>(GetOwner());
	return PlayerState ? PlayerState->GetCombatSettings() : nullptr;
}

const UVeloxWeaponTable* UVeloxWeaponInventoryComponent::GetWeaponTable() const
{
	const UVeloxCombatSettings* Settings = GetCombatSettings();
	return Settings ? Settings->WeaponTable.Get() : nullptr;
}

int32 UVeloxWeaponInventoryComponent::NumWeapons() const
{
	const UVeloxWeaponTable* Table = GetWeaponTable();
	return Table ? Table->Num() : 0;
}

const UVeloxWeaponDefinition* UVeloxWeaponInventoryComponent::GetDefinition(int32 Index) const
{
	const UVeloxWeaponTable* Table = GetWeaponTable();
	return Table ? Table->Get(Index) : nullptr;
}

int32 UVeloxWeaponInventoryComponent::IndexOf(const UVeloxWeaponDefinition* Definition) const
{
	const UVeloxWeaponTable* Table = GetWeaponTable();
	return Table ? Table->IndexOf(Definition) : INDEX_NONE;
}

int32 UVeloxWeaponInventoryComponent::GetCurrentIndex() const
{
	if (PredictedIndex != INDEX_NONE)
	{
		const UWorld* World = GetWorld();
		if (World && World->GetTimeSeconds() < PredictionExpireTime)
		{
			return PredictedIndex;
		}
	}
	return GetAuthoritativeIndex();
}

UVeloxAbilitySystemComponent* UVeloxWeaponInventoryComponent::GetVeloxASC() const
{
	const AVeloxPlayerState* PlayerState = Cast<AVeloxPlayerState>(GetOwner());
	return PlayerState ? PlayerState->GetVeloxAbilitySystemComponent() : nullptr;
}

FGameplayAbilitySpec* UVeloxWeaponInventoryComponent::FindFireSpec(int32 Index) const
{
	const UVeloxWeaponDefinition* Definition = GetDefinition(Index);
	UVeloxAbilitySystemComponent* ASC = GetVeloxASC();
	if (!Definition || !ASC)
	{
		return nullptr;
	}
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.SourceObject.Get() == Definition && Spec.Ability && Spec.Ability->IsA<UVeloxGameplayAbility_Fire>())
		{
			return &Spec;
		}
	}
	return nullptr;
}

int32 UVeloxWeaponInventoryComponent::ResolveSelectInput(const FGameplayTag& InputTag) const
{
	using namespace VeloxGameplayTags;
	const UVeloxWeaponTable* Table = GetWeaponTable();
	if (!Table || !InputTag.IsValid())
	{
		return INDEX_NONE;
	}
	// Cycling continues from a running switch's target on the server, from the predicted index on the client.
	const int32 From = GetPendingIndex() != INDEX_NONE ? GetPendingIndex() : GetCurrentIndex();
	if (InputTag == InputTag_Weapon_Next)
	{
		return UVeloxWeaponTable::CycleOwned(GetEffectiveOwnedMask(), Table->Num(), From, +1);
	}
	if (InputTag == InputTag_Weapon_Prev)
	{
		return UVeloxWeaponTable::CycleOwned(GetEffectiveOwnedMask(), Table->Num(), From, -1);
	}
	for (int32 Slot = 1; Slot <= 9; ++Slot)
	{
		if (InputTag == SlotInputTag(Slot))
		{
			const int32 Index = Table->FindBySlot(Slot);
			return OwnsIndex(Index) ? Index : INDEX_NONE;
		}
	}
	return INDEX_NONE;
}

void UVeloxWeaponInventoryComponent::GrantFireAbility(int32 Index)
{
	UVeloxAbilitySystemComponent* ASC = GetVeloxASC();
	const UVeloxWeaponDefinition* Definition = GetDefinition(Index);
	if (!ASC || !Definition || !Definition->FireAbility)
	{
		return;
	}
	FireSpecHandles.SetNum(FMath::Max(FireSpecHandles.Num(), Index + 1));
	if (FireSpecHandles[Index].IsValid())
	{
		return;
	}
	FGameplayAbilitySpec Spec(Definition->FireAbility, 1, INDEX_NONE, const_cast<UVeloxWeaponDefinition*>(Definition));
	Spec.GetDynamicSpecSourceTags().AddTag(VeloxGameplayTags::InputTag_Fire);
	FireSpecHandles[Index] = ASC->GiveAbility(Spec);
}

void UVeloxWeaponInventoryComponent::RemoveFireAbility(int32 Index)
{
	if (!FireSpecHandles.IsValidIndex(Index) || !FireSpecHandles[Index].IsValid())
	{
		return;
	}
	if (UVeloxAbilitySystemComponent* ASC = GetVeloxASC())
	{
		ASC->ClearAbility(FireSpecHandles[Index]);
	}
	FireSpecHandles[Index] = FGameplayAbilitySpecHandle();
}

void UVeloxWeaponInventoryComponent::GrantSwitchAbility()
{
	UVeloxAbilitySystemComponent* ASC = GetVeloxASC();
	if (!ASC || !SwitchAbilityClass || SwitchSpecHandle.IsValid())
	{
		return;
	}
	using namespace VeloxGameplayTags;
	FGameplayAbilitySpec Spec(SwitchAbilityClass, 1, INDEX_NONE, this);
	FGameplayTagContainer& Tags = Spec.GetDynamicSpecSourceTags();
	for (const FGameplayTag& Tag : { InputTag_Weapon_Slot1.GetTag(), InputTag_Weapon_Slot2.GetTag(), InputTag_Weapon_Slot3.GetTag(), InputTag_Weapon_Slot4.GetTag(),
		InputTag_Weapon_Slot5.GetTag(), InputTag_Weapon_Slot6.GetTag(), InputTag_Weapon_Slot7.GetTag(), InputTag_Weapon_Slot8.GetTag(), InputTag_Weapon_Slot9.GetTag(),
		InputTag_Weapon_Next.GetTag(), InputTag_Weapon_Prev.GetTag() })
	{
		Tags.AddTag(Tag);
	}
	SwitchSpecHandle = ASC->GiveAbility(Spec);
}

bool UVeloxWeaponInventoryComponent::AddWeapon(const UVeloxWeaponDefinition* Definition, int32 Ammo)
{
	const int32 Index = IndexOf(Definition);
	if (!GetOwner() || !GetOwner()->HasAuthority() || Index == INDEX_NONE)
	{
		UE_CLOG(Definition && Index == INDEX_NONE, LogVeloxCombat, Warning, TEXT("%s: %s is not in the weapon table."), *GetNameSafe(GetOwner()), *GetNameSafe(Definition));
		return false;
	}
	GrantSwitchAbility();
	const bool bNew = (OwnedMask & (1u << Index)) == 0;
	if (bNew)
	{
		OwnedMask |= static_cast<uint16>(1u << Index);
		GrantFireAbility(Index);
	}
	const int32 Amount = Ammo >= 0 ? Ammo : Definition->PickupAmmo;
	UVeloxAbilitySystemComponent* ASC = GetVeloxASC();
	if (ASC && Amount > 0 && Definition->AmmoTag.IsValid())
	{
		// GE_AmmoCost with a negative cost adds ammo (clamped to the cap by UVeloxAmmoSet).
		const FGameplayEffectSpecHandle Spec = VeloxEffects::MakeAmmoCostSpec(ASC, Definition->AmmoTag, -Amount);
		if (Spec.IsValid())
		{
			ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}
	if (CurrentIndex == NoWeapon)
	{
		SetCurrentIndex(Index);
	}
	UE_LOG(LogVeloxCombat, Log, TEXT("%s: AddWeapon %s index=%d new=%d ammo+%d owned=0x%04x current=%d"), *GetNameSafe(GetOwner()), *GetNameSafe(Definition), Index,
		bNew ? 1 : 0, Amount, OwnedMask, GetAuthoritativeIndex());
	return true;
}

void UVeloxWeaponInventoryComponent::RemoveAllWeapons()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	for (int32 Index = 0; Index < FireSpecHandles.Num(); ++Index)
	{
		RemoveFireAbility(Index);
	}
	OwnedMask = 0;
	SetCurrentIndex(INDEX_NONE);
}

void UVeloxWeaponInventoryComponent::ResetToLoadout()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	GrantSwitchAbility();
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(GetCombatSettings());
	const UVeloxWeaponTable* Table = GetWeaponTable();
	uint16 Desired = 0;
	int32 First = INDEX_NONE;
	for (const FVeloxSpawnWeapon& Entry : Settings.SpawnLoadout)
	{
		const int32 Index = Table ? Table->IndexOf(Entry.Definition) : INDEX_NONE;
		if (Index == INDEX_NONE)
		{
			UE_LOG(LogVeloxCombat, Warning, TEXT("%s: spawn loadout entry %s is not in the weapon table %s."), *GetNameSafe(GetOwner()), *GetNameSafe(Entry.Definition), *GetNameSafe(Table));
			continue;
		}
		Desired |= static_cast<uint16>(1u << Index);
		First = First == INDEX_NONE ? Index : First;
	}
	// ADR 0003 B35/B38: move-shot records and waiting fire RPCs belong to the shot sequence of the old life. Dropped before
	// the specs change: removing the spec of a fire ability with a waiting shot ends it not-cancelled, which would launch it.
	ClearMoveShots();
	// Keep the specs of weapons that stay (no re-grant churn on respawn), take the others away.
	for (int32 Index = 0; Index < UVeloxWeaponTable::MaxWeapons; ++Index)
	{
		const bool bWant = (Desired & (1u << Index)) != 0;
		if (!bWant)
		{
			RemoveFireAbility(Index);
		}
		else if (!OwnsIndex(Index) || !FireSpecHandles.IsValidIndex(Index) || !FireSpecHandles[Index].IsValid())
		{
			GrantFireAbility(Index);
		}
	}
	OwnedMask = Desired;
	SwitchEndTime = 0.0;
	// B37: a switch running at death must not land on a weapon of the old life (SetCurrentIndex clears it too).
	ClearPendingDropTimer();
	ResetShotSeq();
	SetCurrentIndex(First);
	UE_LOG(LogVeloxCombat, Log, TEXT("%s: ResetToLoadout owned=0x%04x current=%s (%d entries)"), *GetNameSafe(GetOwner()), OwnedMask, *GetNameSafe(GetDefinition(First)),
		Settings.SpawnLoadout.Num());
}

void UVeloxWeaponInventoryComponent::StopBeamOf(int32 Index)
{
	const UVeloxWeaponDefinition* Definition = GetDefinition(Index);
	if (Definition && Definition->BeamCueTag.IsValid() && Definition->BeamCueTag == ActiveBeamCueTag)
	{
		StopBeam();
	}
}

void UVeloxWeaponInventoryComponent::KeepBeamAlive(const UVeloxWeaponDefinition& Definition, const FGameplayCueParameters& Parameters)
{
	UVeloxAbilitySystemComponent* ASC = GetVeloxASC();
	UWorld* World = GetWorld();
	if (!ASC || !World || !GetOwner()->HasAuthority() || !Definition.BeamCueTag.IsValid())
	{
		return;
	}
	if (ActiveBeamCueTag.IsValid() && ActiveBeamCueTag != Definition.BeamCueTag)
	{
		StopBeam();
	}
	if (!ASC->IsGameplayCueActive(Definition.BeamCueTag))
	{
		// No prediction key: the shooter has no predicted copy, so it receives the beam through replication like everyone.
		FScopedPredictionWindow NoPrediction(ASC, FPredictionKey(), false);
		ASC->AddGameplayCue(Definition.BeamCueTag, Parameters);
	}
	ActiveBeamCueTag = Definition.BeamCueTag;
	// B23: at least one interval, or held fire would drop the beam between two on-time shots.
	const float KeepAlive = FMath::Max(UVeloxCombatSettings::GetOrDefault(GetCombatSettings()).GetBeamKeepAliveSeconds(), Definition.GetSafeFireInterval());
	World->GetTimerManager().SetTimer(BeamTimer, FTimerDelegate::CreateUObject(this, &ThisClass::StopBeam), KeepAlive, false);
}

bool UVeloxWeaponInventoryComponent::ShouldLogRejectionAtLog(double Now)
{
	if (RejectLogWindowStart < 0.0 || Now - RejectLogWindowStart >= 1.0 || Now < RejectLogWindowStart)
	{
		RejectLogWindowStart = Now;
		RejectLogCount = 0;
	}
	return ++RejectLogCount <= RejectLogLimitPerSecond;
}

void UVeloxWeaponInventoryComponent::StopBeam()
{
	UVeloxAbilitySystemComponent* ASC = GetVeloxASC();
	if (ASC && ActiveBeamCueTag.IsValid() && ASC->IsGameplayCueActive(ActiveBeamCueTag))
	{
		ASC->RemoveGameplayCue(ActiveBeamCueTag);
	}
	ActiveBeamCueTag = FGameplayTag();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BeamTimer);
	}
}

void UVeloxWeaponInventoryComponent::SetCurrentIndex(int32 Index)
{
	const uint8 NewIndex = (Index >= 0 && Index < UVeloxWeaponTable::MaxWeapons) ? static_cast<uint8>(Index) : NoWeapon;
	PendingIndex = NoWeapon;
	ClearPendingDropTimer();
	if (!GetOwner() || !GetOwner()->HasAuthority() || NewIndex == CurrentIndex)
	{
		return;
	}
	const int32 Old = GetAuthoritativeIndex();
	StopBeamOf(Old);
	CurrentIndex = NewIndex;
	PredictedIndex = INDEX_NONE;
	OnCurrentWeaponChanged.Broadcast(Old, GetAuthoritativeIndex());
}

void UVeloxWeaponInventoryComponent::SetPendingIndex(int32 Index, float DropEndSeconds)
{
	PendingIndex = (Index >= 0 && Index < UVeloxWeaponTable::MaxWeapons) ? static_cast<uint8>(Index) : NoWeapon;
	ClearPendingDropTimer();
	if (PendingIndex == NoWeapon || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || !(DropEndSeconds > 0.f))
	{
		CompletePendingDrop(PendingIndex);
		return;
	}
	World->GetTimerManager().SetTimer(PendingDropTimer, FTimerDelegate::CreateUObject(this, &ThisClass::CompletePendingDrop, static_cast<int32>(PendingIndex)), DropEndSeconds,
		false);
}

bool UVeloxWeaponInventoryComponent::IsPendingDropTimerActive() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(PendingDropTimer);
}

void UVeloxWeaponInventoryComponent::CompletePendingDrop(int32 Target)
{
	PendingDropTimer.Invalidate();
	if (GetPendingIndex() != Target || Target == INDEX_NONE)
	{
		return;
	}
	UE_LOG(LogVeloxCombat, Verbose, TEXT("%s: VeloxWeaponSwitch drop end (inventory) -> current %d (was %d)"), *GetNameSafe(GetOwner()), Target, GetAuthoritativeIndex());
	SetCurrentIndex(Target);
}

void UVeloxWeaponInventoryComponent::ClearPendingDropTimer()
{
	if (!PendingDropTimer.IsValid())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingDropTimer);
	}
	PendingDropTimer.Invalidate();
}

void UVeloxWeaponInventoryComponent::CompletePendingSwitch()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || PendingIndex == NoWeapon)
	{
		return;
	}
	ClearPendingDropTimer();
	if (UVeloxAbilitySystemComponent* ASC = GetVeloxASC())
	{
		if (const FGameplayAbilitySpec* Spec = SwitchSpecHandle.IsValid() ? ASC->FindAbilitySpecFromHandle(SwitchSpecHandle) : nullptr; Spec && Spec->IsActive())
		{
			for (UGameplayAbility* Instance : Spec->GetAbilityInstances())
			{
				if (UVeloxGameplayAbility_WeaponSwitch* Switch = Cast<UVeloxGameplayAbility_WeaponSwitch>(Instance); Switch && Switch->IsActive())
				{
					Switch->CompleteSwitchEarly();
				}
			}
		}
	}
	CompletePendingSwitchIndexOnly();
}

void UVeloxWeaponInventoryComponent::CompletePendingSwitchIndexOnly()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || PendingIndex == NoWeapon)
	{
		return;
	}
	const int32 Target = PendingIndex;
	ClearPendingDropTimer();
	if (UVeloxAbilitySystemComponent* ASC = GetVeloxASC())
	{
		ASC->RemoveActiveGameplayEffectBySourceEffect(UVeloxGE_WeaponSwitching::StaticClass(), nullptr);
	}
	SwitchEndTime = 0.0;
	SetCurrentIndex(Target);
}

void UVeloxWeaponInventoryComponent::PredictCurrentIndex(int32 Index)
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const int32 Old = GetCurrentIndex();
	if (Index == GetAuthoritativeIndex())
	{
		PredictedIndex = INDEX_NONE;
	}
	else
	{
		PredictedIndex = Index;
		PredictionExpireTime = World->GetTimeSeconds() + PredictionTimeout;
	}
	if (Old != GetCurrentIndex())
	{
		OnCurrentWeaponChanged.Broadcast(Old, GetCurrentIndex());
	}
}

void UVeloxWeaponInventoryComponent::OnRep_CurrentIndex(uint8 OldIndex)
{
	const int32 OldEffective = PredictedIndex != INDEX_NONE ? PredictedIndex : (OldIndex == NoWeapon ? INDEX_NONE : static_cast<int32>(OldIndex));
	// The server confirmed (or overrode) the prediction.
	PredictedIndex = INDEX_NONE;
	if (OldEffective != GetAuthoritativeIndex())
	{
		OnCurrentWeaponChanged.Broadcast(OldEffective, GetAuthoritativeIndex());
	}
}

uint16 UVeloxWeaponInventoryComponent::GetEffectiveOwnedMask() const
{
	if (PredictedOwnedMask == 0)
	{
		return OwnedMask;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	uint16 Predicted = 0;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PredictedOwnedUntil); ++Index)
	{
		if ((PredictedOwnedMask & (1u << Index)) != 0 && Now < PredictedOwnedUntil[Index])
		{
			Predicted |= static_cast<uint16>(1u << Index);
		}
	}
	return OwnedMask | Predicted;
}

void UVeloxWeaponInventoryComponent::PredictOwned(int32 Index, double Until)
{
	if (Index < 0 || Index >= UE_ARRAY_COUNT(PredictedOwnedUntil) || (GetOwner() && GetOwner()->HasAuthority()))
	{
		return;
	}
	PredictedOwnedMask |= static_cast<uint16>(1u << Index);
	PredictedOwnedUntil[Index] = Until;
	UE_LOG(LogVeloxCombat, Log, TEXT("%s: PredictOwned index=%d until=%.3f owned=0x%04x effective=0x%04x"), *GetNameSafe(GetOwner()), Index, Until, OwnedMask, GetEffectiveOwnedMask());
}

void UVeloxWeaponInventoryComponent::DropPredictedOwned(int32 Index)
{
	if (Index < 0 || Index >= UE_ARRAY_COUNT(PredictedOwnedUntil) || (PredictedOwnedMask & (1u << Index)) == 0)
	{
		return;
	}
	PredictedOwnedMask &= static_cast<uint16>(~(1u << Index));
	PredictedOwnedUntil[Index] = 0.0;
	UE_LOG(LogVeloxCombat, Log, TEXT("%s: DropPredictedOwned index=%d owned=0x%04x"), *GetNameSafe(GetOwner()), Index, OwnedMask);
}

FGameplayTag UVeloxWeaponInventoryComponent::SlotInputTag(int32 Slot)
{
	using namespace VeloxGameplayTags;
	switch (Slot)
	{
	case 1: return InputTag_Weapon_Slot1;
	case 2: return InputTag_Weapon_Slot2;
	case 3: return InputTag_Weapon_Slot3;
	case 4: return InputTag_Weapon_Slot4;
	case 5: return InputTag_Weapon_Slot5;
	case 6: return InputTag_Weapon_Slot6;
	case 7: return InputTag_Weapon_Slot7;
	case 8: return InputTag_Weapon_Slot8;
	case 9: return InputTag_Weapon_Slot9;
	default: return FGameplayTag();
	}
}

bool UVeloxWeaponInventoryComponent::AllowsAutoSwitchFrom(const UVeloxWeaponDefinition* Current)
{
	return !Current || Current->WeaponTag.MatchesTagExact(VeloxGameplayTags::Weapon_MachineGun) || Current->WeaponTag.MatchesTagExact(VeloxGameplayTags::Weapon_Gauntlet);
}

int32 UVeloxWeaponInventoryComponent::SelectAutoSwitchTarget(bool bEnabled, bool bCurrentAllowsAutoSwitch, int32 CurrentIndex, uint16 OldMask, uint16 NewMask)
{
	const uint16 Added = static_cast<uint16>(NewMask & ~OldMask);
	if (!bEnabled || !bCurrentAllowsAutoSwitch || OldMask == 0 || Added == 0 || FMath::CountBits(Added) != 1)
	{
		return INDEX_NONE;
	}
	const int32 Target = FMath::CountTrailingZeros(static_cast<uint32>(Added));
	return Target == CurrentIndex ? INDEX_NONE : Target;
}

void UVeloxWeaponInventoryComponent::RunAutoSwitch(uint16 OldMask, uint16 NewMask)
{
	const AVeloxPlayerState* PlayerState = Cast<AVeloxPlayerState>(GetOwner());
	const AVeloxPlayerController* Controller = PlayerState ? Cast<AVeloxPlayerController>(PlayerState->GetPlayerController()) : nullptr;
	UVeloxAbilitySystemComponent* ASC = GetVeloxASC();
	if (!Controller || !Controller->IsLocalController() || !ASC || !Controller->GetPawn())
	{
		return;
	}
	const int32 Current = GetCurrentIndex();
	const int32 Target = SelectAutoSwitchTarget(Controller->ShouldAutoSwitchOnPickup(), AllowsAutoSwitchFrom(GetDefinition(Current)), Current, OldMask, NewMask);
	const UVeloxWeaponDefinition* TargetDefinition = GetDefinition(Target);
	const FGameplayTag SlotTag = TargetDefinition ? SlotInputTag(TargetDefinition->Slot) : FGameplayTag();
	if (!SlotTag.IsValid())
	{
		return;
	}
	// One switch path: a slot press through the ASC input (processed in AVeloxPlayerController::PlayerTick), predicted
	// and validated like a key press. Pressed and released in one frame, like a tap.
	UE_LOG(LogVeloxCombat, Log, TEXT("%s: autoswitch %s -> %s (owned 0x%04x -> 0x%04x)"), *GetNameSafe(GetOwner()), *GetNameSafe(GetDefinition(Current)), *GetNameSafe(TargetDefinition),
		OldMask, NewMask);
	ASC->AbilityInputTagPressed(SlotTag);
	ASC->AbilityInputTagReleased(SlotTag);
}

void UVeloxWeaponInventoryComponent::OnRep_OwnedMask(uint16 OldMask)
{
	// Predicted bits the server now confirms are no longer predictions.
	PredictedOwnedMask &= static_cast<uint16>(~OwnedMask);
	RunAutoSwitch(OldMask, OwnedMask);
}

// ---------------------------------------------------------------------------------------------------------------------
// Server: the move-carried projectile shot (ADR 0003 B35, ADR 0002 A36)

UVeloxMovementComponent* UVeloxWeaponInventoryComponent::GetBoundServerMovement() const
{
	return BoundServerMovement.Get();
}

void UVeloxWeaponInventoryComponent::BindServerMoveStream(UVeloxMovementComponent* Movement)
{
	if (Movement && BoundServerMovement.Get() == Movement)
	{
		return;
	}
	if (UVeloxMovementComponent* Old = BoundServerMovement.Get())
	{
		Old->OnServerMoveShot.Remove(MoveShotHandle);
		Old->OnServerMoveProcessed.Remove(MoveProcessedHandle);
	}
	MoveShotHandle.Reset();
	MoveProcessedHandle.Reset();
	BoundServerMovement = Movement;
	// A new pawn is a new move stream (and a new life): nothing of the previous one carries over.
	ClearMoveShots();
	bServerMoveStreamSeen = false;
	LastProcessedMoveStamp = 0.f;
	if (Movement && GetOwner() && GetOwner()->HasAuthority())
	{
		MoveShotHandle = Movement->OnServerMoveShot.AddUObject(this, &ThisClass::HandleServerMoveShot);
		MoveProcessedHandle = Movement->OnServerMoveProcessed.AddUObject(this, &ThisClass::HandleServerMoveProcessed);
	}
}

void UVeloxWeaponInventoryComponent::UnbindServerMoveStream(const UVeloxMovementComponent* Movement)
{
	if (Movement && BoundServerMovement.Get() == Movement)
	{
		BindServerMoveStream(nullptr);
	}
}

int32 UVeloxWeaponInventoryComponent::FindMoveShotRecordIndex(uint8 Seq) const
{
	return ServerMoveShots.IndexOfByPredicate([Seq](const FVeloxMoveShotRecord& R) { return R.ShotSeq == Seq; });
}

const FVeloxMoveShotRecord* UVeloxWeaponInventoryComponent::FindMoveShotRecord(uint8 Seq) const
{
	const int32 Index = FindMoveShotRecordIndex(Seq);
	return Index != INDEX_NONE && !ServerMoveShots[Index].bAdopted ? &ServerMoveShots[Index] : nullptr;
}

bool UVeloxWeaponInventoryComponent::AdoptMoveShotRecord(uint8 Seq, FVeloxMoveShotRecord& OutRecord)
{
	const int32 Index = FindMoveShotRecordIndex(Seq);
	if (Index == INDEX_NONE || ServerMoveShots[Index].bAdopted)
	{
		return false;
	}
	ServerMoveShots[Index].bAdopted = true;
	OutRecord = ServerMoveShots[Index];
	return true;
}

bool UVeloxWeaponInventoryComponent::ShouldWaitForMoveShot(float FireMoveTimeStamp) const
{
	const UVeloxMovementComponent* Movement = BoundServerMovement.Get();
	if (!Movement || !bServerMoveStreamSeen)
	{
		return false; // no move stream (yet): the RPC path as before B35
	}
	const bool bPassed = LastProcessedMoveStamp != FireMoveTimeStamp
		&& UVeloxMovementComponent::IsMoveTimeDue(FireMoveTimeStamp, LastProcessedMoveStamp, Movement->MinTimeBetweenTimeStampResets);
	return !bPassed;
}

void UVeloxWeaponInventoryComponent::AddPendingRpcShot(uint8 Seq, float FireMoveTimeStamp, UVeloxGameplayAbility_FireProjectile* Ability)
{
	PendingRpcShots.RemoveAll([Seq](const FPendingRpcShot& P) { return P.ShotSeq == Seq; });
	FPendingRpcShot& Pending = PendingRpcShots.AddDefaulted_GetRef();
	Pending.ShotSeq = Seq;
	Pending.FireMoveTimeStamp = FireMoveTimeStamp;
	Pending.Ability = Ability;
}

void UVeloxWeaponInventoryComponent::RemovePendingRpcShot(uint8 Seq, const UVeloxGameplayAbility_FireProjectile* Ability)
{
	PendingRpcShots.RemoveAll([Seq, Ability](const FPendingRpcShot& P) { return P.ShotSeq == Seq && (!Ability || P.Ability.Get() == Ability); });
}

bool UVeloxWeaponInventoryComponent::HasPendingRpcShot(uint8 Seq) const
{
	return PendingRpcShots.ContainsByPredicate([Seq](const FPendingRpcShot& P) { return P.ShotSeq == Seq && P.Ability.IsValid(); });
}

void UVeloxWeaponInventoryComponent::NoteRpcLaunchedShot(uint8 Seq)
{
	if (RpcLaunchedShotSeqs.Contains(Seq))
	{
		return;
	}
	if (RpcLaunchedShotSeqs.Num() >= MaxRpcLaunchedShotSeqs)
	{
		RpcLaunchedShotSeqs.RemoveAt(0);
	}
	RpcLaunchedShotSeqs.Add(Seq);
}

bool UVeloxWeaponInventoryComponent::WasShotLaunchedByRpc(uint8 Seq) const
{
	return RpcLaunchedShotSeqs.Contains(Seq);
}

double UVeloxWeaponInventoryComponent::GetMoveShotOrphanSeconds() const
{
	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(GetCombatSettings());
	return static_cast<double>(Settings.GetFireRateJitterToleranceSeconds()) + static_cast<double>(Settings.MaxRewindSeconds);
}

void UVeloxWeaponInventoryComponent::NoteMoveShotOrphan(const FVeloxMoveShotRecord& Record, const TCHAR* Why)
{
	++NumMoveShotOrphans;
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	// B35 asks for Verbose; the first lines per second go to Log through the rejection throttle so the soaks can count them.
	const FString Line = FString::Printf(TEXT("VeloxMoveShotOrphan seq=%u weapon=%s age_ms=%.0f why=%s count=%d (move-shot whose fire RPC never came)"), Record.ShotSeq,
		*GetNameSafe(GetDefinition(Record.WeaponIndex)), (Now - Record.ReceiveTime) * 1000.0, Why, NumMoveShotOrphans);
	if (ShouldLogRejectionAtLog(Now))
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("%s"), *Line);
	}
	else
	{
		UE_LOG(LogVeloxCombat, Verbose, TEXT("%s"), *Line);
	}
}

void UVeloxWeaponInventoryComponent::ClearMoveShots()
{
	ServerMoveShots.Reset();
	RpcLaunchedShotSeqs.Reset();
	const TArray<FPendingRpcShot, TInlineAllocator<2>> Pending = PendingRpcShots;
	PendingRpcShots.Reset();
	for (const FPendingRpcShot& P : Pending)
	{
		if (UVeloxGameplayAbility_FireProjectile* Ability = P.Ability.Get())
		{
			Ability->CancelPendingServerShot();
		}
	}
}

void UVeloxWeaponInventoryComponent::HandleServerMoveShot(const FVeloxMoveShot& Shot, float MoveEndStamp, float MoveDeltaTime)
{
	UVeloxMovementComponent* Movement = BoundServerMovement.Get();
	APawn* Pawn = Movement ? Cast<APawn>(Movement->GetOwner()) : nullptr;
	UVeloxAbilitySystemComponent* ASC = GetVeloxASC();
	UWorld* World = GetWorld();
	if (!Movement || !Pawn || !ASC || !World || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	const int32 Index = Shot.WeaponIndex;
	const UVeloxWeaponDefinition* Definition = GetDefinition(Index);
	FGameplayAbilitySpec* Spec = nullptr;
	UVeloxGameplayAbility_FireProjectile* Ability = nullptr;
	const FVector ShotDir(Shot.Dir);
	const TCHAR* Ignore = nullptr;
	// (1) An owned projectile weapon with splash; not necessarily the current one (its switch RPC may still be in flight;
	// the current/pending check stays on the fire RPC, B18).
	if (!Definition || !OwnsIndex(Index))
	{
		Ignore = TEXT("not_owned");
	}
	else if (!Definition->ProjectileClass || Definition->SplashRadiusCm <= 0.f || Definition->ProjectileSpeed <= 0.f)
	{
		Ignore = TEXT("no_splash");
	}
	else if (!(Spec = FindFireSpec(Index)) || !(Ability = Cast<UVeloxGameplayAbility_FireProjectile>(Spec->GetPrimaryInstance())))
	{
		Ignore = TEXT("no_ability");
	}
	else if (ASC->HasMatchingGameplayTag(VeloxGameplayTags::State_Dead) || ASC->HasMatchingGameplayTag(VeloxMatchTags::State_MatchFrozen))
	{
		Ignore = TEXT("blocked");
	}
	// (2) Not yet recorded, and not already launched on the rpc path (B38: a sequence is scheduled at most once; checked
	// before the ammo/token step so a late fire move neither pays a second token nor schedules a second self splash).
	else if (FindMoveShotRecordIndex(Shot.ShotSeq) != INDEX_NONE)
	{
		Ignore = TEXT("duplicate");
	}
	else if (WasShotLaunchedByRpc(Shot.ShotSeq))
	{
		Ignore = TEXT("rpc_done");
	}
	else if (Shot.Origin.ContainsNaN() || ShotDir.ContainsNaN() || ShotDir.IsNearlyZero() || !FMath::IsFinite(Shot.FireMoveTimeStamp))
	{
		Ignore = TEXT("payload");
	}
	else
	{
		// (5) A fire RPC that overtook this move validated the shot already (token, ammo through CanActivate/CheckCost).
		const bool bRpcPaid = HasPendingRpcShot(Shot.ShotSeq);
		if (!bRpcPaid && !Ability->CheckCost(Spec->Handle, ASC->AbilityActorInfo.Get()))
		{
			Ignore = TEXT("ammo");
		}
		else if (!bRpcPaid && !Ability->ServerConsumeMoveShotToken(Now, *Definition))
		{
			Ignore = TEXT("rate");
		}
	}
	if (Ignore)
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxMoveShotIgnored seq=%u weapon=%d (%s) move_ts=%.6f reason=%s"), Shot.ShotSeq, Index, *GetNameSafe(Definition), MoveEndStamp, Ignore);
		return;
	}

	const UVeloxCombatSettings& Settings = UVeloxCombatSettings::GetOrDefault(GetCombatSettings());
	// (3) The stamp against the move's start (the explicit stamp keeps a merged predecessor from shifting it).
	const float MoveStart = MoveEndStamp - MoveDeltaTime;
	EVeloxFireStampClamp StampResult = EVeloxFireStampClamp::None;
	const float FireStamp = VeloxShotValidation::ClampFireMoveTimeStamp(Shot.FireMoveTimeStamp, MoveStart, Settings.MaxRewindSeconds, Movement->MinTimeBetweenTimeStampResets,
		StampResult);
	if (StampResult == EVeloxFireStampClamp::Clamped)
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotTimeClamp seq=%u client_ts=%.6f last_ts=%.6f clamped_ts=%.6f source=move"), Shot.ShotSeq, Shot.FireMoveTimeStamp, MoveStart, FireStamp);
	}
	else if (StampResult == EVeloxFireStampClamp::Reset)
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotTimeReset seq=%u client_ts=%.6f last_ts=%.6f source=move (time stamp reset in between, client value kept)"), Shot.ShotSeq,
			Shot.FireMoveTimeStamp, MoveStart);
	}
	// (4) The B15 origin rule against the live eye: the fire move has not run yet, so the pawn is at its start.
	const TCHAR* SnapReason = nullptr;
	const FVector ClientOrigin(Shot.Origin);
	const FVector Origin = VeloxShotValidation::ValidateOrigin(*World, *Pawn, ClientOrigin, Settings.MaxOriginErrorCm, SnapReason);
	if (SnapReason)
	{
		UE_LOG(LogVeloxCombat, Log, TEXT("VeloxShotOriginSnap seq=%u shooter=%s err_cm=%.1f reason=%s source=move late_ms=0.0"), Shot.ShotSeq, *GetNameSafe(Pawn),
			FVector::Dist(ClientOrigin, Origin), SnapReason);
	}
	// (6) Exactly what the predicting client schedules: world-only flight from the quantized shot, Quad from the move
	// timeline at the fire stamp (ADR 0004 section 3).
	const FVector Dir = ShotDir.GetSafeNormal();
	FVector SplashOrigin;
	const double Flight = AVeloxProjectile::ComputeWorldFlight(*World, *Definition, Origin, Dir, Pawn, SplashOrigin);
	const float EventTime = FireStamp + static_cast<float>(Flight);
	const bool bQuad = Movement->IsPowerupActiveAt(Movement->GetPowerupMoveTimes().QuadEnd, FireStamp);
	const int32 EventId = AVeloxProjectile::ScheduleOwnerSelfSplash(*Movement, *Definition, ASC, EventTime, SplashOrigin, Shot.ShotSeq, false, bQuad);

	if (ServerMoveShots.Num() >= MaxMoveShotRecords)
	{
		if (!ServerMoveShots[0].bAdopted)
		{
			NoteMoveShotOrphan(ServerMoveShots[0], TEXT("evicted"));
		}
		ServerMoveShots.RemoveAt(0);
	}
	FVeloxMoveShotRecord& Record = ServerMoveShots.AddDefaulted_GetRef();
	Record.ShotSeq = Shot.ShotSeq;
	Record.WeaponIndex = Index;
	Record.Origin = Origin;
	Record.Dir = Dir;
	Record.ClientOrigin = ClientOrigin;
	Record.ClientDir = Dir;
	Record.ClientFireMoveTimeStamp = Shot.FireMoveTimeStamp;
	Record.FireMoveTimeStamp = FireStamp;
	Record.EventId = EventId;
	Record.SplashOrigin = SplashOrigin;
	Record.ReceiveTime = Now;
	UE_LOG(LogVeloxCombat, Log, TEXT("VeloxMoveShot seq=%u weapon=%s fire_ts=%.6f move_ts=%.6f move_dt=%.6f flight_ms=%.1f event_ts=%.6f splash_origin=%s rpc_first=%d quad=%d"), Shot.ShotSeq,
		*GetNameSafe(Definition), FireStamp, MoveEndStamp, MoveDeltaTime, Flight * 1000.0, EventTime, *SplashOrigin.ToCompactString(), HasPendingRpcShot(Shot.ShotSeq) ? 1 : 0,
		bQuad ? 1 : 0);
}

void UVeloxWeaponInventoryComponent::HandleServerMoveProcessed(float MoveEndStamp)
{
	const UVeloxMovementComponent* Movement = BoundServerMovement.Get();
	LastProcessedMoveStamp = MoveEndStamp;
	bServerMoveStreamSeen = true;

	// Fire RPCs that overtook their move: adopt the record the move just created, or fall back once the stream passed the
	// fire stamp without one.
	if (PendingRpcShots.Num() > 0 && Movement)
	{
		const TArray<FPendingRpcShot, TInlineAllocator<2>> Pending = PendingRpcShots;
		for (const FPendingRpcShot& P : Pending)
		{
			UVeloxGameplayAbility_FireProjectile* Ability = P.Ability.Get();
			if (!Ability)
			{
				RemovePendingRpcShot(P.ShotSeq, nullptr);
				continue;
			}
			const bool bRecorded = FindMoveShotRecord(P.ShotSeq) != nullptr;
			const bool bPassed = MoveEndStamp != P.FireMoveTimeStamp && UVeloxMovementComponent::IsMoveTimeDue(P.FireMoveTimeStamp, MoveEndStamp, Movement->MinTimeBetweenTimeStampResets);
			if (bRecorded || bPassed)
			{
				Ability->ResumePendingServerShot();
			}
		}
	}

	// Records whose fire RPC never came.
	const UWorld* World = GetWorld();
	if (World && ServerMoveShots.Num() > 0)
	{
		const double Now = World->GetTimeSeconds();
		const double Expiry = GetMoveShotOrphanSeconds();
		for (int32 I = ServerMoveShots.Num() - 1; I >= 0; --I)
		{
			const FVeloxMoveShotRecord& Record = ServerMoveShots[I];
			if (!Record.bAdopted && Now - Record.ReceiveTime > Expiry)
			{
				NoteMoveShotOrphan(Record, TEXT("expired"));
				ServerMoveShots.RemoveAt(I);
			}
		}
	}
}
