# 08b — FireRun defects D1–D4 (hint stamp, MG bucket, switch after finalise, soak latency)

**Milestone:** M2 Combat
**Agent:** dev-hard (first attempt; architect review afterwards — the architect's root-cause pass on 2026-10-29 was cut off by the Fable usage limit before it wrote anything)
**Status:** done (FireRun 5/5; architect review pending; report notes/08b-report.md)
**Depends on:** 08 (tooling, commit 83ad004)

## Goal
Make FireRun pass 5/5 by fixing the defects task 08 found (`docs/tasks/m2-combat/notes/08-report.md`), without weakening the soak's assertions.

## Lead decisions for this attempt (provisional, architect to confirm)
- **D2:** FireRun is judged at the ADR's latency, **100 ms RTT = `--lag 50` one-way**, 2 % loss. `MaxRewindSeconds` stays 0.25. nettest runs FireRun/FireRunControl with `--lag 50` by default (per-script lag), the movement scripts keep 100 ms one-way. Record the 125 ms and 250 ms one-way results as report-only.
- Assertions in FireRun stay as built in 08 unless a defect analysis proves one wrong; any change to an assertion needs a one-line justification in the report.

## Scope — files/modules the agent may touch
- `Source/Velox/Net/VeloxLagCompensationSubsystem.*` (D1), `Source/Velox/Character/VeloxCharacter.*` / `Source/Velox/Movement/VeloxMovementComponent.*` only for smoothing-stamp access (D1), `Source/Velox/Weapons/VeloxFireRateBucket.h`, `VeloxShotValidation.*`, `VeloxGameplayAbility_Fire*.cpp` (D3), `VeloxGameplayAbility_WeaponSwitch.*`, `VeloxWeaponInventoryComponent.*` (D4), `Source/Velox/Combat/VeloxCombatSettings.*` + bootstrap script if a tunable is added, `infra/ci/nettest.sh` + README (per-script lag), tests.

## Defects
- **D1 — hint stamp ~55 ms too old.** Find why the drawn pose matches server history at `hint_stamp + ~50 ms` (suspect: `SmoothingClientTimeStamp` is not the stamp of the drawn pose under Linear smoothing, engine `SmoothClientPosition_Interpolate` ~l.8394). Derive the correct stamp of the drawn pose, prove it with the `velox.lagcomp.Debug 2` comparison used in 08 (median |dt| ≤ 10 ms over ≥ 20 shots), fix `BuildShotTiming`. Add/extend a unit test for the stamp computation.
- **D3 — MG bucket rejects honest sustained fire.** Bucket capacity 3 drained by resend bursts never refills at max rate; arrivals ~30 ms early rejected. Specify and implement a rule where honest clients at up to 250 ms RTT / 2 % loss see 0 rate rejections while a 2× fire-rate cheat is rejected within ~1 s (e.g. refill against the client's shot schedule bounded by server time, or a long-window average check). Extend `FireRateBucket`/`ShotValidation` tests with a resend-burst row and a 2× cheat row.
- **D4 — switch rejected after finalise.** A switch arriving in the same server frame as the shot that finalised the previous pending switch is rejected because `GE_WeaponSwitching` still runs. Fix: finalising a switch ends/removes the switch GE (or a new switch supersedes it). Extend the `Inventory` test.
- **D2** — see lead decision; implement the per-script lag in nettest.

## Acceptance criteria
- [ ] `infra/ci/build.sh` green (49 + new tests).
- [ ] D1 evidence: median |dt| ≤ 10 ms between client `aim_pos` and server history at the hinted stamp, ≥ 20 shots, at `--lag 50`.
- [ ] `infra/ci/nettest.sh --repeat 5` full default list 5/5 on an otherwise idle machine, FireRun at `--lag 50` with hint `stamp` on ≥ n−1 rails and ≥ 9/10 rail hits, 0 MG rejections, 0 switch failures.
- [ ] Report-only: FireRun at 125 ms and 250 ms one-way.
- [ ] No commit; report with per-defect cause, fix, evidence, files.
