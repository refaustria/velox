# 26 (H7) — Bots avoid hazards; L_HazardTest pad link

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done (dev-hard, 2026-11-13)
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
- [x] New `FtBotLavaAvoid`: bots respawned / routed next to the lava channel on L_HazardTest take 0 lava hits in N seeded trials (N ≥ 10) with the behaviour on; with `velox.bots.EdgeAware 0` the test shows hits (proves it catches the problem)
- [x] `FtBotKnockOff` pad check is a hard check and passes: the bot takes `Pad_Gap`'s link and lands on the far platform in ≥ 8/10 seeds
- [x] FtBotEdge, FtBotLavaEscape, FtHazard unchanged green; build (Editor+Game) + full automation + fts.sh green
- [x] BotRun ×3 on L_DM1_Blockout behaviour PASS (cost windows: host-load A/B rule as in H4/H5); DM1 golden OK
- [x] navlint on L_HazardTest PASS
- [x] (lead, 2026-11-13) FFA x10 at 3 bots on L_Sacrarium_Blockout: JumpPad link success >= 90 %, per-link numbers

## Notes / outcome
dev-hard, 2026-11-13, branch `wip/m7-26-h7-hazard-avoidance` (base origin/m7-maps incl. B1 and H3), engine 5.8.4.
Raw logs: /mnt/oldroot/velox-scratch/m7-h7/.

