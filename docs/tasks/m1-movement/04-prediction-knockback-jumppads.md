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
- `docs/adr/0002-movement-component-design.md` (primary, **including the Amendments section A1–A8**), `docs/adr/0001-project-and-module-structure.md` §3
- Engine source for API verification: `/opt/UnrealEngine-5.8/Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`
- Rules for all M1 tasks: never bypass the saved-move path; no engine-field magic numbers outside `SetMovementSettings`; report ADR disagreements instead of deviating.

## Acceptance criteria
- [ ] `FSavedMove_Velox` fields/flags and `CanCombineWith` rules per ADR §5; `FVeloxMoveResponseDataContainer` + `OnClientCorrectionReceived` carry knockback + grace to the client.
- [ ] `AddKnockback`/`ConsumePendingKnockback`/grace timer per ADR §4; `velox.move.TestKnockback` console command.
- [ ] `AVeloxJumpPad` with AimAtTarget|FixedVelocity, deterministic on client and server, idempotent on replay; `MaxLaunchSpeed` validation (editor `CheckForErrors` map-check warning + one-time runtime warning when the computed launch speed exceeds it or is zero) per ADR §4/A4.
- [ ] Tests `SavedMoveCombine` and `JumpPad` green (`JumpPad` covers the five cases of ADR §7: above/apex, vertical, level 45°, below, unsolvable); correction counters exposed for task 05.
- [ ] `architect` review passes.

## Verification steps
1. `infra/ci/build.sh` ends with `[ci] CI OK` (Editor + Game compile, all `Velox.*` automation tests green).
2. Task-specific checks listed above, with command output in the report.

## Report back
Standard format for the agent type; include the design summary (dev-hard) so `architect` can review without the diff.

## Instructions added by the task-01 design review (architect, 2026-10-24)
- `VeloxMoveSim::JumpPadLaunchVelocity` keeps its three-parameter signature: Q3 `AimAtTarget` above the pad, minimum-launch-speed arc level/below (A4). Do not add an apex-height parameter in M1; mappers use `FixedVelocity` when they need a different arc.
- `ComputeLaunchVelocity` uses the trigger centre as `From` (Q3); the pawn launches from its capsule centre at the next `PerformMovement`. Accepted error; `GetLandingPoint()` uses the same approximation.
- Pad launches go through `AddKnockback(…, bOverrideVelocity)` and therefore set a knockback grace; no special-casing (the pawn is airborne longer than `KnockbackGraceMax`).
- `ConsumePendingKnockback`: `KnockbackGrace = max(KnockbackGrace, GraceFor(impulse))` (A7, deliberate deviation from Q3's set-if-idle).

## Instructions added by the task-02 design review (architect, 2026-10-24) — ADR 0002 A9, A12, A14
- **Replay rule (A14).** `PrepMoveFor` restores *inputs* only: `bSavedJumpHeld`, `bSavedHasForwardInput/SideInput` (→ `FLAG_Custom_1/2`, also read in `UpdateFromCompressedFlags`), `bSavedJumpLatched`, and `PendingKnockback` by **assignment** (never `+=`). It must **not** restore `KnockbackGrace`: that is derived state and chains from the correction (`OnClientCorrectionReceived` sets it from `FVeloxMoveResponseDataContainer`, each replayed `PerformMovement` decrements it). `SavedKnockbackGrace` is recorded for `CanCombineWith` and debugging only. `SavedRampExitVelocityZ` is dropped (the ramp value is stateless within a move, A9).
- **Jump latch (A12).** Add `uint8 bJumpLatched : 1` to `UVeloxMovementComponent`: set in `DoJump` when it returns true; cleared at the start of `PerformMovement` when `!bWantsJumpHeld`; `ProcessLanded` auto-jumps iff `bWantsJumpHeld && (SimParams.bAutoHop || !bJumpLatched)` (keep the existing `GroundMovementMode == Walking && !bWantsToCrouch` guards). Saved move: `bSavedJumpLatched` (start-of-move value, captured in `SetMoveFor`, restored in `PrepMoveFor`); `CanCombineWith` returns false when it differs. Response container: serialise `bJumpLatched` next to `KnockbackGrace` (1 bit) and apply it in `OnClientCorrectionReceived`. Not sent client→server (the server derives it from the flag stream).
- **CanCombineWith (ADR §5, unchanged list + latch):** jump-held differs; latch differs; either move has a pending knockback; either `SavedKnockbackGrace > 0`; either start mode Falling and `!bCombineAirMoves`; input-presence flags differ; then `Super`. Mirror the engine's `PendingLaunchVelocity` → `bForceNoCombine` idea for pending knockback in `SetMoveFor`.
- **Tests:** `SavedMoveCombine` covers the latch rule; add a `Velox.Movement.JumpLatch` case on hand-built moves or a CMC-level check in `FT_MovementDeterminism` (task 05): VQ3 mode (`bAutoHop=false`), jump pressed mid-air after a release → the landing move jumps (XY unchanged, Z = JumpVelocity + gravity for the remainder); jump held through a landing without release → no jump.
