# 08d — Exact server-time estimate (GameState `PreReplication`) and the fallback interpolation delay

**Milestone:** M2 Combat
**Agent:** dev
**Status:** todo
**Depends on:** 08b (2876aba), ADR 0003 B30, B31; review `docs/reviews/m2-08b-m1-07-review.md` defects 3 and 4

## Goal
The client's server-time estimate no longer carries the ≈ 50 ms replication-age bias, without reflection into a protected engine field: the Velox GameState stamps its replicated time in `PreReplication`, and every Velox reader (shot timing, projectile proxy dead reckoning, projectile ability) goes through `VeloxLagComp::EstimateServerTime`. The fallback interpolation delay (no moving proxy) stops assuming a 100 ms smoothing lag that does not exist.

## Scope — files/modules the agent may touch
- `Source/Velox/GameModes/VeloxGameState.h/.cpp` — `virtual void PreReplication(IRepChangedPropertyTracker&) override`: `Super`, then `ReplicatedWorldTimeSecondsDouble = GetWorld()->GetTimeSeconds()` (protected member of `AGameStateBase`; keep the engine timer untouched).
- `Source/Velox/Net/VeloxLagCompensationSubsystem.h/.cpp` — delete `ExpectedServerTimeStaleness` and the reflection in `EstimateServerTime` (it returns `GameState.GetServerWorldTimeSeconds()`); fallback delay per B30: the last measured mean if < 2 s old, else `VeloxLagCompCVars::FallbackInterpolationDelay` (0.03, non-shipping cvar `velox.lagcomp.FallbackInterpolationDelay`). Where to keep the last measurement: a small struct on `AVeloxPlayerController` (`LastInterpolationDelay`, `LastInterpolationDelayTime`) passed by reference into `BuildShotTiming`, or a static per-PC map — pick the simpler; it must not live in the subsystem (clients may have none, B10).
- `Source/Velox/Weapons/VeloxProjectile.cpp` (`TickProxy`, l.598), `Source/Velox/Weapons/VeloxGameplayAbility_FireProjectile.cpp` (l.141), `Source/Velox/Weapons/VeloxGameplayAbility_FireHitscan.cpp` (`server_est` log value) — read through `EstimateServerTime`.
- `Source/Velox/Tests/VeloxLagCompTest.cpp` — delete the staleness test; add a fallback-delay row (no proxies: fresh measurement reused < 2 s, constant after).
- `infra/ci/nettest.sh` — no assertion change expected; `view_delay_ms` may be printed in the FireRun summary if not already.

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` B13 (corrected by B30/B31), B24, B30, B31
- `docs/reviews/m2-08b-m1-07-review.md` rulings 1, 2
- UE 5.8 `GameStateBase.cpp` l.155–193 (client-side mean), `NetDriver.cpp` `ServerReplicateActors_BuildConsiderList` (`CallPreReplication` only for actors due this frame)

## Acceptance criteria
- [ ] `grep -rn "GetServerWorldTimeSeconds" Source/Velox` matches only `VeloxLagComp::EstimateServerTime`; `FindFProperty` is gone from `VeloxLagCompensationSubsystem.cpp`.
- [ ] `AVeloxGameState::PreReplication` exists and is covered by a comment citing B31; `ServerWorldTimeSecondsUpdateFrequency` is not touched.
- [ ] Measured on the 08b evidence path (`velox.lagcomp.Debug 2`, ≥ 20 rail shots at `--lag 50`): `server_est − (true server time at receipt − one-way lag)` is within ±10 ms on average (the dev may derive it from `view_ms` vs `rewind_ms`: FireRun `|rewind − view|` mean ≤ 30 ms, per-shot ≤ 60 ms, must still hold — quote the values before and after).
- [ ] FireRunControl behaviour unchanged (0–2 hits).
- [ ] RocketJumpRun: the proxy rocket's dead-reckoned position is not asserted; report `VeloxSelfSplash` equality still holds.
- [ ] `infra/ci/build.sh` green; `infra/ci/nettest.sh --repeat 3` full list 3/3 on an idle machine.
- [ ] No commit; report with the measured bias before/after and the fallback path exercised at least once (`bDelayFromProxies=0` lines in the warm-up shots are fine as evidence).

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`.
2. `infra/ci/nettest.sh --scripts FireRun,FireRunControl,RocketJumpRun --repeat 3 --keep-going` → 3/3, paste rows with `view_delay_ms`, `server_est` samples.
3. `infra/ci/nettest.sh --repeat 3` → 3/3.

## Report back
Standard format; include the before/after `|rewind − view|` means and where the last-measurement state lives.

## Notes / outcome
