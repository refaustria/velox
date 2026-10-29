# 08e — Remote-shooter switch timing and 08c leftovers

**Milestone:** M2 Combat (follow-up)
**Agent:** dev-hard (architect ruling first on items 1–3)
**Status:** todo — needs architect ruling
**Depends on:** 08c, 08d merged

## Findings from 08c (2026-10-29, report-only runs `--lag 125`)
1. **Honest rail rejected at 125 ms one-way (cause, not load).** Client `wait=0.090`, server `wait=0.373`: the server's `RefireWait` reads the server-side MG `NextFireTime`, which the Q3 carry pushes ahead when resent MG shots arrive bunched. The server's switch ends ~0.28 s after the client's; the rail fired at the client's switch end hit the B18 gate at ~0.30 s remaining (> 0.25 tolerance) and was rejected. Proposed: for remote shooters the server's switch duration excludes the server-side refire wait. B18's 0.25 s is also below the ~330 ms resend compression at 250 ms RTT (same class as B32's defect 1).
2. **B33 reject branch:** the engine retrigger ends the superseded instance before `ActivateAbility`; after a rejection its drop end never runs, so current/pending stay until the next switch or an accepted shot of the pending weapon. Re-arm the completion?
3. **`MaxRewindSeconds` 0.30 vs jitter:** at `--lag 125` (±20 ms netsim jitter, RTT 210–290 ms) rewind is RTT + 20–40 ms; run 1 had 3 fallbacks (all still hit), run 2 none. ~0.35 would hold including jitter. Human confirmation of the value is pending (STATUS question 19).
4. **Stale "bucket + spacing" comments:** `Weapons/VeloxGameplayAbility_Fire.h` l.7, 65, 82; `VeloxGameplayAbility_FireHitscan.h` l.9; `VeloxGameplayAbility_FireHitscan.cpp` l.247; `VeloxGameplayAbility_FireProjectile.cpp` l.238; `Net/VeloxLagCompensationSubsystem.h` member default 0.25 (overwritten from settings).

## Acceptance (after the ruling)
- [ ] FireRun `--lag 125` report-only: 0 `VeloxShotRejected`, 0 `ClientActivateAbilityFailed` over 5 runs.
- [ ] Default soak list 5/5 at the default lags; build green.
