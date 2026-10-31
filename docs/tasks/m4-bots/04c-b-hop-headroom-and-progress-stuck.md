# 04c-b — Hop headroom guard + progress-based stuck rule

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** partially done (merged 88cfaa6), open items for architect
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

## Notes / outcome
dev-hard, 2026-10-30. Headroom guard (sweep 124 cm, ≤ every 0.1 s, plain path segments) + progress rule (`StuckProgressCm` 100 within `StuckSeconds` 2). FtOffMesh `ramp_strip_wall` now asserts the goal: old code FAIL (25 s hopping), new PASS 2.43 s. build.sh 95 green, fts 5/5 ×3, unity build OK.
FFA 3 Medium 120 s seeds 1–10: stuck 0/2/7/2/4/1/0/1/0/1 (8/10 meet ≤ 2; old 1/1/0/0/0/1/4/1). Ramp underside: longest stay outside Fight 0–4 s (met).
For architect (one pass before 07):
1. Ratify: progress clock pauses while the combat overlay owns the wish (enforcing: 10–22 strikes/run; resetting: 9 s scrape).
2. Drop-link Approach phase has no timeout; one bot hung 57 s (stuck and off-mesh rules are suspended on links).
3. Remaining progress strikes: bunny-hop reversal / wall scraping; proposal "no hop when velocity points away from the target".
4. Combat-overlay stalls (7 s against a platform face) caught by neither rule; dodge jump bypasses the headroom guard.
5. AgentHeight 140 = capsule height lets nav lead under the slab (D18 alternative AgentHeight 255).

Architect ruling (2026-10-30, ADR 0005 D27–D34): 1 → ratified with a 4 s pause cap (D27); 2 → `LinkTimeoutSeconds 6` + `LinkBelowStartCm 100`, timeout counts as a strike (D28); 3 → now, `MaxHopHeadingDeg 45` (D29); 4 → now, fight-stall rule with a 4 s Fight suppression (D30) and the dodge jump gated by the headroom sweep (D31); 5 → 140 stays, 255 rejected for good (D32). Implementation: task 04f.
