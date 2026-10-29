# M2-08d report (2026-10-29)
build.sh 53 tests, 0 failures (`Saved/CI/20261029-203021-3711244`). FireRun/FireRunControl/RocketJumpRun `--repeat 3`: 3/3, hinted |rewind−view| mean 9.6–12.8 ms (max 32 ms), rewind_med 136–143 ms. Full list run 1 under load 1/3 (PadRun 11.85 cm; RocketJumpRun C=2 window 9.0/45.9 with unequal ts — `Saved/CI/20261029-204056-3737118`), run 2 3/3 (`Saved/CI/20261029-210133-3804616`, FireRun dmean 6.0–11.6 ms).
- `AVeloxGameState::PreReplication` sets `ReplicatedWorldTimeSecondsDouble = World->GetTimeSeconds()` (B31).
- `EstimateServerTime` = `GetServerWorldTimeSeconds()`; reflection staleness removed; projectile `TickProxy` and FireProjectile read through it.
- Fallback delay: `FDelayMemory` on the PlayerController, last proxy measurement reused for < 2 s, else `velox.lagcomp.FallbackInterpolationDelay` 0.03 (B30).
- Not exercised in a soak: the fallback path (all shots `delay_src=proxies`), covered by unit rows only.
- Watch: RocketJumpRun failed once under load with unequal self-splash ts (1.931298 vs 1.731284) — check on an idle machine in the final M2 soak.
