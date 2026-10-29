// Velox - item enums shared by the world-free pickup rules and the item definition asset (ADR 0004 section 1).

#pragma once

#include "CoreMinimal.h"

#include "VeloxItemTypes.generated.h"

/** What a pickup grants (Q3 itemType_t without holdables / team items). */
UENUM(BlueprintType)
enum class EVeloxItemType : uint8
{
	Health,
	Armor,
	Ammo,
	Weapon,
	Powerup,
};

/** Health items: the cap the pickup heals up to (Q3: only the 5 and the Mega stack past max health). */
UENUM(BlueprintType)
enum class EVeloxHealthCap : uint8
{
	/** The MaxHealth attribute (100). */
	MaxHealth,
	/** UVeloxCombatSettings::HealthHardCap (200). */
	HardCap,
};
