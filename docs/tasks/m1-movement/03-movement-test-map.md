# 03 — Greybox movement test map L_MovementTest

**Milestone:** M1 Movement
**Agent:** dev
**Status:** done (2026-10-24)
**Depends on:** ADR 0002 (parallel with 02)

## Goal
Implement ADR 0002 task **`03-movement-test-map`** exactly as specified in `docs/adr/0002-movement-component-design.md` → "Implementation notes for dev-hard", item 3. The ADR section references in that item (§1 tuning table, §2 override table, §3 FVeloxMoveSim, §5 prediction, §7 tests) are the specification; this file only pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Tools/editor/create_movement_test_map.py` (new), `Tools/editor/README.md`
- `Content/Maps/L_MovementTest.umap`, `Content/Materials/M_Greybox*` (generated, LFS)
- `Config/DefaultEngine.ini` (default map switch), `Config/DefaultGame.ini` (`+MapsToCook`)

## Relevant ADRs / references
- `docs/adr/0002-movement-component-design.md` (primary), `docs/adr/0001-project-and-module-structure.md` §3
- Engine source for API verification: `/opt/UnrealEngine-5.8/Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`
- Rules for all M1 tasks: never bypass the saved-move path; no engine-field magic numbers outside `SetMovementSettings`; report ADR disagreements instead of deviating.

## Acceptance criteria
- [ ] Script builds the map headlessly per ADR §7: 5120 cm strafe corridor with markers every 512 cm, ramps 10/20/30/45/50°, stairs 20/40/60 cm, 3 jump pads (placeholder actors until task 04 provides `AVeloxJumpPad`), 3 bunny-hop gaps, PlayerStart, static lights; unlit greybox materials.
- [ ] Script asserts actor counts and is idempotent (re-run replaces the map).
- [ ] Map loads in `-game -nullrhi` without errors; headless cook of the map succeeds.

## Verification steps
1. `infra/ci/build.sh` ends with `[ci] CI OK` (Editor + Game compile, all `Velox.*` automation tests green).
2. Task-specific checks listed above, with command output in the report.

## Report back
Standard format for the agent type; include the design summary (dev-hard) so `architect` can review without the diff.
