// Velox - native gameplay tags.

#include "Core/VeloxGameplayTags.h"

namespace VeloxGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Move, "InputTag.Move", "Move input (wish direction).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Look_Mouse, "InputTag.Look.Mouse", "Look input (raw mouse delta).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Jump, "InputTag.Jump", "Jump input (handled by movement, never by an ability).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Crouch, "InputTag.Crouch", "Crouch input.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Fire, "InputTag.Fire", "Fire the current weapon.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Zoom, "InputTag.Zoom", "Zoom.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Scoreboard, "InputTag.Scoreboard", "Show the scoreboard.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Next, "InputTag.Weapon.Next", "Switch to the next weapon.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Prev, "InputTag.Weapon.Prev", "Switch to the previous weapon.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Slot1, "InputTag.Weapon.Slot1", "Select weapon slot 1.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Slot2, "InputTag.Weapon.Slot2", "Select weapon slot 2.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Slot3, "InputTag.Weapon.Slot3", "Select weapon slot 3.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Slot4, "InputTag.Weapon.Slot4", "Select weapon slot 4.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Slot5, "InputTag.Weapon.Slot5", "Select weapon slot 5.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Slot6, "InputTag.Weapon.Slot6", "Select weapon slot 6.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Slot7, "InputTag.Weapon.Slot7", "Select weapon slot 7.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Slot8, "InputTag.Weapon.Slot8", "Select weapon slot 8.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Weapon_Slot9, "InputTag.Weapon.Slot9", "Select weapon slot 9.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Dead, "Velox.State.Dead", "Character is dead.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_SpawnProtected, "Velox.State.SpawnProtected", "Character has spawn protection.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_WeaponSwitching, "Velox.State.WeaponSwitching", "Character is switching weapons.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(State_Firing, "Velox.State.Firing", "Character is firing.");
}
