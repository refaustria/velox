# 02 — Quake physics in UVeloxMovementComponent

**Milestone:** M1 Movement
**Agent:** dev-hard
**Status:** done (2026-10-24, architect-reviewed)
**Depends on:** 01

## Goal
Implement ADR 0002 task **`02-cmc-quake-physics`** exactly as specified in `docs/adr/0002-movement-component-design.md` → "Implementation notes for dev-hard", item 2. The ADR section references in that item (§1 tuning table, §2 override table, §3 FVeloxMoveSim, §5 prediction, §7 tests) are the specification; this file only pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/Movement/VeloxMovementComponent.h/.cpp`
- `Source/Velox/Character/VeloxCharacter.h/.cpp`, `Source/Velox/Core/VeloxPawnData.h`
- `Config/DefaultEngine.ini`, `Config/DefaultGame.ini` (ADR §5 blocks only)
- `Tools/editor/create_bootstrap_content.py` (DA_MovementSettings_Default, DA_PawnData_Default values; the ADR's `create_data_assets.py` is this file)

## Relevant ADRs / references
- `docs/adr/0002-movement-component-design.md` (primary, **including the Amendments section A1–A8**), `docs/adr/0001-project-and-module-structure.md` §3
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

## Instructions added by the task-01 design review (architect, 2026-10-24)
- Build `FVeloxMoveParams` once per `PerformMovement` (and in `SetMovementSettings`), cache it in a member, reuse it for every `CalcVelocity` call of that move (A8). Never call `BuildSimParams()` from `CalcVelocity`.
- `CalcVelocity` builds one `FVeloxMoveInput` from `Acceleration` (WishDir = `Acceleration.GetSafeNormal2D()`, WishSpeed = `Acceleration.Size2D()` × crouch scale), `bHasForwardInput/bHasSideInput`, `bWantsJumpHeld`; pass the start-of-move `KnockbackGrace` to `GroundMove` for every call of the move and decrement `KnockbackGrace` once per `PerformMovement` (A7). Engine `PhysFalling` (l.4933) zeroes `Velocity.Z` and substitutes `FallAcceleration` around `CalcVelocity`; rely on that, do not touch Z in the falling branch.
- `bAutoHop == false`: `ProcessLanded` never auto-jumps; jumping then needs a fresh press through the engine's `CheckJumpInput`/`bPressedJump` edge (`JumpMaxHoldTime = 0`). `bAutoHop == true`: `bWantsJumpHeld` on landing jumps in the landing frame as in §2 (A3). A jump always leaves the rest of the move as `PhysFalling` → air acceleration (A2).
- Apply ADR §1 table value `AirControlScale = 80` in `DA_MovementSettings_Default` (A5; the task-01 follow-up changes the C++ default and the bootstrap script).

## Follow-ups from the task-02 design review (architect, 2026-10-24) — verdict: pass with fixes
Small, independent of task 04; do them in one `dev-hard` (or `dev`) pass before the human playtest. ADR 0002 amendments A9–A16 are the specification.
- **A13 (fix before playtest):** delete the `StepUp` override (`VeloxMovementComponent.h` declaration + `.cpp` l.360–369). The engine's displacement recompute preserves speed on clean steps and clips velocity on wall slides (Q3 `PM_SlideMove`); the override suppressed wall clipping for every ground wall hit.
- **A9:** in `ShouldCatchAir`, replace the `CatchAirNormalZMargin = 0.01` test with the Q3 kick-off rule: `Zc = ComputeRampExitVelocityZ(Old normals)`; catch air iff `Zc > 0 && ((FVector(Velocity.X, Velocity.Y, Zc) | NewFloor.HitResult.ImpactNormal) > SimParams.RampSlideMinNormalSpeed)`. Remove the constant.
- **A11:** `NavAgentProps.bCanCrouch = false` in the `AVeloxCharacter` constructor (replaces the current `= true`). Keep the `Input_Crouch*` handlers and `CrouchSpeedScale`.
- **A10 / A15:** `GetMaxSpeed()` applies the crouch scale in Walking/NavWalking only (plain `MaxGroundSpeed` in Falling); `GetMaxAcceleration()` and `GetMaxBrakingDeceleration()` return Velox values only in Walking/NavWalking/Falling, else `Super`.
- Acceptance: build + 12 tests green; `L_MovementTest -game -nullrhi` clean. Task 05 adds the regression checks (stairs ≥ 98 % speed retention, 45° wall slide settles at ≈ 566 cm/s).
