// Velox - plain bot types of the world-free cores and the controller glue (ADR 0005 sections 1, 2). No UObject, no
// world, no UHT. The difficulty POD and the weapon preference row live in Bots/VeloxBotTypes.h (task M4-03).

#pragma once

#include "CoreMinimal.h"

/** Bot goal kinds (ADR 0005 section 2). */
enum class EVeloxBotGoal : uint8
{
	None,
	Item,
	Fight,
	Roam,
};

/** Short lower-case name for logs (`goal=` in the debug line). */
inline const TCHAR* LexToString(EVeloxBotGoal Goal)
{
	switch (Goal)
	{
	case EVeloxBotGoal::Item: return TEXT("item");
	case EVeloxBotGoal::Fight: return TEXT("fight");
	case EVeloxBotGoal::Roam: return TEXT("roam");
	default: return TEXT("none");
	}
}

/**
 * Everything a bot sends to its pawn in one server tick (ADR 0005 section 1), applied by AVeloxBotController::ApplyInput
 * through the player entry points only.
 */
struct FVeloxBotInput
{
	/** Unit XY wish direction in world space, or zero. */
	FVector2D WishDirWorld = FVector2D::ZeroVector;
	/** 0..1, scales AddMovementInput. */
	float WishScale = 0.f;
	bool bJumpHeld = false;
	bool bCrouch = false;
	/** Control rotation (pitch included). */
	FRotator View = FRotator::ZeroRotator;
	bool bFireHeld = false;
	/** Weapon slot to select this tick (0 = none). */
	int32 SelectSlot = 0;
};
