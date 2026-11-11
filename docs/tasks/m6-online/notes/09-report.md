# M6-09 report: per-player anomaly counters (ADR 0007 section 5.4)

Branch `wip/m6-09-anomaly` (from `origin/m6-online` at 9768385). Agent: dev-hard. 2026-11-11.

## Design
- `Net/VeloxAnomalyStats.h/.cpp` (world-free): `EVeloxAnomaly`, `FVeloxPlayerAnomalyStats` (Shots, Switches, Rejects by
  reason in an 8-entry inline array, OriginSnaps, TimeClamps, LateRetargets, FreshRetargets, MoveShotOrphans, RpcRefused,
  Discrepancies, RttMs), `VeloxAnomaly::Rates`, `Exceeds`, `FormatLine`. `FVeloxAnomalyThresholds` is a USTRUCT, all
  thresholds 0 = disabled.
- `AVeloxPlayerState`: server-only, non-replicated `AnomalyStats` (plus the counters as of the last printed line), and
  `NoteAnomaly` / static `NoteAnomalyFor(actor, ...)`, which resolves PlayerState, pawn or controller and does nothing
  without authority. `Reset()` does not clear the counters: a match restart keeps the connection.
- `Net/VeloxAnomalyLog` (`UWorldSubsystem`, Config=Game, no tick): a looping timer every `velox.net.AnomalyLogIntervalSeconds`
  (default 60; on dedicated and listen worlds). It prints a line for each player whose counters changed since that
  player's last line. `FGameModeEvents::GameModeLogoutEvent` prints `at=logout`. `OnWorldBeginTearDown` prints `at=shutdown` for
  players still connected. A player whose counters are all zero gets no line. Every print checks `VeloxServerKind::IsServerLike`.
- Shots and switches count only remote-shooter accept points. Bots never produce a line: BotRun at 125 ms printed none.

## Line format
`VeloxAnomaly at=<interval|logout|shutdown> pid=<PlayerId> shots=N(+d) switches=N(+d) rejects{reason:N(+d),...} snaps=N(+d)
clamps=N(+d) late_retargets=N(+d) fresh_retargets=N(+d) orphans=N(+d) rpc_refused=N(+d) discrepancies=N(+d) late_share=0.00
reject_share=0.00 rtt_ms=N [exceeds=a,b] player=<name to end of line>`

Values are cumulative and the `(+d)` is the delta since the player's previous line. `late_share` = late / max(switches,1) and
`reject_share` = rejects / max(shots,1), both to 2 decimals. `switches` counts every accepted remote switch press: new
switches plus the `pending` and `late` retargets. A `fresh` retarget is counted when it runs as a new switch.

