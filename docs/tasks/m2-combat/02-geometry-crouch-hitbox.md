# 02 — Class-default geometry, crouch, hitbox component, linear smoothing

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** done (2026-10-25)
**Depends on:** 01

## Goal
Implement ADR 0003 implementation-notes task **`02-geometry-crouch-hitbox`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Source/Velox/Character/VeloxCharacter.*`, `Source/Velox/Core/VeloxPawnData.h` (remove geometry fields), `Source/Velox/Movement/VeloxMovementComponent.*` (CanCrouchInCurrentState ground-only, crouch speed scale already exists), `Source/Velox/Combat/VeloxHitboxComponent.*`, `VeloxHitMath.*` (new), `Tools/editor/create_bootstrap_content.py` (IA_Crouch, pawn data), `Config/*.ini`, tests

## Acceptance criteria
- [ ] Capsule 37.5/70 standing, 50 crouched, eye 55/40 as class defaults; crouch ground-only; `IA_Crouch` mapped (Ctrl).
- [ ] `UVeloxHitboxComponent` exposes the single capsule as `FVeloxHitShape`; `Velox.Combat.HitMath` green; pawn capsules ignore the `Weapon` channel.
- [ ] `NetworkSmoothingMode=Linear`; all M1 tests green and `nettest.sh` full list 1/1 unchanged.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Review notes from task 01 (architect, 2026-10-25; ADR 0003 amendments B1–B9)
- No blocker from 01. `FVeloxDamageMath::ClosestCapsuleDistance(Origin, Centre, Radius, HalfHeight)` already exists (`Combat/VeloxDamageMath.h`); the `FVeloxHitShape` overload of ADR §2 forwards to it, do not duplicate the math.
- Keep `UVeloxHitboxComponent` free of any ASC dependency; it registers on authority in `BeginPlay` and unregisters on death (task 06 calls it from `HandleDeath`) and `EndPlay`.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
