# 01 — World-free movement simulation core and settings

**Milestone:** M1 Movement
**Agent:** dev-hard
**Status:** todo
**Depends on:** ADR 0002

## Goal
Implement ADR 0002 task **`01-move-sim`** exactly as specified in `docs/adr/0002-movement-component-design.md` → "Implementation notes for dev-hard", item 1. The ADR section references in that item (§1 tuning table, §2 override table, §3 FVeloxMoveSim, §5 prediction, §7 tests) are the specification; this file only pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/Movement/VeloxMoveSim.h/.cpp` (new)
- `Source/Velox/Movement/VeloxMovementSettings.h` (extend/rename fields per ADR §1, add `BuildSimParams()`)
- `Source/Velox/Tests/VeloxMoveSimTest.cpp`

## Relevant ADRs / references
- `docs/adr/0002-movement-component-design.md` (primary), `docs/adr/0001-project-and-module-structure.md` §3
- Engine source for API verification: `/opt/UnrealEngine-5.8/Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`
- Rules for all M1 tasks: never bypass the saved-move path; no engine-field magic numbers outside `SetMovementSettings`; report ADR disagreements instead of deviating.

## Acceptance criteria
- [ ] `VeloxMoveSim.h` includes only `CoreMinimal.h`; all functions of ADR §3 exist with the given signatures; fixed 8 ms substep integration (`StepFrame`) toggleable.
- [ ] `UVeloxMovementSettings` has every field of the ADR §1 tuning table with the Q3 defaults converted via UnitScale 2.5; `BuildSimParams()` is the only conversion point.
- [ ] All `Velox.Movement.*` unit tests of ADR §7 except `SavedMoveCombine` and `JumpPad` exist and pass headlessly (friction closed form, accel per tick, strafe gain bounded, bunny-hop retention, ramp clip, knockback grace, determinism).

## Verification steps
1. `infra/ci/build.sh` ends with `[ci] CI OK` (Editor + Game compile, all `Velox.*` automation tests green).
2. Task-specific checks listed above, with command output in the report.

## Report back
Standard format for the agent type; include the design summary (dev-hard) so `architect` can review without the diff.
