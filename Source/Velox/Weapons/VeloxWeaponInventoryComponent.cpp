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
#include "GameModes/VeloxPlayerState.h"
#include "GameplayPrediction.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Weapons/VeloxGameplayAbility_Fire.h"
#include "Weapons/VeloxGameplayAbility_WeaponSwitch.h"
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
		return UVeloxWeaponTable::CycleOwned(OwnedMask, Table->Num(), From, +1);
	}
	if (InputTag == InputTag_Weapon_Prev)
	{
		return UVeloxWeaponTable::CycleOwned(OwnedMask, Table->Num(), From, -1);
	}
	const FGameplayTag SlotTags[] = { InputTag_Weapon_Slot1, InputTag_Weapon_Slot2, InputTag_Weapon_Slot3, InputTag_Weapon_Slot4, InputTag_Weapon_Slot5,
		InputTag_Weapon_Slot6, InputTag_Weapon_Slot7, InputTag_Weapon_Slot8, InputTag_Weapon_Slot9 };
	for (int32 Slot = 1; Slot <= UE_ARRAY_COUNT(SlotTags); ++Slot)
	{
		if (InputTag == SlotTags[Slot - 1])
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
	const bool bNew = !OwnsIndex(Index);
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
	World->GetTimerManager().SetTimer(BeamTimer, FTimerDelegate::CreateUObject(this, &ThisClass::StopBeam), 2.5f * Definition.GetSafeFireInterval(), false);
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

void UVeloxWeaponInventoryComponent::SetPendingIndex(int32 Index)
{
	PendingIndex = (Index >= 0 && Index < UVeloxWeaponTable::MaxWeapons) ? static_cast<uint8>(Index) : NoWeapon;
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

void UVeloxWeaponInventoryComponent::OnRep_OwnedMask()
{
}
