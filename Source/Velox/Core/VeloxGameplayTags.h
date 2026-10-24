// Velox - native gameplay tags (ADR 0001 section 2). Designer tags live in Config/Tags/VeloxTags.ini.

#pragma once

#include "NativeGameplayTags.h"

namespace VeloxGameplayTags
{
	// Native input actions, bound directly on the character.
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Move);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Look_Mouse);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Jump);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Crouch);

	// Ability input actions, routed to the ability system component by tag.
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Fire);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Zoom);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Scoreboard);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Next);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Prev);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Slot1);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Slot2);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Slot3);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Slot4);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Slot5);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Slot6);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Slot7);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Slot8);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputTag_Weapon_Slot9);

	// Server-set, replicated character state. Movement state is deliberately not tagged.
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Dead);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_SpawnProtected);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_WeaponSwitching);
	VELOX_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Firing);
}
