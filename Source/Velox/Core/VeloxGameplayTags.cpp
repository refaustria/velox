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
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Ready, "InputTag.Ready", "Toggle ready-up (warmup / intermission).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputTag_Menu, "InputTag.Menu", "Open the in-game menu.");
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

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Health_Small, "Velox.Item.Health.Small", "Item: +5 health up to the hard cap (Q3 green).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Health_Medium, "Velox.Item.Health.Medium", "Item: +25 health up to max health (Q3 yellow).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Health_Large, "Velox.Item.Health.Large", "Item: +50 health up to max health (Q3 gold).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Health_Mega, "Velox.Item.Health.Mega", "Item: +100 health up to the hard cap (Q3 mega).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Armor_Shard, "Velox.Item.Armor.Shard", "Item: +5 armor.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Armor_Yellow, "Velox.Item.Armor.Yellow", "Item: +50 armor.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Armor_Red, "Velox.Item.Armor.Red", "Item: +100 armor.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Ammo_Bullets, "Velox.Item.Ammo.Bullets", "Item: machine gun ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Ammo_Shells, "Velox.Item.Ammo.Shells", "Item: shotgun ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Ammo_Rockets, "Velox.Item.Ammo.Rockets", "Item: rocket launcher ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Ammo_Cells, "Velox.Item.Ammo.Cells", "Item: plasma gun ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Ammo_Lightning, "Velox.Item.Ammo.Lightning", "Item: lightning gun ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Ammo_Slugs, "Velox.Item.Ammo.Slugs", "Item: railgun ammo.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Weapon_MachineGun, "Velox.Item.Weapon.MachineGun", "Item: machine gun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Weapon_Shotgun, "Velox.Item.Weapon.Shotgun", "Item: shotgun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Weapon_RocketLauncher, "Velox.Item.Weapon.RocketLauncher", "Item: rocket launcher.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Weapon_LightningGun, "Velox.Item.Weapon.LightningGun", "Item: lightning gun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Weapon_Railgun, "Velox.Item.Weapon.Railgun", "Item: railgun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Weapon_PlasmaGun, "Velox.Item.Weapon.PlasmaGun", "Item: plasma gun.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Powerup_Quad, "Velox.Item.Powerup.Quad", "Item: Quad Damage.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Powerup_Haste, "Velox.Item.Powerup.Haste", "Item: Haste.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Powerup_Regen, "Velox.Item.Powerup.Regen", "Item: Regeneration.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Item_Powerup_BattleSuit, "Velox.Item.Powerup.BattleSuit", "Item: Battle Suit.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Powerup, "Velox.Powerup", "Any active powerup (removed on death by GE_SpawnInit, task 02).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Powerup_Quad, "Velox.Powerup.Quad", "Active Quad Damage (x3 outgoing damage).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Powerup_Haste, "Velox.Powerup.Haste", "Active Haste (x1.3 wish speed and fire rate).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Powerup_Regen, "Velox.Powerup.Regen", "Active Regeneration.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Powerup_BattleSuit, "Velox.Powerup.BattleSuit", "Active Battle Suit.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_MachineGun_Fire, "GameplayCue.Weapon.MachineGun.Fire", "MachineGun: shot fired (tracer from the eye to the ray end).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_MachineGun_Impact, "GameplayCue.Weapon.MachineGun.Impact", "MachineGun: impact at the ray end / explosion.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_Shotgun_Fire, "GameplayCue.Weapon.Shotgun.Fire", "Shotgun: shot fired (tracer from the eye to the ray end).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_Shotgun_Impact, "GameplayCue.Weapon.Shotgun.Impact", "Shotgun: impact at the ray end / explosion.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_PlasmaGun_Fire, "GameplayCue.Weapon.PlasmaGun.Fire", "PlasmaGun: shot fired (tracer from the eye to the ray end).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_PlasmaGun_Impact, "GameplayCue.Weapon.PlasmaGun.Impact", "PlasmaGun: impact at the ray end / explosion.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_RocketLauncher_Fire, "GameplayCue.Weapon.RocketLauncher.Fire", "RocketLauncher: shot fired (tracer from the eye to the ray end).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_RocketLauncher_Impact, "GameplayCue.Weapon.RocketLauncher.Impact", "RocketLauncher: impact at the ray end / explosion.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_LightningGun_Fire, "GameplayCue.Weapon.LightningGun.Fire", "LightningGun: shot fired (tracer from the eye to the ray end).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_LightningGun_Impact, "GameplayCue.Weapon.LightningGun.Impact", "LightningGun: impact at the ray end / explosion.");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_LightningGun_Beam, "GameplayCue.Weapon.LightningGun.Beam", "Lightning gun beam (WhileActive, server-driven, removed on release/switch/death).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_Railgun_Fire, "GameplayCue.Weapon.Railgun.Fire", "Railgun: shot fired (tracer from the eye to the ray end).");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(GameplayCue_Weapon_Railgun_Impact, "GameplayCue.Weapon.Railgun.Impact", "Railgun: impact at the ray end / explosion.");
}
