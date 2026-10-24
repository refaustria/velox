# 05 — Debug tooling, scripted input, determinism test, headless net soak

**Milestone:** M1 Movement
**Agent:** dev
**Status:** todo
**Depends on:** 02, 04

## Goal
Implement ADR 0002 task **`05-debug-tooling-and-net-soak`** exactly as specified in `docs/adr/0002-movement-component-design.md` → "Implementation notes for dev-hard", item 5. The ADR section references in that item (§1 tuning table, §2 override table, §3 FVeloxMoveSim, §5 prediction, §7 tests) are the specification; this file only pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/UI/VeloxHUD.h/.cpp`, `Source/Velox/Movement/VeloxMoveDebug.h/.cpp` (new), `Source/Velox/Movement/VeloxScriptedInputComponent.h/.cpp` (new)
- `Source/Velox/Tests/` (FT_MovementDeterminism functional test), `Content/Data/MoveScripts/StrafeRun.json`
- `Tools/analyze_run.py`, `infra/ci/nettest.sh` (new), `infra/ci/build.sh` (`--net-soak` flag)

## Relevant ADRs / references
- `docs/adr/0002-movement-component-design.md` (primary), `docs/adr/0001-project-and-module-structure.md` §3
- Engine source for API verification: `/opt/UnrealEngine-5.8/Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`
- Rules for all M1 tasks: never bypass the saved-move path; no engine-field magic numbers outside `SetMovementSettings`; report ADR disagreements instead of deviating.

## Acceptance criteria
- [ ] HUD speed block, `showdebug VeloxMovement`, CSV recorder + `Tools/analyze_run.py`, `velox.move.*` cvars mirroring every settings field (non-shipping), `DumpSettings`, `Preset Q3|CPM`.
- [ ] `UVeloxScriptedInputComponent` replays a JSON input script; `FT_MovementDeterminism` passes (same script → same end state on listen-server and client simulation).
- [ ] `infra/ci/nettest.sh` runs dedicated server + `-nullrhi` client on this VM with 100 ms / 2 % packet simulation and prints the `VeloxNetSoak` line within the ADR thresholds; wired into `build.sh --net-soak`.

## Verification steps
1. `infra/ci/build.sh` ends with `[ci] CI OK` (Editor + Game compile, all `Velox.*` automation tests green).
2. Task-specific checks listed above, with command output in the report.

## Report back
Standard format for the agent type; include the design summary (dev-hard) so `architect` can review without the diff.
