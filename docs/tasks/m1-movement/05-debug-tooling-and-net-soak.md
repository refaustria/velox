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
- **Wall slide (A13, corrected by A23 on 2026-10-24):** run at 45° into a corridor wall for 1 s (test tick fixed at 1/120 s). Pass iff: `|Velocity.Y| < 1 cm/s` (velocity is clipped, not merely blocked); `GetSpeed2D() > MaxGroundSpeed` (Q3 wall-hug: pressing into a wall at an angle is faster than running); and `0.90·Expected ≤ GetSpeed2D() ≤ 1.01·Expected` with `Expected = min(GroundAcceleration·MaxGroundSpeed·cos 45° / GroundFriction, MaxGroundSpeed / cos 45°)` from `GetSimParams()` (= 942.8 cm/s for the defaults → 848.5–952.2; the engine's sweep contact loss gives ≈ 896 at 120 Hz). Remove the `AddWarning`; `AddInfo` prints measured, expected, deviation %. The old `MaxGroundSpeed · cos 45° ± 5 %` (566) figure was wrong — see ADR 0002 A23 for the derivation.
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
3. *(criteria superseded by the A24–A26 block below; script unchanged)* `KnockbackRun` (`PlayerStart_Corridor`): stand still 1 s on the ground; send `serverKnockback [1250, 0, 600]`; wait 1.5 s. Pass: exactly one correction in `[t, t + 0.5 s]` (the intended one; a second stray ≤ 5 cm allowed), excluded from `X`/`Y`; after that correction the client is airborne within one move, and `GetKnockbackGrace()` on the client right after the replay is `> 0.1 s` (the response container delivered the grace — with no container the value would be 0); `GetSpeed2D()` 0.15 s after the correction `≥ 0.9 × 1250` (friction suppressed client-side; with friction on it would be ≈ 0.4 × 1250); **no further correction in `[t_corr + 0.05 s, t_corr + 0.6 s]`** (A18: a stale grace or latch in the response shows up here as a second correction).
4. *(window rule superseded by the A24–A26 block below; script unchanged)* `LatchRun` (VQ3 preset on both ends): run forward, press and release jump (take-off), press again mid-air and hold; while still airborne send `serverKnockback [0, 0, 300]` (forces a mid-air correction with latch = false in the response); land. Pass: the landing move jumps on the client (`Velocity.Z ≈ JumpVelocity − g·remainder` right after the landing move, XY speed unchanged within 1 %) and only a stray correction in `[t_land, t_land + 0.3 s]`. Second half: hold jump continuously through a landing (no release): no jump, one friction tick, same window rule. Both halves prove the latch chains correctly through a replay (A17) and that the response's latch bit matches (A12).
5. Existing task-02-review checks (stairs, wall slide, ramp exit) stay in `FT_MovementDeterminism`, not in the soak.

Thresholds are initial (ADR §5); tune after the first real run and record the measured numbers in `STATUS.md`.

## Instructions added by the net-soak root cause (architect, 2026-10-24) — ADR 0002 A24, A25, A26

Two findings from the first real soak runs (`Saved/CI/20261024-19*`): the zero-cm corrections after a knockback are the engine's inherent "echo" corrections for in-flight moves (A24, accepted, invisible), and the 32 cm `LatchRun` correction is an engine race in which a good-move ack culls saved moves that a pending correction replay still needs (A25, must be guarded). Read the amendments before touching the code. Work split: **dev-hard** owns the `UVeloxMovementComponent` change (A25 fix, `svel=` field); **dev** owns the scripts, `nettest.sh` rules and the `VeloxReplay` line. Both: `infra/ci/build.sh` must stay `[ci] CI OK`.

**Code (dev-hard, `Source/Velox/Movement/VeloxMovementComponent.h/.cpp`):**
- Override `virtual void ClientAckGoodMove_Implementation(float TimeStamp)` (engine `CharacterMovementComponent.h` l.2571, virtual; `ClientHandleMoveResponse` dispatches to it on the packed path): when `GetPredictionData_Client_Character()->bUpdatePosition` is true, log Verbose `"ClientAckGoodMove %.3f ignored: correction pending replay"` and return without calling `Super`; otherwise `Super`. Nothing else changes (no replay inside the RPC handler, no re-teleport to the acked move).
- `VeloxCorrectionResponse` line gains `svel=<|NewVelocity|>` (the server's velocity in the response, before the replay).

**Instrumentation (dev, `VeloxMovementComponent.cpp`, non-shipping):** `OnClientCorrectionReceived` stores the correction's `TimeStamp` in a `PendingCorrectionTimeStamp` member; `ClientUpdatePositionAfterServerUpdate`, before `Super`, if `ClientData->bUpdatePosition && SavedMoves.Num() > 0`, logs `VeloxReplay t=<world s> start=<SavedMoves[0]->TimeStamp> expected=<PendingCorrectionTimeStamp + SavedMoves[0]->DeltaTime> moves=<N>`. Do not use `LastAckedMove->TimeStamp` for `expected`: after the A25 cull it is already the acked good move and the check would pass.

**Scripts (dev, `Content/Data/MoveScripts/`):** add `KnockbackGroundRun.json` = `KnockbackRun` with `serverKnockback [1250, 0, 0]` (the pawn stays Walking: normal speed 0 < `RampSlideMinNormalSpeed`); `nettest.sh` default script list becomes `StrafeRun,PadRun,KnockbackRun,KnockbackGroundRun,LatchRun`.

**`nettest.sh` rules (dev) — these replace criteria 3 and 4 above and the "stray" definition:**
- *Echo:* a correction with `cm < 1.0` whose `t` lies in `[t_c, t_c + 0.35]` after a non-echo correction at `t_c` (`cm` is the pre-replay error at the acked move, the same number as `p.NetShowCorrections`). Echoes are reported as `echoes=E` in the summary row and excluded from `C`, `X`, `Y`, from `L_exp` accounting and from every window rule. More than 5 echoes attributable to one non-echo correction fails (`"replay does not converge"`).
- *Stray:* one non-echo correction `≤ 5 cm` per window (unchanged), its echoes allowed.
- *Replay start:* every `VeloxReplay` line must satisfy `start − expected ≤ 0.5 / 120`; otherwise fail with the line quoted (this is the A25 regression check). With the fix in place the Verbose ack-ignored line is expected to appear in some runs; its absence is not a failure.
- `KnockbackRun` (lift-off): exactly one non-echo correction in `[t_k, t_k + 0.5]` (the intended one, 20–25 cm; one stray allowed); after it the client is airborne within one move; response grace of the intended correction `≥ KnockbackGraceMax − 2/60` (the two moves of the dual RPC); for every echo `i` of it, `grace_i = grace_{i−1} − (ts_i − ts_{i−1})` within 1e-3 (A18 proof); `GetSpeed2D()` 0.15 s after the correction `≥ 0.9 × 1250`; no non-echo correction in `[t_c + 0.05, t_c + 0.6]`. The old "`GetKnockbackGrace()` after the replay > 0.1" is dropped (impossible by construction: the replay spans one RTT ≥ `KnockbackGraceMax`).
- `KnockbackGroundRun` (friction suppression online): same window rules as `KnockbackRun`, client *not* airborne after the correction, and `speed2d` of the intended correction's `VeloxCorrection` line within 1 % of that response's `svel`; no non-echo correction in `[t_c + 0.05, t_c + 0.6]`.
- `LatchRun`: unchanged checks (mid-air correction present, land1 jumps with `Vz ≈ JumpVelocity − g·remainder` and XY within 1 %, land2 no jump) with windows `[t_land, t_land + 0.3]` evaluated on non-echo corrections only; additionally no non-echo correction anywhere in `[t_k, t_land2]` except the intended mid-air one.
- `StrafeRun`, `PadRun`: unchanged, evaluated on non-echo corrections.

**Acceptance (both agents):** `nettest.sh` passes 20 consecutive times for `LatchRun` and 5 for the full list (`for i in $(seq 20); do infra/ci/nettest.sh --scripts LatchRun || break; done`); record in `STATUS.md`: runs, `C`/`E`/`max`/`mean` per script, count of ack-ignored lines, and the measured RTT (first correction delay after a knockback, ≈ 0.21–0.23 s here). Before the fix the soak measured `LatchRun` 4 failures in 31 runs (32.21, 32.21, 46.11 and 32.85 cm = 4, 4, 6 and 4 moves at 800 cm/s; the last two caught with verbose logging, see A25 and `docs/adr/evidence/`), `KnockbackRun` 4/4 "failures" that were all echo-only.