## Thresholds (`[/Script/Velox.VeloxAnomalyLog] Thresholds=(...)`, all default 0 = off, log only, never a kick)
`MaxLateShare` (+`MinSwitchesForLateShare`=20; the ADR's M7 trigger is 0.20 over 20 or more switches), `MaxRejectShare`
(+`MinShotsForRejectShare`=20), `MaxOriginSnaps`, `MaxTimeClamps`, `MaxMoveShotOrphans`, `MaxRpcRefused`, `MaxDiscrepancies`.
Every comparison is cumulative and strict (`>`). An exceeded name appends `exceeds=` to the line and does nothing else.

## Call sites (one counting line each, at the existing log line; no decision changed)
- `FireHitscan.cpp`: `Reject()` (VeloxShotRejected reason=), `no_lag_compensation` reject, the accept point next to
  `NoteAcceptedRemoteShot`, and `Result.bOriginSnapped` right after `TraceShot`. The snap is counted in the ability, so the
  lag-compensation subsystem is untouched.
- `FireProjectile.cpp`: `Reject()`, the accept point, the `source=rpc` VeloxShotOriginSnap, VeloxShotTimeClamp.
- `WeaponInventoryComponent.cpp`: `NoteMoveShotOrphan`, VeloxShotTimeClamp `source=move`, VeloxShotOriginSnap `source=move`.
- `WeaponSwitch.cpp`: retarget `mode=pending` (switch), `mode=late` (switch + late), `mode=fresh` (fresh), and the
  server-for-remote "VeloxWeaponSwitch X -> Y" accept line (switch).
- `PlayerController.cpp`: `NotifyRpcRefused` (VeloxRpcRefused) and `PollTimeDiscrepancy` (VeloxTimeDiscrepancy, outside `Movement/`).

## Verification
- `build.sh --targets VeloxEditor,Velox --no-tests`: Succeeded. Server and Client are skipped on the installed engine. The
  only warning is the existing `Virtual_Accept` one in VeloxServerBrowserWidget.cpp.
- `build.sh --tests-only`: rc=0, 140/140 Success, including the new rows `Velox.Net.AnomalyFormat` and `Velox.Net.AnomalyRates`.
- Nettest, dedicated, port 7880, one round: FireRun, FireRunControl, RocketJumpRun, PlasmaRun, FireRunHold,
  RocketJumpRunHold, RpcGateRun and SpeedHackRun all PASS. Repeated on port 7882 with a 10 s interval
  (`-ini:Engine:[ConsoleVariables]:velox.net.AnomalyLogIntervalSeconds=10`): FireRun and FireRunHold PASS, and the interval
  lines are live. BotRun at `--lag 125`: PASS, and no VeloxAnomaly line.

Sample lines:
- Honest FireRun (50 ms one way / 2 %): `at=logout pid=256 shots=53(+53) switches=11(+11) rejects{} snaps=0(+0) clamps=0(+0) late_retargets=0(+0) fresh_retargets=0(+0) orphans=0(+0) rpc_refused=0(+0) discrepancies=0(+0) late_share=0.00 reject_share=0.00 rtt_ms=138`
- Honest FireRun, second round: `... late_retargets=1(+0) ... late_share=0.09 ... rtt_ms=137` (see Deviations)
- Hold, FireRunHold (`DebugHoldShotRpc 300 7 3`): `shots=53 switches=11 rejects{} snaps=0 clamps=0 late_retargets=0..1 orphans=0` (no counter moves; see below)
- Hold, RocketJumpRunHold (`300 5 1`): `shots=1 switches=1 ... all 0 ... rtt_ms=243`
- RpcGateRun (two refused clients, logged at logout after the kick): `at=logout pid=256 ... rpc_refused=1(+1) ... rtt_ms=37` and `pid=257 ... rpc_refused=1(+1)`
- SpeedHackRun (DebugTimeScale 1.3): `at=logout pid=256 shots=0 ... discrepancies=14(+14) ... rtt_ms=224`, which equals the 14 VeloxTimeDiscrepancy lines.
- Honest client at 250 ms RTT (FireRun `--lag 125`): `at=logout pid=256 shots=53(+1) switches=11(+0) rejects{} snaps=0 clamps=0 late_retargets=0 orphans=0 rpc_refused=0 discrepancies=0 late_share=0.00 reject_share=0.00 rtt_ms=286`.
  That round FAILs an existing FireRun band (rail rewind_ms <= 250 + late, a 50 ms band), not because of anomaly counters.

## Deviations
1. **No fail on `late_retargets > 0` for honest rounds.** Two of five FireRun-family rounds at 50 ms / 2 % (FireRun x2, FireRunControl, FireRunHold x2;
   the late retarget was in the switch phase, about 25 s after the holds) had exactly one server `mode=late` retarget. B42 accepts `mode=late` within the resend tolerance, and the existing FireRun rule passes it,
   so a hard 0 would make FireRun flaky. Instead nettest checks that the anomaly `late_retargets` count equals the server's
   `mode=late` lines, and it prints every line. Snaps, orphans and rpc_refused are hard 0 for FireRun, FireRunControl,
   RocketJumpRun and PlasmaRun, and shots must be greater than 0.
2. **The hold rows move no counter.** B36 validates the origin at the fire stamp, so a held rail RPC is not snapped. B35/B38
   adopt the held rocket RPC through its move-shot record (`source=move`, late_ms ≈ 283), so it is not orphaned. The hold
   therefore shows only in the existing `late_ms` and VeloxShotLate lines and is not a counted surface. An orphan needs an
   RPC that never arrives, which no nettest row exercises. Nettest reports the hold rows' counters and asserts none.
3. **There is no "2x rate" cheat row.** No fire-rate cheat cvar exists in the tree, and adding a client cheat is outside
   this task's scope. `rejects{rate:n}` is covered by the unit rows. A client-side `velox.combat.DebugFireRateScale` cheat
   would close this gap (follow-up).
4. **Line format.** `pid=` replaces the ADR's `id=` (F3/F6), the line gains an `at=` field, and `player=` moves to the end
   because names may contain spaces. `FormatLine` takes the player name as an `FString` instead of an `FName` and takes the
   previous-line stats for the deltas.
5. **Files.** The retarget log lines live in `VeloxGameplayAbility_WeaponSwitch.cpp`, not in the inventory component, so
   that file was touched. `VeloxAnomalyStats.cpp` was added next to the header.
6. **Shutdown lines on a headless listen host.** When the world tears down, the spectator PlayerController may already be
   gone, so `IsServerLike` can fail and the `at=shutdown` lines can be skipped. Logout lines are unaffected.

## Reading for a 10-minute FFA at 250 ms / 2 %
FireRun at 250 ms RTT counted 0 on every surface over 53 shots and 11 switches. From the five 50 ms rounds, the rate is 2 late retargets in 55
switches, so late_share is about 0.04. A 10-minute FFA with roughly 60 switches and 1000 shots should
show late_retargets of 0 to 4 (late_share at most about 0.07, well under the 0.20 M7 trigger) and reject_share near 0.
That reject_share estimate assumes the MG bucket rejects from M2-08 D3 stay rare. Rate rejects are the one surface an honest
MG player can raise, which is why the rejects are reported by reason.

## Addendum — task 09b (review follow-ups, 2026-11-11)

- Open point 6 is fixed (review F1): the log prints on any `NM_DedicatedServer` / `NM_ListenServer` world. Verified with
  FireRun stopped by SIGTERM while the client was connected: dedicated `VeloxAnomaly at=shutdown pid=256 shots=51(+51)
  switches=4(+4) ...`, headless listen host `VeloxAnomaly at=shutdown pid=257 shots=51(+51) switches=4(+4) ...`.
- `reject_share` is now rejects / attempts (accepted + rejected), its minimum count on attempts (review F2). A hitscan
  `no_lag_compensation` reject no longer also counts as an accepted shot (review F3 (a)); `nan_origin` stays uncounted.
- The 2x rate row now exists: `FireRunRate` (client `velox.combat.DebugFireRateScale 2`, MG hold 5 s). Real example:
  `VeloxAnomaly at=logout pid=256 shots=54(+54) switches=0(+0) rejects{rate:46(+46)} snaps=0(+0) clamps=0(+0)
  late_retargets=0(+0) fresh_retargets=0(+0) orphans=0(+0) rpc_refused=0(+0) discrepancies=0(+0) late_share=0.00
  reject_share=0.46 rtt_ms=131 player=Player` (client 100 shots at 20/s, server accepted 10.74/s vs honest 10/s).
