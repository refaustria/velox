# 07 — L_CombatTest map and target dummies

**Milestone:** M2 Combat
**Agent:** dev
**Status:** done (2026-10-25)
**Depends on:** ADR 0003 (parallel)

## Goal
Implement ADR 0003 implementation-notes task **`07-combat-test-map-dummies`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Tools/editor/create_combat_test_map.py` (new), `Content/Maps/L_CombatTest.umap`, `Source/Velox/Bots/VeloxDummyController.*` (new, simple strafing dummy using the movement component), `Config/DefaultGame.ini` (+MapsToCook)

## Acceptance criteria
- [ ] Headless script builds `L_CombatTest` (arena 40×40 m, cover blocks, 4 PlayerStarts, 3 dummy spawns at 10/20/30 m) and asserts counts; dummies strafe left/right at configurable speed via `SetLocalMoveInput`; map loads `-game -nullrhi` clean.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
