# 05 — Debug tooling, scripted input, determinism test, headless net soak

**Milestone:** M1 Movement
**Agent:** dev
**Status:** todo
**Depends on:** 02, 04 (including the task-04 review fixes D1–D3, ADR 0002 A17–A19)

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

## Instructions added by the task-02 design review (architect, 2026-10-24) — ADR 0002 A9, A12, A13
Add these scripted checks to `FT_MovementDeterminism` / `Content/Data/MoveScripts/` (all headless, `-UseFixedTimeStep -FPS=120`):
- **Stairs (A13):** run the 20 cm and 40 cm flights at ≥ 800 cm/s; speed after the flight ≥ 98 % of the speed before. The 60 cm flight must stop the pawn.
- **Wall slide (A13):** run at 45° into a corridor wall for 1 s; `GetSpeed2D()` settles at `MaxGroundSpeed · cos 45°` ± 5 % (velocity is clipped, not merely blocked).
- **Ramp exit (A9):** run up the 20° and 30° ramps onto their platforms; the pawn becomes airborne at the top with `Velocity.Z ≈ speed · tan(angle)` (± 10 %) and lands on the platform; running across the 10° ramp sideways does not leave the ground.
- **Jump latch (A12), VQ3 preset:** script row with jump pressed mid-air (after a release) and held through landing → jump in the landing move, XY speed unchanged; jump held continuously through a landing → no jump, friction applied.
- `showdebug VeloxMovement` and the HUD block show `bJumpLatched` and the ramp climb rate of the last exit.

## Instructions added by the task-04 design review (architect, 2026-10-24) — ADR 0002 A17–A22: what `nettest.sh` must verify

**Preconditions.** Task 04's D1–D3 are merged. `DefaultEngine.ini` pins `p.NetUsePackedMovementRPCs=1` under `[ConsoleVariables]` (the response container only exists on the packed path). Both processes run the same movement settings; where a script needs the VQ3 preset, pass the same `-ExecCmds="velox.move.Preset Q3"` to server and client.

**Topology.** Dedicated server `Velox L_MovementTest -server -nullrhi -log -port=7777 -UseFixedTimeStep -FPS=60`; one client `Velox 127.0.0.1:7777 -game -nullrhi -nosound -unattended -UseFixedTimeStep -FPS=120 -VeloxScript=<name>`. Packet simulation on the client in **both directions**: `PktLag=100 PktLagVariance=20 PktLoss=2 PktIncomingLagMin=80 PktIncomingLagMax=120 PktIncomingLoss=2` (RTT ≈ 200 ms, 2 % loss each way; the engine's `PktLag/PktLoss` apply to outgoing packets only). Verify `-ini:Engine:[PacketSimulationSettings]:...` is picked up under `DO_ENABLE_NET_TEST`; fallback: the scripted component execs `Net PktLag=100` etc. after possession. The server runs at 60 Hz and the client at 120 Hz on purpose: two client RPCs per server frame is the condition A18 fixes.

**Server-side knockback from a script (test-only RPC).** `UVeloxScriptedInputComponent` (non-shipping) gets `UFUNCTION(Server, Reliable, WithValidation) void ServerApplyTestKnockback(FVector Impulse, bool bOverride)`; validation rejects `|Impulse| > 20 × MaxGroundSpeed`; the implementation calls `AddKnockback` on the server's pawn — the production path for M2 rockets, so this does not bypass the saved-move path. Script row key: `"serverKnockback": [x, y, z], "override": false`. The client logs `VeloxNetSoak event=knockback t=<client time>` when it sends it.

**Instrumentation.** The CMC's correction counters exist (task 04). The scripted component additionally records every correction as `VeloxCorrection t=<s> cm=<d>` (from `GetLastCorrectionTime/Cm`) and evaluates per-event windows itself; at script end it prints `VeloxNetSoak: script=<name> moves=N corrections=C max=X mean=Y result=PASS|FAIL reason=...` and quits with exit code 0/1. `nettest.sh` fails on a non-zero client exit, a server crash, any `Ensure condition failed`, any `LogNetPlayerMovement: Warning` containing `TimeStamp expired` or `discrepancy`, or a missing `VeloxNetSoak` line (timeout 120 s).

**Scripts and pass criteria** (`N` = client moves, `L_exp = 0.02 × N` expected lost client→server moves; "a stray correction" = at most one correction ≤ 5 cm inside the window, the allowance for a lost move):
1. `StrafeRun` (existing, `PlayerStart_Corridor`): `C ≤ 3 × L_exp`, `X ≤ 30 cm`, `Y ≤ 5 cm` (ADR §5 thresholds); peak `GetSpeed2D() ≥ 1.4 × MaxGroundSpeed` on the client with no correction > 5 cm in the 0.5 s after the peak (the server agrees with the strafe gain).
2. `PadRun` (`PlayerStart_Pads` at (−300, −2500, 100)): walk onto pad B (500, −2500) and land on its target marker; walk back, onto pad A (500, −1500) and land on `JumpPadA_Platform`; drop off, onto pad C (500, −3500) and land on `JumpPadC_Ledge`. Per pad: the client log shows exactly one `VeloxJumpPad` launch line and the server log exactly one for the same pad; in `[t_launch, t_launch + 0.6 s]` only a stray correction is allowed; the script's landing checkpoint (pawn location inside the platform's XY bounds, on the ground) passes. This is the "pad launch without correction" criterion of ADR §4.
3. `KnockbackRun` (`PlayerStart_Corridor`): stand still 1 s on the ground; send `serverKnockback [1250, 0, 600]`; wait 1.5 s. Pass: exactly one correction in `[t, t + 0.5 s]` (the intended one; a second stray ≤ 5 cm allowed), excluded from `X`/`Y`; after that correction the client is airborne within one move, and `GetKnockbackGrace()` on the client right after the replay is `> 0.1 s` (the response container delivered the grace — with no container the value would be 0); `GetSpeed2D()` 0.15 s after the correction `≥ 0.9 × 1250` (friction suppressed client-side; with friction on it would be ≈ 0.4 × 1250); **no further correction in `[t_corr + 0.05 s, t_corr + 0.6 s]`** (A18: a stale grace or latch in the response shows up here as a second correction).
4. `LatchRun` (VQ3 preset on both ends): run forward, press and release jump (take-off), press again mid-air and hold; while still airborne send `serverKnockback [0, 0, 300]` (forces a mid-air correction with latch = false in the response); land. Pass: the landing move jumps on the client (`Velocity.Z ≈ JumpVelocity − g·remainder` right after the landing move, XY speed unchanged within 1 %) and only a stray correction in `[t_land, t_land + 0.3 s]`. Second half: hold jump continuously through a landing (no release): no jump, one friction tick, same window rule. Both halves prove the latch chains correctly through a replay (A17) and that the response's latch bit matches (A12).
5. Existing task-02-review checks (stairs, wall slide, ramp exit) stay in `FT_MovementDeterminism`, not in the soak.

Thresholds are initial (ADR §5); tune after the first real run and record the measured numbers in `STATUS.md`.
