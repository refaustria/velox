// Velox - item definition.

#include "Items/VeloxItemDefinition.h"

#include "Weapons/VeloxWeaponDefinition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(VeloxItemDefinition)

int32 UVeloxItemDefinition::GetEffectiveQuantity() const
{
	if (Type == EVeloxItemType::Weapon)
	{
		return Weapon ? Weapon->PickupAmmo : 0;
	}
	return Quantity;
}
