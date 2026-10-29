# Task 08b agent report (2026-10-29)

build.sh 50 tests (49 + `Velox.Combat.RenderedStamp`), 0 failures (`Saved/CI/20261029-140847-3333900`). Soak 5 rounds (`Saved/CI/20261029-141000-3335522`): FireRun 5/5 at 50 ms one-way (rails/hits/stamp 11/11/11 every round, rewind_med 123–141 ms, |rewind−view| mean 7.8–12.1 ms, 0 MG rejections, 0 activation failures), FireRunControl hits 0–2; overall 4/5 because of one LatchRun 10.8 cm mid-air stray in round 4 at ts 1.4918 after a repaired move at 1.3918 (5 standalone reruns PASS, `Saved/CI/20261029-144239-3378021`).

- **D1** cause: `SmoothingClientTimeStamp` only drives the mesh-offset lerp; `SimulateMovement`/`MoveSmooth` advance the proxy capsule from the newest server pose, drawn = capsule + offset. Fix: 32-entry ring of received updates (stamp, location) via `SmoothCorrection` override on simulated proxies; `GetRenderedServerStamp` = stamp of the nearest point on the received path + extrapolation segment, bounded below by SCT (ping-pong strafe). Interp-delay mean uses it. dt before: median 50/56 ms (range 14–78); after (22 shots, `Saved/CI/20261029-135744-3322257`): median +0.10, |dt| median 0.20, max 1.9 ms. Test `Velox.Combat.RenderedStamp`.
- **D5** (new): `GetServerWorldTimeSeconds()` ~45–51 ms behind (server refreshes replicated time on a 0.1 s timer, GameState at 10 Hz); B13 "≤ one frame" wrong. Provisional fix: `VeloxLagComp::EstimateServerTime` adds `0.5 × min(update period, 1/NetUpdateFrequency)` (period read via reflection). Exact fix would be in `AVeloxGameState` (stamp in `PreReplication` or raise rate). Projectile code (`VeloxProjectile.cpp:592`, `VeloxGameplayAbility_FireProjectile.cpp:140`) carries the same bias, untouched.
- **D3** cause: capacity C tolerates (C−1)×interval of arrival spread; resends arrive ~RTT+80 ms late. Fix: `CapacityFor = max(FireRateBurstAllowance, 1 + FireRateJitterToleranceSeconds / interval)`, new tunable 0.5 s (MG 6, LG 11, others 3). Tests: honest MG 2 % resends 600 s at 100/250 ms RTT never rejected; 2× cheat rejected within 1 s.
- **D4** cause: server's instanced switch ability still active (drop-end timer behind the client's) → GAS "already active". Fix: `CompletePendingSwitch()` (index, `CompleteSwitchEarly`, remove `GE_WeaponSwitching`, clear `SwitchEndTime`); switch ability `bRetriggerInstancedAbility = true`; local `PrepareInputActivation` skips during an active switch. Inventory test rows.
- **D2**: per-script lag in nettest (FireRun/FireRunControl 50 ms one-way, others 100 ms; `--lag N` overrides all).
- Report-only: 125 ms one-way FireRun 9/11, all fallback (rewind > MaxRewindSeconds); 250 ms: 0/11, 2 MG rejections, 1 activation failure (beyond the 250 ms RTT design bound). RocketJumpRun PASS at both.

## For the architect
1. B13/B14 premises wrong (proxies drawn extrapolated); B13 fallback delay (108 ms) rests on the same model; measured rendered lag ≈ update age (22–36 ms). Fallback unchanged.
2. D5 reflection-based correction provisional; exact fix in `AVeloxGameState`; projectile time reads carry the bias.
3. D3 widens the cheat surface: after idle, instant burst of 6 MG / 11 LG (LG ≈ 88 dmg in one frame) — same as an honest resend burst. `FireRateSpacingTolerance` 0.25 s < resend spread at 250 ms RTT (~330 ms): rails can be spacing-rejected near the ping limit.
4. D4: retrigger + early completion instead of only removing the GE.
5. B17 expected rewind ~160 ms; measured median 123–141 ms at 100 ms RTT.
6. LatchRun stray → separate movement task.
