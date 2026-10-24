# 04 — Weapon definitions, inventory, fire/switch abilities, hitscan weapons

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** todo
**Depends on:** 01, 03

## Goal
Implement ADR 0003 implementation-notes task **`04-weapons-hitscan`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/` (new: VeloxWeaponDefinition, VeloxWeaponTable, VeloxWeaponInventoryComponent, VeloxGameplayAbility_Fire*, _Switch, VeloxFireRateBucket, target data), `Source/Velox/Combat/VeloxAbilitySystemComponent.*` (route InputTag.Fire to current weapon), `Core/VeloxGameplayTags.*`, `Tools/editor/create_bootstrap_content.py` (6 DA_Weapon_*), tests

## Acceptance criteria
- [ ] MG, SG (pellet pattern), LG (beam, tick damage), RG (instant rail) damage a dummy headless; fire-rate token bucket rejects 0 legitimate shots and all over-rate shots; `ClientConfirmHit` hit markers.
- [ ] `Velox.Combat.FireRateBucket` green; shots travel over GAS target-data RPCs, never over moves; architect review.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Review notes from task 01 (architect, 2026-10-25; ADR 0003 amendments B1–B9 are binding)
- **Prerequisite:** follow-up `01b` (armor constant B2, input call site B6) must be merged first; the acceptance numbers below assume ⌈0.66 d⌉ (100 vs 100 armor → 66/34).
- **B6:** ability input is processed in `AVeloxPlayerController::PlayerTick` after `Super` (after `UpdateRotation`), not in `PostProcessInput`. Fire abilities take the origin from `Pawn->GetPawnViewLocation()` and the direction from `Controller->GetControlRotation().Vector()`; never from the camera manager. `ViewServerTime` and the hint stamp are sampled there too.
- **B5:** delete `FVeloxAmmoTypeInfo::SpawnAmount`; `VeloxEffects::MakeSpawnInitSpec` sums `UVeloxCombatSettings::SpawnLoadout[].Ammo` per `Def->AmmoTag`; `ResetToLoadout` grants from the same list. Update `Velox.Combat.Ammo` and the bootstrap script accordingly.
- Fire filter: implement `UVeloxAbilitySystemComponent::CanRouteInputToSpec` (hook exists) — for `InputTag.Fire` only the spec whose `SourceObject` is the inventory's current `UVeloxWeaponDefinition`. `ProcessAbilityInput` gates on `Velox.State.Dead` (01b); fire abilities also carry `ActivationBlockedTags = {Velox.State.Dead, Velox.State.WeaponSwitching}`.
- Damage specs go through `VeloxEffects::MakeDamageSpec(SourceASC, FVeloxDamageSpecParams)` (`Combat/VeloxGameplayEffects.h`) — `Damage = Def.Damage`, `PelletHits = n`, `Direction = Dir`, `KnockbackScale`, `ShotSeq`, `DamageTag`; nothing else builds `GE_Damage`.
- The LG beam is a WhileActive cue; it must be removed on death/unpossess (task 06 calls `RemoveAllGameplayCues` before clearing the avatar, B8) and on weapon switch.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
