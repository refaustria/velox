# 26 (H7) — Bots avoid hazards; L_HazardTest pad link

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H4, H5 (merged)
**Wave:** hazard track (before B4 Fornax / B5 Orbita)

## Goal
Close the two gaps H5 found (docs/tasks/m7-maps/10-h5-airborne-recovery.md, findings 1–2). Lead-added task (2026-11-13), not in ADR 0008's list.
1. **Lava avoidance, not only escape.** A bot respawning on L_HazardTest's main floor walks into the lava channel (H4 observation); H5 only gets it out in ~0.6 s (~75 damage). Bots must not path or strafe into a lava volume. Lead preference (reversible): both (a) the H4 edge probe rejects points inside a hazard volume (`UVeloxHazardSubsystem::IsInHazard/FindHazardAt`) and (b) hazard boxes get a high nav area cost (or are excluded) so paths route around lava; void needs no area (it has no nav). Keep the H4 gate: only on maps with hazard volumes.
2. **Pad_Gap jump-pad link on L_HazardTest is never taken** (base and H5): the bot runs over the pad without using the link and overshoots into the void. Find out why (link generation, link projection, pad trigger vs link start, path follower) and fix it, so `FtBotKnockOff`'s pad check can become a hard check and pad discipline is proven in a real flight, not only the unit test.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/*` (VeloxBotCombat, VeloxBotController, VeloxBotPathFollowingComponent, VeloxBotSettings, nav area class if needed)
- `Source/Velox/Combat/VeloxHazardVolume.*` (only to attach a nav modifier / area)
- `Tools/editor/create_hazard_test_map.py`, `Content/Maps/L_HazardTest.umap` (regenerate if the link fix needs it)
- `Source/Velox/Tests/VeloxBotRecoveryFt.cpp`, `VeloxBotEdgeFt.cpp`, new `FtBotLavaAvoid`, `infra/ci/fts.sh` rows
- this task file

## Acceptance criteria
- [ ] New `FtBotLavaAvoid`: bots respawned / routed next to the lava channel on L_HazardTest take 0 lava hits in N seeded trials (N ≥ 10) with the behaviour on; with `velox.bots.EdgeAware 0` the test shows hits (proves it catches the problem)
- [ ] `FtBotKnockOff` pad check is a hard check and passes: the bot takes `Pad_Gap`'s link and lands on the far platform in ≥ 8/10 seeds
- [ ] FtBotEdge, FtBotLavaEscape, FtHazard unchanged green; build (Editor+Game) + full automation + fts.sh green
- [ ] BotRun ×3 on L_DM1_Blockout behaviour PASS (cost windows: host-load A/B rule as in H4/H5); DM1 golden OK
- [ ] navlint on L_HazardTest PASS

## Notes / outcome
(Filled by the agent.)
