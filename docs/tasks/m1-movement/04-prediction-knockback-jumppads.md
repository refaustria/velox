# 04 — Prediction details, knockback and jump pads

**Milestone:** M1 Movement
**Agent:** dev-hard
**Status:** review: fix-needed (architect, 2026-10-24) — apply D1–D3 below, rebuild, then done
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
- [ ] `architect` review passes — **fix-needed** (2026-10-24): D1–D3 below are required before task 05; D4–D6 are tidy-ups in the same pass.

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

## Instructions added by the task-04 design review (architect, 2026-10-24) — ADR 0002 A17–A22

Verdict: **fix-needed**. Defects ranked; B = blocker for the net soak / playtest. Engine line numbers refer to UE 5.8.4 `CharacterMovementComponent.cpp`.

- **D1 (B) — Replay model (A17).** `VeloxNetworkMoveData.cpp` `FSavedMove_Velox::PrepMoveFor` (l.293–306): remove `SetJumpLatched(bSavedJumpLatched)` and `SetPendingKnockback(SavedPendingKnockback)`; the latch and the pending knockback are derived state and chain from the correction like `KnockbackGrace` and `Velocity` (the replayed pad overlap re-fires on its own). The three flag restores are redundant with `UpdateFromCompressedFlags` in `MoveAutonomous` (l.10677); drop them too. `VeloxMovementComponent.cpp` `OnClientCorrectionReceived` (l.451–458): delete the `SavedMoves[0]` patch. Keep `bSavedJumpLatched`/`SavedPendingKnockback`/`SavedKnockbackGrace` in the saved move (`CanCombineWith`, `bForceNoCombine`, debug). Update the header comments (`VeloxNetworkMoveData.h` l.57–58, l.85–101; `VeloxMovementComponent.h` l.52, l.90) and the `SetJumpLatched`/`SetPendingKnockback` doc strings ("correction only").
- **D2 (B) — Response snapshot at check time (A18).** `VeloxMovementComponent.h/.cpp`: override `ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel, const FVector& RelativeClientLoc, FMovementBaseInterfaceData* ClientMovementBaseInterfaceData, FName ClientBaseBoneName, uint8 ClientMovementMode)` (header l.2429; `using Super::ServerMoveHandleClientError;` for the deprecated overload): call `Super`, then `FNetworkPredictionData_Server_Character* ServerData = GetPredictionData_Server_Character(); if (ServerData && !ServerData->PendingAdjustment.bAckGoodMove && ServerData->PendingAdjustment.TimeStamp == ClientTimeStamp)` copy `KnockbackGrace`, `bJumpLatched`, `PendingKnockback`, `ClientTimeStamp` into a new `FVeloxCorrectionState ServerCorrectionState` member (declare the struct in `VeloxNetworkMoveData.h`; expose `GetServerCorrectionState()`). `VeloxNetworkMoveData.cpp` `ServerFillResponseData` (l.341–356): use the snapshot when `!PendingAdjustment.bAckGoodMove && Snapshot.TimeStamp == PendingAdjustment.TimeStamp`, else the live values. Reason: `SendClientAdjustment` (l.11055) serialises at replication time after all ServerMove RPCs of the frame; the engine snapshots `NewLoc/NewVel/MovementMode` in `ServerMoveHandleClientError` (l.10405–10460) and so must we.
- **D3 (B) — No overlap knockback during the correction teleport (A19).** `VeloxMovementComponent`: add `uint8 bApplyingCorrection : 1` (transient, owning client). Set it in `OnClientCorrectionReceived` (before returning, in the correction branch); clear it at the top of `ClientUpdatePositionAfterServerUpdate` (before `Super`) and at the top of `PerformMovement`. `AddKnockback` (l.199–207): when `bApplyingCorrection` and the owner is an autonomous proxy, log Verbose and return without queuing. Reason: `ClientAdjustPosition_Implementation` teleports *after* `OnClientCorrectionReceived` (l.11322 → l.11325) and the teleport dispatches `BeginOverlap` immediately; the server's response already contains every entry it saw.
- **D4 — `VeloxJumpPad.h` l.52–56 `GetLandingPoint` doc:** state that `AimAtTarget` returns the apex (the pawn lands beyond it); M4 computes the landing. No code change.
- **D5 — `VeloxNetworkMoveData.h` l.131–134 container comment:** add "values are the server's state at the time of the error check (snapshot), see `ServerCorrectionState`".
- **D6 — `VeloxSavedMoveTest.cpp`:** add one assertion block documenting A17: construct an `FSavedMove_Velox` with `bSavedJumpLatched = true` and a pending knockback, and assert that `GetCompressedFlags()` carries neither (already there) **and** add a comment that `PrepMoveFor` is `Super` only (no world-free test is possible for the replay itself; the net soak's `LatchRun`/`PadRun` scripts in task 05 cover it).

Accepted without change (A20–A22): `CanCombineWith` order and the `bForceNoCombine` on pending knockback; `SerializeOptionalValue` usage and bit budget; `OnClientCorrectionReceived` overload and time-stamp guard; `ClientUpdatePositionAfterServerUpdate` input save/restore; pad fire-once-per-entry on both ends with trigger-centre velocity; `ConsumePendingKnockback` before `MoveStartKnockbackGrace`; visible `PadMesh`; pad A target over the near edge; class-default gravity in `CheckForErrors`; `TestKnockback` non-shipping scoping.

Report back after the fix: build green, 15 tests green, and a one-paragraph confirmation of the three replay cases of A17 against the new code (no `architect` re-review needed unless the implementer disagrees with A17–A19).
