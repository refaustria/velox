# 09 (H4) — Edge-aware overlay and hop landing guard

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done (dev-hard, 2026-11-13)
**Depends on:** H2
**Wave:** hazard track

## Goal
Edge probe in the combat overlay (`EdgeProbeCm` 150, mirrored lateral, drop lateral/retreat) and the hop landing guard, both active only when `UVeloxHazardSubsystem::HasHazards()`.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotCombat.*`
- `Source/Velox/Bots/VeloxBotPathFollowingComponent.*`
- `Source/Velox/Bots/VeloxBotSettings.h`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §3.6 items 1-2, ADR 0005 §5/§10 (D17, D18)
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `FtBotEdge` green
- [ ] BotRun x 3 on `L_DM1_Blockout` inside its bands (proves DM1 behaviour unchanged by the `HasHazards()` gate)
- [ ] New tunables in `UVeloxBotSettings`, no magic numbers
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
dev-hard, 2026-11-13, branch `wip/m7-09-h4-edge-overlay` (base origin/m7-maps), engine 5.8.4.

- Gate: `VeloxBotCombatGlue::IsEdgeAware(World)` = `UVeloxHazardSubsystem::HasHazards()` and cvar `velox.bots.EdgeAware`
  (default 1; 0 = A/B off). Off hazard maps every code path is the pre-H4 one (same wish, same jump), so DM1 / test-map bands hold.
- Overlay (3.6.1, `VeloxBotCombatGlue::Overlay`): after `CombatWish`, every `EdgeProbeCheckSeconds` (0.1) probe
  `Feet + Vel2D x EdgeProbeLookaheadSeconds (0.25) + Wish x EdgeProbeCm (150)` with `GetOnMeshExtent()`; fail -> mirrored lateral
  (StrafeSign and, while dodging, the dodge dir flipped; kept); fail -> cached Drop: `FVeloxCombatWishInput::bNoLateralOrRetreat`
  (closing in and the blended path stay; a zero wish leaves the path wish). World-free `EdgeProbePoint`, `ChooseEdgeWish`.
- Hop landing guard (3.6.2, `UVeloxBotPathFollowingComponent::HasHopLanding`): grounded tick that wants a hop (path bunny hop in
  `FollowPathSegment`, overlay dodge jump) is refused when `Feet + Vel2D x (HopAirSeconds + HopLandingSlackSeconds 0.1)` does not
  project within `(HopLandingProjectRadiusCm 20, 20, OnMeshHalfHeightCm)`. Deviation from the ADR wording: not `GetOnMeshExtent()`
  (100 cm horizontal): the mesh is eroded by 45 cm, so the wide extent accepts landings ~55 cm past a ledge; with it seed 1 died at
  6.9 s (0.73 s hop at 800 cm/s, landing 20 cm past the platform edge). Uncached (one projection per grounded hop tick).
- Tunables in `UVeloxBotSettings` (Bots|Hazard): EdgeProbeCm, EdgeProbeLookaheadSeconds, EdgeProbeCheckSeconds,
  HopLandingSlackSeconds, HopLandingProjectRadiusCm.
- Tests: `Velox.Bots.CombatRules` (edge fallback, probe point, choice, landing point); `velox.bots.FtBotEdge`
  (`Tests/VeloxBotEdgeFt.cpp`, fts.sh row `FtBotEdge|L_HazardTest`): Hard bot (spawn loadout) vs the invulnerable dummy moved
  to the platform centre (strafe 250), 60 s, no death, target visible >= 50 %, shots > 0, probes > 0.
  Seeds 1/2/3: PASS, 0 deaths, probes 601, mirror 110/116/99, drop 14/12/16, hops refused 152/150/143, min edge -18/5/5 cm.
  A/B `velox.bots.EdgeAware 0`: void death at 1.7/1.1/1.1 s on all three seeds.
- Verification (soak lock; raw logs /mnt/oldroot/velox-scratch/m7-h4/): `build.sh --targets VeloxEditor,Velox` green, automation
  143/143; `fts.sh` OK 10/10 incl. FtBotEdge; `create_map.py --map dm1 --golden check` OK.
  BotRun x 3 on L_DM1_Blockout (bands L_DM1): set PASS (behaviour PASS, cost PASS; r3 slow throughout, cost n/j), stuck 0,
  left_arena none, world_kills 0. Default nettest list (18 scripts) once: 16/18 PASS; BotRun and BotDuel (L_ItemTest) FAIL on
  D47 cost windows only (early windows 1-3, frame_other_ref ~2000 us vs 837-1187 on a quiet host, load 3-9 from other work),
  behaviour clean. BotRun/BotDuel x 3 follow-up (M6-07 rule): BotRun behaviour PASS / cost FAIL; BotDuel cost FAIL and
  JumpPad 29/33 = 87.9 % (the known marginal pad, cf. M6-02 G4 35/39). A/B with the base sources (origin/m7-maps) on the same
  host, BotRun/BotDuel x 2: base BotDuel r2 also cost FAIL (windows 2,3,14,20 at frame_other_ref 1993 us), so the cost windows
  are the host; per-category bot costs scale uniformly (overlay/perceive 0.41-0.49 in both builds). Off hazard maps the H4 code
  is decision-identical to the base (IsEdgeAware false -> same wish, same jump).
- Out of scope, noticed: after a death the bot respawns on the main floor and walked into the lava channel twice in one run
  (H5 hazard escape / nav cost of lava are not H4).

