# 09b — Task 09 review follow-ups: shutdown-line gate, attempts denominator, fire-rate cheat row

**Milestone:** M6 Online
**Agent:** dev-hard
**Status:** done (branch `wip/m6-09-anomaly`, 2026-11-11)
**Depends on:** 09 (`wip/m6-09-anomaly`), review `docs/adr/reviews/0007-09-review.md` (approve with follow-ups; Opus standing in for architect)

## Work
1. **F1:** `VeloxAnomalyLog.cpp:60-62` — gate the print on net mode (`NM_DedicatedServer || NM_ListenServer`), as the timer at `:54` already does, so `at=shutdown` lines survive teardown on the headless listen host.
2. **F2:** `VeloxAnomalyStats.cpp:87/99` — `reject_share` and the minimum-count check use attempts (accepted + rejected); unit rows for an all-rejected client.
3. **F3(a):** `no_lag_compensation` reject (`VeloxGameplayAbility_FireHitscan.cpp:309-314`) is counted once (reject only); count `nan_origin` if cheap.
4. **T1:** `velox.combat.DebugFireRateScale` (client-side, non-Shipping cheat cvar, default 1.0) and a nettest row `FireRunRate` (2×): server rejects with `rejects{rate:n}` > 0 counted in the `VeloxAnomaly` line; honest FireRun unchanged.
5. Append the review's ADR 0007 amendment text as **F4** (rename internal references so it does not clash with the review's finding numbers).

## Acceptance criteria
- [x] Editor build + all tests green; new unit rows.
- [x] FireRun ×1 dedicated + ×1 listen with the server stopped while the client is connected: an `at=shutdown` line each.
- [x] FireRunRate PASS (rate rejects counted), FireRun/FireRunControl/RpcGateRun/SpeedHackRun PASS.
- [x] Nothing in Movement/ or the lag-compensation decision logic changed.

## Constraints
Production live on this host (ports 7777/8790, units `velox-server@ffa`, `veloxmaster`) — never touch. Disk ≥ 11 GB or pause.

## Notes / outcome
Done 2026-11-11 (dev-hard).
- **F1:** `UVeloxAnomalyLog::IsActive` tests `NM_DedicatedServer || NM_ListenServer` (the include of `VeloxServerKind.h` is gone).
- **F2:** `reject_share = rejects / max(shots + rejects, 1)` (`FVeloxPlayerAnomalyStats::GetShotAttempts`), `MinShotsForRejectShare`
  gates on attempts; `shots=` still means accepted. New rows in `Velox.Net.AnomalyRates`: all-rejected client (share 1.00,
  19 attempts not exceeded, 20 exceeded with 0 accepted, defaults exceed nothing), min-sample gate on attempts; the
  `AnomalyFormat` full line now shows `reject_share=0.25` (4 / 16).
- **F3 (a):** FireHitscan counts `Shot` only when `ResolveServerShot` did not set `LastServerShot.bRejected`
  (`no_lag_compensation` = one Reject only). `nan_origin` stays uncounted (not cheap: the subsystem reports no rejection
  to the ability and must stay untouched); documented on `EVeloxAnomaly::Shot`.
- **T1:** `velox.combat.DebugFireRateScale` (non-shipping, `NM_Client` worlds only, in `UVeloxGameplayAbility_Fire::AdvanceNextFireTime`)
  and nettest row `FireRunRate` (`Content/Data/MoveScripts/FireRunRate.json`, MG hold 5 s; see `infra/ci/README.md`).
  "Damage per second not above honest" is judged as the server's accepted MG shot rate <= 1.15 x honest (same weapon, same
  damage per hit; the dummy is invulnerable), honest = client rate / scale.
- **F4:** the review's amendment text is appended to ADR 0007 as amendment F4; review findings are cited there as "review F*n*".
- Verification: `build.sh --targets VeloxEditor` green, 140 tests, 0 not successful (rows added inside existing tests).
  Shutdown runs (FireRun, SIGTERM to the server 35 s after join, client connected): dedicated
  `VeloxAnomaly at=shutdown pid=256 shots=51(+51) switches=4(+4) ... player=Player`, listen host
  `VeloxAnomaly at=shutdown pid=257 shots=51(+51) switches=4(+4) ... player=Player (2)`. FireRunRate x2 PASS:
  `rejects{rate:46}` reject_share=0.46 server 10.74/s vs honest 10.00/s; `rejects{rate:44}` 0.44, 10.82/s. FireRun, FireRunControl,
  RpcGateRun (rpc_refused=1 on both), SpeedHackRun (discrepancies=14 = 14 lines) PASS.
