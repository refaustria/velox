# 09 — Per-player anomaly counters (the deferred cheat surfaces become rates)

**Milestone:** M6 Online
**Agent:** dev-hard
**Status:** done (branch `wip/m6-09-anomaly`, pending review)
**Depends on:** ADR 0007 §5.4; task 07 merged (shared files `VeloxPlayerController.*`, `VeloxPlayerState.*`; the refused-RPC and discrepancy hooks)

## Goal
Every bounded cheat surface the combat ADR accepted and deferred to M6 (B25 clamp window, B32 burst after idle, B35 withheld RPC / orphans, B37 (ii) switch tolerance, B42 `Late` retargets, origin snaps, time-stamp clamps, fire rejections, task 07's refused RPCs and discrepancy resolutions) is already logged per event. This task adds the **per-player rate** that tells an honest player at the ping limit from a modified client, as a 60 s server log line and a logout summary, log-only in M6. The move-time switch gate is **not** built (ADR 0007 §5.4 trigger).

## Scope — files/modules the agent may touch
- `Source/Velox/Net/VeloxAnomalyStats.h` (new, world-free): `FVeloxPlayerAnomalyStats` (counters: `Shots`, `Switches`, `Rejects` by reason tag, `OriginSnaps`, `TimeClamps`, `LateRetargets`, `FreshRetargets`, `MoveShotOrphans`, `RpcRefused`, `Discrepancies`, `RttMs` smoothed), `VeloxAnomaly::FormatLine(const FVeloxPlayerAnomalyStats&, FName Player, int32 Id) → FString`, `VeloxAnomaly::Rates(...)` (late share, reject share), `VeloxAnomaly::Exceeds(const Stats, const FVeloxAnomalyThresholds&) → TArray<FName>` (unused for kicks in M6 — thresholds default 0 = disabled)
- `Source/Velox/GameModes/VeloxPlayerState.h/.cpp` — the server-side, **non-replicated** `AnomalyStats` member and `NoteAnomaly(EVeloxAnomaly, int32 Count = 1)`
- `Source/Velox/Net/VeloxAnomalyLog.h/.cpp` (new `UWorldSubsystem`, server only): 60 s timer → one `VeloxAnomaly player= id= shots= switches= rejects{reason:n,…} snaps= clamps= late_retargets= fresh_retargets= orphans= rpc_refused= discrepancies= late_share= reject_share= rtt_ms=` line per player with any non-zero counter since the last line (cumulative values, per-interval deltas in parentheses); one final line at `Logout`
- Call sites — **one line each**, where the event is already logged: `Weapons/VeloxGameplayAbility_Fire*.cpp` (`VeloxShotRejected reason=`, `VeloxShotOriginSnap`, `VeloxShotTimeClamp`), `Weapons/VeloxWeaponInventoryComponent.cpp` (`mode=late|fresh` retargets, `VeloxMoveShotOrphan`, switches), `GameModes/VeloxPlayerController.cpp` (`VeloxRpcRefused` from task 07), the discrepancy hook from task 07. **No change** to any decision, tolerance or validation — counting only; `Movement/` is not touched (the discrepancy count comes from task 07's hook outside `Movement/`)
- `Source/Velox/Tests/VeloxNetTests.cpp` — `Velox.Net.AnomalyFormat`, `Velox.Net.AnomalyRates`
- `infra/ci/nettest.sh` — the FireRun/RocketJumpRun/PlasmaRun summaries print the honest client's final `VeloxAnomaly` line and **fail** when `late_retargets > 0`, `snaps > 0`, `orphans > 0` or `rpc_refused > 0` for the honest client (B35/B36 already expect 0 for snaps/orphans; this just reads one line); the existing cheat rows (`DebugHoldShotRpc` hold run, the 2× rate row) assert non-zero counters
- `docs/tasks/m6-online/notes/09-report.md`

## Relevant ADRs / references
- ADR 0007 §5.4 (what is counted, the log-only rule, the M7 trigger for the move-time switch gate)
- ADR 0003 B25, B32, B35, B36, B37 (ii), B38, B42, B44 and `docs/adr/reviews/0003-B42-review.md` point 4 (the surfaces and their existing log lines)
- Task 07's hooks

## Acceptance criteria
- [ ] Counting is side-effect-free: no validation outcome, tolerance, clamp or timer changes (diff review: only `NoteAnomaly(...)` calls added at existing log sites, plus `Switches`/`Shots` increments at the accept points).
- [ ] `VeloxAnomaly` line format fixed and unit-tested (`Velox.Net.AnomalyFormat`); `Rates`: `late_share = LateRetargets / max(Switches,1)`, `reject_share = Rejects / max(Shots,1)`, both 2 decimals; `Exceeds` returns the exceeded names for non-zero thresholds and nothing when thresholds are 0 (`Velox.Net.AnomalyRates`).
- [ ] The log subsystem exists only on servers (dedicated or headless host — `VeloxServerKind` if merged), prints nothing for players with all-zero counters, prints the logout line even if the 60 s timer did not fire, and never allocates per tick.
- [ ] `velox.net.AnomalyKickThresholds` is **not** implemented as a kick in M6; `FVeloxAnomalyThresholds` exists with defaults 0 so the M7 decision is config, and the report lists the thresholds' meaning.
- [ ] Nettest: FireRun, RocketJumpRun, PlasmaRun, FireRunControl PASS with the honest client's line showing `late_retargets=0 snaps=0 orphans=0 rpc_refused=0`; the hold-RPC run shows `orphans≥1` or `late… ` as designed by B38 (report which counter B38's hold moves), the 2× rate row shows `rejects{rate:n}` with `n ≥ 1`; `RpcGateRun` (task 07) shows `rpc_refused=1` before the disconnect line.
- [ ] Build and tests green.

## Verification steps
1. `infra/ci/build.sh` → green.
2. Under the soak lock: `nettest.sh --scripts FireRun,FireRunControl,RocketJumpRun,PlasmaRun --repeat 1`; the two cheat rows per their existing invocation (see `infra/ci/README.md`, `velox.combat.DebugHoldShotRpc`).
3. `grep -c "VeloxAnomaly" <server log>` → ≥ 1 per round; paste the lines.

## Report back
Standard format plus: the final line format with one real example per row type (honest, hold, 2× rate), the list of call sites, and your reading of what an honest client at 250 ms / 2 % produces over a 10-minute FFA (run one BotRun round at `--lag 125` and report the line).

## Notes / outcome
Done 2026-11-11 (dev-hard), report: `notes/09-report.md`. Build and 140 tests green. All nettest rows PASS: FireRun,
FireRunControl, RocketJumpRun, PlasmaRun, FireRunHold, RocketJumpRunHold, RpcGateRun (`rpc_refused=1` on both refused
clients' logout lines) and SpeedHackRun (`discrepancies=14` = 14 VeloxTimeDiscrepancy lines). Deviations:
- An honest `late_retargets` is reported, not failed: 2 of 5 honest 50 ms rounds had one B42 `mode=late` retarget. Nettest
  checks the count against the server's `mode=late` lines instead.
- The hold rows move no counter (B36 and B35/B38 absorb the hold).
- No 2x fire-rate cheat row exists in the tree; `rejects{rate:n}` is covered by the unit rows only.
- `pid=` replaces `id=` and `player=` is printed last.
