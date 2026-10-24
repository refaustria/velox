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

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Gauntlet, "Velox.Weapon.Gauntlet", "Gauntlet (M3).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_MachineGun, "Velox.Weapon.MachineGun", "Machine gun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Shotgun, "Velox.Weapon.Shotgun", "Shotgun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_PlasmaGun, "Velox.Weapon.PlasmaGun", "Plasma gun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_RocketLauncher, "Velox.Weapon.RocketLauncher", "Rocket launcher.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_LightningGun, "Velox.Weapon.LightningGun", "Lightning gun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Weapon_Railgun, "Velox.Weapon.Railgun", "Railgun.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Bullets, "Velox.Ammo.Bullets", "Machine gun ammo (also the SetByCaller key of GE_AmmoCost/GE_SpawnInit).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Shells, "Velox.Ammo.Shells", "Shotgun ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Cells, "Velox.Ammo.Cells", "Plasma gun ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Rockets, "Velox.Ammo.Rockets", "Rocket launcher ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Lightning, "Velox.Ammo.Lightning", "Lightning gun ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ammo_Slugs, "Velox.Ammo.Slugs", "Railgun ammo.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_MachineGun, "Velox.Damage.MachineGun", "Damage type: machine gun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Shotgun, "Velox.Damage.Shotgun", "Damage type: shotgun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Plasma, "Velox.Damage.Plasma", "Damage type: plasma.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Rocket, "Velox.Damage.Rocket", "Damage type: rocket.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Lightning, "Velox.Damage.Lightning", "Damage type: lightning.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Rail, "Velox.Damage.Rail", "Damage type: railgun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Gauntlet, "Velox.Damage.Gauntlet", "Damage type: gauntlet.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Fall, "Velox.Damage.Fall", "Damage type: falling.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Environment, "Velox.Damage.Environment", "Damage type: world (pits, test commands).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Telefrag, "Velox.Damage.Telefrag", "Damage type: telefrag.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Damage_Self, "Velox.Damage.Self", "Added alongside the weapon damage tag when the victim is the instigator.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Damage, "SetByCaller.Damage", "GE_Damage: raw damage per hit/pellet.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_PelletHits, "SetByCaller.PelletHits", "GE_Damage: number of pellets that hit (default 1).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Knockback, "SetByCaller.Knockback", "Reserved (knockback comes from FVeloxDamageMath).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Duration, "SetByCaller.Duration", "Duration of HasDuration GEs (weapon switching, spawn protection).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Health, "SetByCaller.Health", "GE_SpawnInit: health.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_MaxHealth, "SetByCaller.MaxHealth", "GE_SpawnInit: max health.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Armor, "SetByCaller.Armor", "GE_SpawnInit: armor.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_MaxArmor, "SetByCaller.MaxArmor", "GE_SpawnInit: max armor.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Weapon_Fire, "Velox.Ability.Weapon.Fire", "Weapon fire abilities.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Weapon_Switch, "Velox.Ability.Weapon.Switch", "Weapon switch ability.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Death, "Velox.Ability.Death", "Reserved.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Respawn, "Velox.Ability.Respawn", "Reserved.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayEvent_Death, "GameplayEvent.Death", "Death event.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayEvent_Damage, "GameplayEvent.Damage", "Damage event.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayEvent_Pickup, "GameplayEvent.Pickup", "Pickup event.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Character_Damage, "GameplayCue.Character.Damage", "Character took damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Character_Death, "GameplayCue.Character.Death", "Character died.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Character_Spawn, "GameplayCue.Character.Spawn", "Character spawned.");
}
