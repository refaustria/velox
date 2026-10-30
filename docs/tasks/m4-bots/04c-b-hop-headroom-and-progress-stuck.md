# 04c-b — Hop headroom guard + progress-based stuck rule

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** todo
**Depends on:** 04b, ADR 0005 D18, review F2

## Goal
A bot no longer oscillates under low ceilings (ramp-top strip under the ramp slab), and a bot that makes no progress gets a stuck strike.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotPathFollowingComponent.{h,cpp}`: headroom sweep upward by `JumpVelocity²/(2g)+10`, at most every 0.1 s; no hop if blocked
- `Source/Velox/Bots/VeloxBotController.{h,cpp}`: stuck rule only (`StuckProgressCm 100`)
- `Source/Velox/Bots/VeloxBotSettings.{h,cpp}`, `Config/DefaultGame.ini` for the tunables
- `Source/Velox/Tests/VeloxBotTests.cpp`, `VeloxBotFunctionalTest.cpp`: rows or an FT case
Do not touch ItemRouting/Decision (04c-a) or fire/hear/dodge/off-mesh giveup (04c-c).

## Acceptance criteria
- [ ] Repeat the 04b ramp-top run: `VeloxBotStuck` ≤ 2, no bot stays > 5 s within 300 cm of the ramp underside
- [ ] build.sh green; fts.sh OK 5/5; -DisableAdaptiveUnity build succeeds
