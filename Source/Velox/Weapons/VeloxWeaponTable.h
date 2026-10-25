// Velox - the weapon set of a game (ADR 0003 section 4): six definitions in slot order. The table index is the weapon's
// network id (inventory bitmask bit, replicated current weapon). Asset: /Game/Weapons/DA_WeaponTable.

#pragma once

#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"

#include "VeloxWeaponTable.generated.h"

class UVeloxWeaponDefinition;

UCLASS(BlueprintType, Const, Meta = (DisplayName = "Velox Weapon Table"))
class VELOX_API UVeloxWeaponTable : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Bits in UVeloxWeaponInventoryComponent's owned mask; entries beyond it are ignored. */
	static constexpr int32 MaxWeapons = 16;

	int32 Num() const { return FMath::Min(Weapons.Num(), MaxWeapons); }
	const UVeloxWeaponDefinition* Get(int32 Index) const { return Index >= 0 && Index < Num() ? Weapons[Index].Get() : nullptr; }
	int32 IndexOf(const UVeloxWeaponDefinition* Definition) const;
	int32 FindBySlot(int32 Slot) const;
	int32 FindByTag(const FGameplayTag& WeaponTag) const;

	/**
	 * Q3 weapnext/weapprev: the next index after From in Direction (+1/-1) whose bit is set in OwnedMask, wrapping;
	 * From itself only if nothing else is owned; INDEX_NONE without owned weapons. World-free (Velox.Combat.WeaponTable).
	 */
	static int32 CycleOwned(uint32 OwnedMask, int32 NumWeapons, int32 From, int32 Direction);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Velox|Weapons")
	TArray<TObjectPtr<const UVeloxWeaponDefinition>> Weapons;
};
