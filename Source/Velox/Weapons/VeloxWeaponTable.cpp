// Velox - weapon table.

#include "Weapons/VeloxWeaponTable.h"

#include "Weapons/VeloxWeaponDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxWeaponTable)

int32 UVeloxWeaponTable::IndexOf(const UVeloxWeaponDefinition* Definition) const
{
	if (!Definition)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Num(); ++Index)
	{
		if (Weapons[Index] == Definition)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UVeloxWeaponTable::FindBySlot(int32 Slot) const
{
	for (int32 Index = 0; Index < Num(); ++Index)
	{
		if (Weapons[Index] && Weapons[Index]->Slot == Slot)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UVeloxWeaponTable::FindByTag(const FGameplayTag& WeaponTag) const
{
	for (int32 Index = 0; Index < Num(); ++Index)
	{
		if (Weapons[Index] && Weapons[Index]->WeaponTag == WeaponTag)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 UVeloxWeaponTable::CycleOwned(uint32 OwnedMask, int32 NumWeapons, int32 From, int32 Direction)
{
	NumWeapons = FMath::Clamp(NumWeapons, 0, MaxWeapons);
	OwnedMask &= NumWeapons >= 32 ? ~0u : ((1u << NumWeapons) - 1u);
	if (OwnedMask == 0 || NumWeapons == 0)
	{
		return INDEX_NONE;
	}
	const int32 Step = Direction >= 0 ? 1 : -1;
	// No valid current weapon: start just outside the range so the first step lands on the first/last index.
	int32 Index = (From >= 0 && From < NumWeapons) ? From : (Step > 0 ? NumWeapons - 1 : 0);
	for (int32 Tries = 0; Tries < NumWeapons; ++Tries)
	{
		Index = (Index + Step + NumWeapons) % NumWeapons;
		if (OwnedMask & (1u << Index))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}
