# 02 — Quake physics in UVeloxMovementComponent

**Milestone:** M1 Movement
**Agent:** dev-hard
**Status:** todo
**Depends on:** 01

## Goal
Implement ADR 0002 task **`02-cmc-quake-physics`** exactly as specified in `docs/adr/0002-movement-component-design.md` → "Implementation notes for dev-hard", item 2. The ADR section references in that item (§1 tuning table, §2 override table, §3 FVeloxMoveSim, §5 prediction, §7 tests) are the specification; this file only pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/Movement/VeloxMovementComponent.h/.cpp`
- `Source/Velox/Character/VeloxCharacter.h/.cpp`, `Source/Velox/Core/VeloxPawnData.h`
- `Config/DefaultEngine.ini`, `Config/DefaultGame.ini` (ADR §5 blocks only)
- `Tools/editor/create_data_assets.py` (DA_MovementSettings_Default, DA_PawnData_Default values)

## Relevant ADRs / references
- `docs/adr/0002-movement-component-design.md` (primary), `docs/adr/0001-project-and-module-structure.md` §3
- Engine source for API verification: `/opt/UnrealEngine-5.8/Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`
- Rules for all M1 tasks: never bypass the saved-move path; no engine-field magic numbers outside `SetMovementSettings`; report ADR disagreements instead of deviating.

## Acceptance criteria
- [ ] Every override in ADR §2 is implemented and the engine-default parts listed there are untouched; `SetMovementSettings` is the only place engine fields are written.
- [ ] `SetLocalMoveInput`/`SetJumpHeld` input path; character capsule/eye height from pawn data; `bUseControllerRotationYaw`.
- [ ] Editor + Game compile; unit tests green; after task 03 exists: `Velox L_MovementTest -game -nullrhi -unattended` runs 10 s with no `LogCharacterMovement` warnings.
- [ ] `architect` design review of the override table passes (lead dispatches).

## Verification steps
1. `infra/ci/build.sh` ends with `[ci] CI OK` (Editor + Game compile, all `Velox.*` automation tests green).
2. Task-specific checks listed above, with command output in the report.

## Report back
Standard format for the agent type; include the design summary (dev-hard) so `architect` can review without the diff.
