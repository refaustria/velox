# 08j — Hold tests assert the hold's contribution to `late_ms`; Haste row for the remote refire wait (B39, B41)

**Milestone:** M2 Combat (follow-up)
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0003 B39, B41; review `docs/reviews/m2-late-rpc-fixes-review.md` (ruling 3, 5a; defects 2, 3); 08f/08g/08h merged. Independent of 08i.

## Goal
`FireRunHold` and `RocketJumpRunHold` stop failing on honest values. `late_ms` at release is the hold plus whatever lateness the RPC already had (0 unlost, RTT + 80 after a lost packet — the 466.7 ms case) plus timer/dispatch quantisation and move-stream jitter (the 275.0 ms case). The hold line logs the lateness at hold time and the scripts assert the delta. Plus the missing unit row: the server's switch refire wait for a remote shooter under Haste.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxGameplayAbility_Fire.cpp` — `DebugHoldServerTargetData` (l.232–260): the `VeloxShotRpcHeld` line gains `late_ms=%.1f` = `(Movement->GetServerClientMoveTimeStamp() − Payload.FireMoveTimeStamp) × 1000` wrap-aware (reuse `UVeloxGameplayAbility_FireProjectile::ComputeShotLateMs`, move it to the base class or `VeloxShotValidation` if cleaner), `na` without a movement component.
- `infra/ci/nettest.sh` — FireRunHold (l.627–641): per held seq, `late_pre` from the `VeloxShotRpcHeld` line, `late_final` from its `VeloxShot` line; assert `hold − 50 ≤ late_final − late_pre ≤ hold + 50` (hold = 300 from the `sexec` argument, parse it rather than hard-code), assert `late_pre ≤ rtt_ms + 150`, report `late_pre`, `late_final`, `rewind_ms`, `hint`, `hit`; RocketJumpRunHold (l.765–766): same delta rule on the `VeloxProjectileShot` line against the held line. Keep: 0 snaps, 0 rejections, `VeloxShotRpcHeld` count.
- `infra/ci/README.md` (l.37–46) — the rule in one sentence.
- `Source/Velox/Tests/VeloxWeaponTests.cpp` — `Velox.Combat.RemoteSwitchTimeline` (l.1233+): Haste row.
- `docs/tasks/m2-combat/notes/08j-report.md`.
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` B39 (hold window), B41 (Haste row), B37, ADR 0004 §3 (Haste interval on the server = tag).
- `docs/reviews/m2-late-rpc-fixes-review.md` §1 ruling 3 (the five terms of `late_ms`), ruling 5a.
- `docs/tasks/m2-combat/notes/08g-report.md` (the three FireRunHold rounds), `08f-report.md` (RocketJumpRunHold 300.0–333.4).

## Acceptance criteria
- [ ] `VeloxShotRpcHeld seq= weapon= slot= ms= t= late_ms=` on both abilities (hitscan and projectile payloads).
- [ ] `FireRunHold --repeat 5`: 5/5; every held rail `late_final − late_pre ∈ [250, 350]`; `late_pre` reported; 0 `VeloxShotOriginSnap`, 0 `VeloxShotRejected`; hit/miss and `rewind_ms` reported only.
- [ ] `RocketJumpRunHold --repeat 5`: 5/5 with the same delta rule on the rocket; the other RocketJumpRun assertions unchanged.
- [ ] `FireRun --repeat 3` and `RocketJumpRun --repeat 3` unchanged (3/3 each).
- [ ] Haste row: with `ASC->AddLooseGameplayTag(Powerup_Haste)` on the server, one MG shot accepted at T → `RefireWait` at T + 1/60 ≈ `0.1 / 1.3 − 1/60` (0.0603, ±1e-3); tag removed, a new MG shot at T' → wait at T' + 1/60 = 0.0833 (±1e-3). Row name prefix `B41:`.
- [ ] `infra/ci/build.sh` green; no commit.

## Verification steps
1. `infra/ci/build.sh` → tests green (+1 row).
2. `infra/ci/nettest.sh --scripts FireRunHold --repeat 5`; `--scripts RocketJumpRunHold --repeat 5`; quote the held rows (`late_pre`/`late_final`/delta).
3. `infra/ci/nettest.sh --scripts FireRun,RocketJumpRun --repeat 3`.

## Report back
Standard format. Quote the held rows of every run (expect `late_pre` ≈ 0 on most, ≈ RTT + 80 on a resent one if the netsim produces one), the Haste row output.

## Notes / outcome
(Filled by the agent or the lead after completion.)
