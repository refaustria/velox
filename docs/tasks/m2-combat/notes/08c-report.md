# M2-08c report (2026-10-29)
build.sh 53 tests, 0 failures (`Saved/CI/20261029-203039-3711987`, worktree). Soak `--repeat 3`: 3/3; FireRun rails/hits/stamp 11/11/11 each round, rewind_med 127–143 ms, dmean 7.6–14.8 ms, 0 rejected, 0 act_failed, confirms 51–52/52; 0 `VeloxShotTimeClamp`.
- B32: `CapacityFor(Interval, Tol) = 1 + Tol/Interval`; spacing rule, `FireRateBurstAllowance`, `FireRateSpacingTolerance` deleted. Capacities at Tol 0.5: MG 6, PG 6, LG 11, SG 1.5, RL 1.625, RG 1.333. Honest RG at 250 ms RTT/2 % resends 600 s: 0 rejected. Rail cheat every 1.0 s: 41 of 61 accepted over 60 s (bound 1 + floor(60/1.5)).
- B33: authority with pending index: `GetSwitchRemaining ≤ WeaponRaiseTime + SwitchTimingToleranceSeconds` → `CompletePendingSwitchIndexOnly()`, else reject (cancel EndAbility, Verbose log). Log `VeloxWeaponSwitch supersede completes … remaining= limit=`.
- B34: `MaxRewindSeconds` 0.30 (code default and bootstrap; asset byte-identical).
- Report-only `--lag 125`: run 1 hits 10/11, stamp 7/11, 1 honest rail rejected (see 08e item 1); run 2 11/11/11 (only the [60,250] rewind window fails, it is written for 50 ms). `--lag 250`: 0/11 stamp, all clamped; RocketJumpRun PASS.
- Follow-ups: `08e-switch-timing-remote-shooter.md`.
