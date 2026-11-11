# 09b — Task 09 review follow-ups: shutdown-line gate, attempts denominator, fire-rate cheat row

**Milestone:** M6 Online
**Agent:** dev-hard
**Status:** todo
**Depends on:** 09 (`wip/m6-09-anomaly`), review `docs/adr/reviews/0007-09-review.md` (approve with follow-ups; Opus standing in for architect)

## Work
1. **F1:** `VeloxAnomalyLog.cpp:60-62` — gate the print on net mode (`NM_DedicatedServer || NM_ListenServer`), as the timer at `:54` already does, so `at=shutdown` lines survive teardown on the headless listen host.
2. **F2:** `VeloxAnomalyStats.cpp:87/99` — `reject_share` and the minimum-count check use attempts (accepted + rejected); unit rows for an all-rejected client.
3. **F3(a):** `no_lag_compensation` reject (`VeloxGameplayAbility_FireHitscan.cpp:309-314`) is counted once (reject only); count `nan_origin` if cheap.
4. **T1:** `velox.combat.DebugFireRateScale` (client-side, non-Shipping cheat cvar, default 1.0) and a nettest row `FireRunRate` (2×): server rejects with `rejects{rate:n}` > 0 counted in the `VeloxAnomaly` line; honest FireRun unchanged.
5. Append the review's ADR 0007 amendment text as **F4** (rename internal references so it does not clash with the review's finding numbers).

## Acceptance criteria
- [ ] Editor build + all tests green; new unit rows.
- [ ] FireRun ×1 dedicated + ×1 listen with the server stopped while the client is connected: an `at=shutdown` line each.
- [ ] FireRunRate PASS (rate rejects counted), FireRun/FireRunControl/RpcGateRun/SpeedHackRun PASS.
- [ ] Nothing in Movement/ or the lag-compensation decision logic changed.

## Constraints
Production live on this host (ports 7777/8790, units `velox-server@ffa`, `veloxmaster`) — never touch. Disk ≥ 11 GB or pause.

## Notes / outcome
