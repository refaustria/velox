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

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
