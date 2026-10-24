// Velox - custom collision channels. Must match [/Script/Engine.CollisionProfile] in Config/DefaultEngine.ini.

#pragma once

#include "Engine/EngineTypes.h"

/** Hitscan trace channel. World geometry blocks it; pawn capsules ignore it (hits on pawns are resolved by lag compensation). */
inline constexpr ECollisionChannel Velox_TraceChannel_Weapon = ECC_GameTraceChannel1;

/** Projectile object channel. Projectiles collide with pawn capsules at current server time. */
inline constexpr ECollisionChannel Velox_ObjectChannel_Projectile = ECC_GameTraceChannel2;
