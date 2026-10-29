// Velox - non-shipping weapon test commands (task 04), shared by the console commands and the automation tests:
//   velox.combat.GiveAllWeapons
//   velox.combat.FireTest <weapon> [AimDistanceCm]
//   velox.combat.DebugHoldShotRpc <ms> <slot> [count=1]   (server; ADR 0003 B35/B36 forced loss, tasks 08f/08g; hitscan and projectile)
// <weapon>: MG SG PG RL LG RG, a slot number, or a tag leaf (Railgun). AimDistanceCm: absent/-1 = aim the control
// rotation at the nearest other living Velox pawn; 0 = fire along the current control rotation; > 0 = also move the
// shooter to that distance from the pawn along the line between them (no sweep: cover may still block). Listen server / standalone (authority world); fires through the real ability.

#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class AActor;
class AVeloxCharacter;
class AVeloxPlayerState;
class UVeloxWeaponTable;

namespace VeloxWeaponCheats
{
	/** Table index for a weapon name (see above); INDEX_NONE if unknown. */
	VELOX_API int32 ResolveWeapon(const UVeloxWeaponTable& Table, const FString& Name);

	/** Server: owns every weapon of the table, ammo at the cap. */
	VELOX_API bool GiveAllWeapons(AVeloxPlayerState& PlayerState);

	/**
	 * Server: selects the weapon (no switch delay; owns it first if needed), optionally aims at / moves towards AimAt,
	 * and activates its fire ability once. False (OutError set) if the activation did not happen.
	 */
	VELOX_API bool FireTestShot(AVeloxCharacter& Shooter, const FString& WeaponName, AActor* AimAt, float PlaceDistanceCm, FString& OutError);

	/**
	 * velox.combat.DebugHoldShotRpc (ADR 0003 B35, review m2-late-rpc section 4 "Forced loss in tests"): the next Count
	 * target-data RPCs of a weapon in Slot are processed Seconds later on the server (the fire ability instance waits, as
	 * for a real reliable resend). Count <= 0 or Seconds <= 0 disarms. Process-wide.
	 */
	VELOX_API void SetShotRpcHold(float Seconds, int32 Slot, int32 Count);

	/** Fire abilities (server): true and the hold time when the target data of a weapon in Slot must be held; uses one count. */
	VELOX_API bool ConsumeShotRpcHold(int32 Slot, float& OutSeconds);
}

#endif // !UE_BUILD_SHIPPING
