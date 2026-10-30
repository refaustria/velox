# 04c-c — Hear/dodge proof, fire-tick signal, FtItemRouting peaceful, off-mesh giveup

**Milestone:** M4 Bots
**Agent:** dev
**Status:** done (merged f800608)
**Depends on:** 04b, ADR 0005 D14–D17, D19, review F3–F6

## Goal
Hearing and projectile dodge are proven deterministically; budget fire-tick exclusion uses the right signal; FtItemRouting is deterministic; stranded bots give up cleanly.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotCombat.{h,cpp}`: `VeloxBotHear` / `VeloxBotDodge` log lines + debug counters
- `Source/Velox/Bots/VeloxBotController.{h,cpp}`: fire-tick signal = fire ability `NextFireTime` advanced (around line 1442); `OffMeshMaxAttempts 6` → `giveup` → suicide (B28 respawn, no teleport)
- `Source/Velox/Bots/VeloxBotSubsystem.{h,cpp}`: fire-tick exclusion (around line 536), `max_fire_us` report-only, counters in the budget line
- `Source/Velox/Tests/VeloxBotFunctionalTest.cpp`: new `velox.bots.FtCombatEvents` (D15); FtItemRouting sets `Peaceful 1` and checks frames ≤ 3×K
- `infra/ci/fts.sh`, `infra/ci/README.md`: 6 FTs
Do not touch ItemRouting/Decision (04c-a) or the follower/stuck rule (04c-b).

## Acceptance criteria
- [ ] FtCombatEvents: damage from a dummy instigator behind a frozen Hard bot → `VeloxBotHear` + turn; spawned rocket → `VeloxBotDodge` + lateral speed
- [ ] fts.sh OK 6/6 three times in a row
- [ ] build.sh green; -DisableAdaptiveUnity build succeeds

## Notes / outcome
dev, 2026-10-30. build.sh 95 tests green; fts.sh OK 6/6 ×3; -DisableAdaptiveUnity OK. FtCombatEvents ×5: hear 0.017 s (limit 0.234), turn 0.43 s (yaw error 18.7°), dodge 0.267 s (limit 0.35), lateral 200 cm/s. Budget line ends with `heard= dodges=`.
Open for M4-07: giveup path and corrected `max_fire_us` not exercised live (grep `giveup`, `heard`, `dodges` in the soak); heard/dodge counters reset per life; lateral check stops at the first sample ≥ 200 cm/s.