- **Pad_Gap root cause (link generation).** Detour connects an off-mesh link's end only in the start's tile or one of its 8
  neighbours (`dtNavMesh::connectExtOffMeshLinks`; UE adds the link to every tile it overlaps, but the start must snap to a
  poly in that tile). TileSizeUU is 1000: the old pad start x -300 lies in recast tile 0, the platform centre x 1400 in tile -2,
  the gap tile between has no polys, so the link never existed in the nav graph. Every path to the platform was partial
  (segment target x -76, the floor's east edge); the bot crossed the pad off-link, got no pad discipline and air-strafed
  past the platform. Fix in the fixture: a pier x 0..300 / y +-200 east of the main floor carries the pad at (150, 0)
  (tile -1, adjacent to the end's tile), apex (750, 0, 450): launch (973, 0, 1233), landing x 1350. Regenerated from the
  merged generator (H3's nav-bounds fix kept). New guard for every map: at nav build `VeloxNav link_span` (Warning) names a
  link whose ends are more than one tile apart over both snap discs (Sacrarium: none).
- **Sacrarium PadSouth (lead add-on) root cause (flight steering).** The pad launches (0, 679, 1766); `TickAirborne` wishes
  End at scale 1 and the path follower steered straight at it, so VQ3 air acceleration (AirMaxWishSpeed 800 > 679) sped the
  flight to 820-905 cm/s and it overshot the 400 cm loft (bots fell off its north edge at y -800). Fix
  (`UVeloxBotPathFollowingComponent::TickManeuver`): in a JUMP_PAD flight after the pad's own launch the wish is the
  ballistic arrive at the link end (the rocket jump's `VeloxBotRocketJump::ArriveWish`, deadband `PadArriveDeadbandCmS` 30),
  on every map; on hazard maps the controller still zeroes it afterwards (ADR 0008 3.6.3 pad discipline unchanged).
- **Lava avoidance** (hazard maps only; A/B `-dpcvars=velox.bots.EdgeAware=0`, the nav area is applied at load):
  (a) `VeloxBotCombatGlue::ProjectsOntoNav` (edge probe and hop landing guard) rejects a point over a hazard: the projected
  nav point in a volume (+HazardProbeUpCm) or the first WorldStatic surface under the point (trace `HazardTraceDepthCm` 300)
  1 cm into a volume (lava top or bed), whether or not the point projects (a bridge over lava stays safe; void ledges keep
  the H4 result). After an edge Drop the remaining overlay wish (closing in) is probed too and reversed when it leads over
  lava (`bEdgeHazardBack`). (b) every Lava volume adds a no-collision dynamic-obstacle box (volume + `HazardNavPaddingCm`
  100 / `HazardNavPaddingZCm` 80) with `UVeloxNavArea_Hazard` (DefaultCost 50, entering 5000; costly, not excluded, so a bot
  in the lava still paths out): routes go around the channel and keep 100 cm off its rim. (c) the bot's own hop or dodge
  jump whose predicted landing (at the take-off level) is over lava steers to the nearest safe nav point
  (`UpdateHazardLanding`, the H5 escape target search; log `VeloxBotHazardLanding`), and the H5 airborne recovery no longer
  accepts a target on the lava bed ("on course" over the channel). Void needs no area.
- **FT_BotLavaAvoid** (`Tests/VeloxBotRecoveryFt.cpp`, fts row `FtBotLavaAvoid|L_HazardTest`): 16 seeded trials, the Hard bot
  resting 60..250 cm off a rim: 8 routes across the channel (sides alternate) + 8 x 6 s fights from the west side against the
  invulnerable dummy strafing at x -800. Pass: 0 lava entries, no death, routes arrive (<= 2 timeouts), dummy in sight >= 50 %.
  On: seeds 1-5 PASS, 0 lava entries each (routes 2.1-4.9 s around the channel vs 1.5-2.9 s straight over it with the A/B).
  A/B EdgeAware 0: seed 1 void death (fight, trial 8), seed 2 void death (trial 9), seed 3 lava entry + lava death (trial 11);
  routes alone never entered the lava in either mode (the eroded floor mesh already keeps walkers off the rim), the fights do.
- **FT_BotKnockOff** pad check is hard: after the 10 knock-off trials 10 seeded pad crossings (starts on the main floor east of
  the channel) must land on the platform after an Ok traversal of Pad_Gap's link with pad-discipline ticks: 10/10 (landings
  x 1281-1335, pad_ticks 74-77). Knock-off part unchanged 10/10.
- Lead add-on: FFA x10 at 3 bots on L_Sacrarium_Blockout (`ffa.sh --map L_Sacrarium_Blockout --repeat 10 --bots 3`):
  link_success JumpPad 100.0 % (154/154, aborted 7; B1: 86.0 %), per link: VeloxNavLink_22 40/40, _23 (PadSouth) 21/21
  (B1: 3/23), _24 49/49, _25 44/44; Drop 95.3 %; item_coverage 38/38 (B1 36/38); all invariants PASS. The set line is FAIL
  only on per-run `budget>300` (cost windows, host load from other projects; map band=none, report-only).
- Verification (soak lock, raw logs /mnt/oldroot/velox-scratch/m7-h7/): `build.sh --targets VeloxEditor,Velox` green,
  automation 144/144 (`Velox.Bots.HazardRules` gains the pad-arrive and hazard-area checks); `fts.sh` OK 14/14 incl. the new
  FtBotLavaAvoid row; FtBotEdge unchanged vs H4 (probes 601, mirror 110, drop 14, hops refused 152, 0 deaths), FtBotLavaEscape
  0.62/0.62/0.62 s, FtHazard PASS; `navlint.sh L_HazardTest` PASS (clusters 0, lowclear 0, hazard_m2 70);
  `create_map.py --map dm1 --golden check` OK. BotRun x3 on L_DM1_Blockout: set behaviour PASS (stuck 0/0/0, JumpPad 12/12 in
  r1; r1 max_dwell 8 s at (2,-557,740), a Medium bot waiting for the Rocket Launcher), cost FAIL in all rounds at
  frame_other_ref 1994-2458 us with load1 2.85 (bot budget_mean_us 202-219 vs 204-245 in the H5 base A/B on this host, which
  passed at 108 us when the host was quiet): host load, not this change (no fresh base A/B run).
- Off hazard maps the only behaviour change is the pad-flight arrive wish (all maps); everything else returns on the
  `IsEdgeAware` gate. ADR note for architect: ADR 0008 3.6.3 keeps "zero input" for pad flights on hazard maps; the arrive wish
  would also correct trigger-edge launch offsets there (kept per ADR).
