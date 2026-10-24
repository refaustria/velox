# 01 — GAS foundation: attribute sets, damage math, effects, health component

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** todo
**Depends on:** ADR 0003

## Goal
Implement ADR 0003 implementation-notes task **`01-gas-foundation`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Source/Velox/Combat/` (new files: VeloxHealthSet, VeloxAmmoSet, VeloxCombatSettings, VeloxDamageMath, VeloxDamageExecution, VeloxGameplayEffects (7 C++ GEs), VeloxHealthComponent, VeloxGameplayAbility base, ASC input routing)
- `Source/Velox/Combat/VeloxAbilitySystemComponent.*`, `VeloxAbilitySet.*` (fill in), `Source/Velox/GameModes/VeloxPlayerState.*`, `VeloxPlayerController.*` (ProcessAbilityInput), `Source/Velox/Character/VeloxCharacter.*` (health component, IGameplayCueInterface stub), `Core/VeloxGameplayTags.*`, `Config/DefaultGame.ini`, `Tools/editor/create_bootstrap_content.py` (DA_CombatSettings_Default), `Source/Velox/Tests/VeloxCombatTests.cpp`

## Acceptance criteria
- [ ] Unit tests `Velox.Combat.DamageMath` (Q3 table incl. armor 2/3, caps, self-damage ×0.5 after knockback, per-pellet rounding), `Velox.Combat.Ammo`, `Velox.Combat.KnockbackSeq` green.
- [ ] Spawn: 125 health decaying 1/s to 100; armor cap 200. `velox.combat.TestDamage <amount>` applies the Q3 table and produces knockback through the single `AddKnockback` entry point.
- [ ] Abilities granted once per PlayerState; respawn re-inits actor info without re-granting.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
