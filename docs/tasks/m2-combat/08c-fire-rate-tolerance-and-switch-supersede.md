# 08c — One fire-rate tolerance, switch supersede completes the pending switch, rewind budget 0.30

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** todo
**Depends on:** 08b (2876aba), ADR 0003 B32, B33, B34; review `docs/reviews/m2-08b-m1-07-review.md` defects 1 and 2

## Goal
An honest player at the 250 ms RTT limit never sees a rejected rail after a lost packet, a weapon switch back to the previous weapon right after a switch is never rejected, and a hinted rail at 250 ms RTT takes the exact stamp path. Three small changes: the fire-rate bucket becomes a single-tolerance GCRA (B32), the switch ability completes a superseded pending switch under the B18 gate before validating the new one (B33), and `MaxRewindSeconds` defaults to 0.30 (B34).

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxFireRateBucket.h` — `CapacityFor(Interval, Tol) = 1 + Tol/Interval`; delete `MinSpacingFor` and the spacing branch of `TryConsume` (signature loses `MinSpacing`); header comment.
- `Source/Velox/Weapons/VeloxShotValidation.*`, `Source/Velox/Weapons/VeloxGameplayAbility_Fire.cpp` (`ConsumeServerFireToken`, `ValidateServerShot`) — call sites; `EVeloxShotReject::Spacing` and `reason=spacing` removed.
- `Source/Velox/Combat/VeloxCombatSettings.*` — delete `FireRateBurstAllowance`, `FireRateSpacingTolerance` (+ getter, cvar, `VELOX_COMBAT_FLOAT_FIELDS` entry); `MaxRewindSeconds` default 0.30; keep `FireRateJitterToleranceSeconds` 0.5.
- `Tools/editor/create_bootstrap_content.py`, `docs/content-bootstrap.md` — drop `fire_rate_burst_allowance`, `fire_rate_spacing_tolerance`; `max_rewind_seconds` 0.30. Re-run the bootstrap so `DA_CombatSettings_Default` matches (report the asset diff).
- `Source/Velox/Weapons/VeloxGameplayAbility_WeaponSwitch.cpp` (`ActivateAbility`, authority branch) — B33 completion/rejection step; `Source/Velox/Weapons/VeloxWeaponInventoryComponent.*` only if a small inventory-only helper (`CompletePendingSwitchIndexOnly()` or a `bool bEndInstance` parameter on `CompletePendingSwitch`) is cleaner than inlining.
- `Source/Velox/Tests/VeloxWeaponTests.cpp` — rows below.
- `infra/ci/nettest.sh` — only the `[60, 250]` window if 0.30 needs it (it should not at 50 ms one-way) and the report-only `--lag 125` run documented in `infra/ci/README.md`.

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` B18, B19 (superseded server paragraph), B32, B33, B34
- `docs/reviews/m2-08b-m1-07-review.md` rulings 3, 4, 6; defects 1, 2
- `docs/tasks/m2-combat/notes/08b-report.md` (D3, D4 evidence)

## Acceptance criteria
- [ ] `FVeloxFireRateBucket::CapacityFor(1.5, 0.5) == 1.3333`, `(1.0, 0.5) == 1.5`, `(0.8, 0.5) == 1.625`, `(0.1, 0.5) == 6`, `(0.05, 0.5) == 11`; no spacing code path remains (`grep -rn "Spacing" Source/Velox` finds nothing but history comments).
- [ ] Test rows: honest RG at 250 ms RTT / ±20 ms jitter / 2 % resends (RTT + 80 ms late, in order) over 600 s → 0 rejections (document that B19's `MinSpacing 1.25` rejects ≥ 1 of the same arrivals); RG cheat firing every 1.0 s from a full bucket → shot 2 accepted, shot 3 rejected, next accepted at 2.5 s, accepted count over 60 s ≤ 41; existing MG/LG honest and 2× rows unchanged.
- [ ] Switch: in `ActivateAbility` (authority), a pending switch with `GetSwitchRemaining(ASC) ≤ WeaponRaiseTime + SwitchTimingToleranceSeconds` is completed (index set, `GE_WeaponSwitching` removed) before `TargetIndex == CurrentIndex` is evaluated; with more remaining the new activation is rejected and the pending switch is untouched. The completion must not call `CompletePendingSwitch()` as written (its instance loop ends the new activation under `InstancedPerActor`).
- [ ] `Inventory` test rows: (a) MG→RG, world time advanced past Wait+Drop (raise still running), server activation RG→MG → accepted, pending MG, current RG at that moment; (b) MG→RG with the drop still running (remaining > `Raise + Tol`), server activation RG→MG → rejected, current MG, pending RG, GE still active; (c) 08b's D4 rows still pass.
- [ ] `MaxRewindSeconds` 0.30 in code default, bootstrap and asset; B25's clamp log (`VeloxShotTimeClamp`) count in RocketJumpRun stays 0.
- [ ] `infra/ci/build.sh` green (53 + new − deleted tests; the D5 staleness test may already be gone if 08d merged first).
- [ ] `infra/ci/nettest.sh --repeat 3` full default list 3/3 on an idle machine (`--port 7830` if 7777 is busy); FireRun 0 `VeloxShotRejected`, 0 `ClientActivateAbilityFailed`, `hint=stamp` ≥ n−1, rails ≥ 9/10.
- [ ] Report-only: `nettest.sh --scripts FireRun,RocketJumpRun --lag 125` once — expect `hint=stamp` on ≥ n−1 rails and ≥ 9/10 hits with 0.30 (quote the row); `--lag 250` once (beyond the bound; quote, no assertion).
- [ ] No commit; report with per-item cause/fix/evidence; note the bucket's new capacities per weapon in the report.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`, test count stated.
2. `infra/ci/nettest.sh --repeat 3 --keep-going` → 3/3; paste the FireRun rows.
3. `infra/ci/nettest.sh --scripts FireRun,RocketJumpRun --lag 125` and `--lag 250` → paste rows.

## Report back
Standard format; include the rail-cheat sequence (accept/reject per shot with token levels) and the switch rows' remaining-time values.

## Notes / outcome
