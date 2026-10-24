# 04 — Prediction details, knockback and jump pads

**Milestone:** M1 Movement
**Agent:** dev-hard
**Status:** todo
**Depends on:** 02, 03

## Goal
Implement ADR 0002 task **`04-prediction-knockback-jumppads`** exactly as specified in `docs/adr/0002-movement-component-design.md` → "Implementation notes for dev-hard", item 4. The ADR section references in that item (§1 tuning table, §2 override table, §3 FVeloxMoveSim, §5 prediction, §7 tests) are the specification; this file only pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/Movement/VeloxNetworkMoveData.h/.cpp`, `Source/Velox/Movement/VeloxMovementComponent.h/.cpp`
- `Source/Velox/Movement/VeloxJumpPad.h/.cpp` (new)
- `Source/Velox/Tests/VeloxSavedMoveTest.cpp`, `Source/Velox/Tests/VeloxJumpPadTest.cpp`
- `Tools/editor/create_movement_test_map.py` (swap placeholder pads for `AVeloxJumpPad`)

## Relevant ADRs / references
- `docs/adr/0002-movement-component-design.md` (primary), `docs/adr/0001-project-and-module-structure.md` §3
- Engine source for API verification: `/opt/UnrealEngine-5.8/Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`
- Rules for all M1 tasks: never bypass the saved-move path; no engine-field magic numbers outside `SetMovementSettings`; report ADR disagreements instead of deviating.

## Acceptance criteria
- [ ] `FSavedMove_Velox` fields/flags and `CanCombineWith` rules per ADR §5; `FVeloxMoveResponseDataContainer` + `OnClientCorrectionReceived` carry knockback + grace to the client.
- [ ] `AddKnockback`/`ConsumePendingKnockback`/grace timer per ADR §4; `velox.move.TestKnockback` console command.
- [ ] `AVeloxJumpPad` with AimAtTarget|FixedVelocity, deterministic on client and server, idempotent on replay.
- [ ] Tests `SavedMoveCombine` and `JumpPad` green; correction counters exposed for task 05.
- [ ] `architect` review passes.

## Verification steps
1. `infra/ci/build.sh` ends with `[ci] CI OK` (Editor + Game compile, all `Velox.*` automation tests green).
2. Task-specific checks listed above, with command output in the report.

## Report back
Standard format for the agent type; include the design summary (dev-hard) so `architect` can review without the diff.
