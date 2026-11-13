# 27 (H8) — follow-ups from the H6 hazard review

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done (dev-hard, review pending)
**Depends on:** H6 review (merged), H7
**Wave:** hazard track (lead-added 2026-11-13 from `docs/adr/reviews/0008-hazard-review.md` §Follow-up tasks)

## Goal
F1, F2, F4 (caches), F5, F6, F7, F8, F11, F12, F13 and ruling 3 (pad zeroing removed: pad-arrive steering replaces zero input on hazard maps too; zero input only when on course). Defect details, rulings and proposed amendments (G1–G7, D52–D55, C12) are in `docs/adr/reviews/0008-hazard-review.md` — read it first.

## Scope — files/modules the agent may touch
`Source/Velox/Bots/*`, `Source/Velox/Combat/VeloxHazardVolume.*`, `Source/Velox/Bots/VeloxNavCoverage.cpp`, `Source/Velox/Tests/VeloxBot*Ft.cpp`, `Source/Velox/Tests/VeloxHazardFt.cpp`, `infra/ci/fts.sh` (new rows), this file

## Acceptance criteria
- [x] build (Editor+Game) and full automation green; `fts.sh` green incl. a new knock-in-pad-flight case and FtBotKnockOff pad part 10/10; `navlint.sh L_HazardTest` PASS with `hazard_m2` equal to the volume boxes (36 m²); new `VeloxNav hazard_area` lint: count 0 on L_HazardTest and no Fornax item/spawn/link end inside a lava footprint; FtBotEdge, FtBotLavaAvoid seeds 1–5, FtBotLavaEscape unchanged green; DM1 golden OK. **Before B4.**

## Notes / outcome
dev-hard, 2026-11-13, branch `wip/m7-27-h8-review-fixes` (base origin/m7-maps 23f5231), engine 5.8.4. Raw logs:
/mnt/oldroot/velox-scratch/m7-h8/.

- **F1 + F12 (G5 b/c).** `AVeloxHazardVolume` no longer creates a nav area and no longer includes `Bots/` (no cvar read).
  `UVeloxBotSubsystem` adds one `LavaNavArea` box per Lava volume on `FWorldDelegates::OnWorldInitializedActors` (before the
  runtime build, as H7's PostInitializeComponents did): the volume's own footprint, bed to `HazardNavTopCm` (10, new;
  `HazardNavPaddingCm`/`ZCm` removed) above the surface (`LavaNavAreaBox`, unit-tested). New lint at nav ready
  (`CountHazardAreaPoints`): `VeloxNav hazard_area` Warning per pickup / PlayerStart / link end whose projected poly has
  `UVeloxNavArea_Hazard`, summary `VeloxNav hazard_area count= points= lava_polys= areas=`. L_HazardTest: count=0 points=6
  areas=1. **Bed check:** `VeloxNav hazard_polys` = 0 polys inside (and touching) the lava box: the generator's "bed is
  invisible to nav" rule works; the H5/H7 "bed's mesh" wording was wrong. Fornax (pure Python, layout records, 8 lava
  volumes, 79 points = items, spawns, pads, pad landings, link starts/ends): 0 inside a lava footprint (bed .. surface +
  110 cm), nearest 60 cm (the lava-hop link starts; H7's 100 cm padding would have covered all of them).
  `create_map.py --map fornax --validate-only`: layout valid.
- **F2 (G6).** `VeloxNavCoverage` exempts `Volume->GetBox()->Bounds`: `navlint.sh L_HazardTest` PASS, hazard_m2 **36.00**
  (lava 36, void slab 0: no surface is sampled inside it).
- **F4.** `HasHopLanding` cached `EdgeProbeCheckSeconds` per grounded stretch (reset on every airborne tick) and only while
  the predicted landing stays within `HopLandingRecheckCm` (30, new) of the tested one. Without that key the first run
  failed FtBotEdge seeds 1 and 3 (void deaths: a strafe flip pointed the hop over the edge on a cached "clear").
  FtBotEdge now: 89-128 landing tests per 60 s instead of one per hop-wanting tick. `UpdateHazardLanding` probes at most
  every `EdgeProbeCheckSeconds`; the airborne recovery's safe-target search over lava runs every `HazardEscapeSearchSeconds`.
  No cost-judged hazard set was run (H9's job).
- **F5 (G2).** `VeloxBotCombatGlue::HopLandsSafely`: one Pawn-channel trace (pawns ignored) at the landing XY from take-off
  height + hop apex down to take-off - distance x tan(walkable slope) (`VeloxBotCombat::HopGroundWindow`), then the hit
  projected with 20 cm x `HopLandingProjectHalfHeightCm` (50, new) and the lava test. No ground in the window = refused.
  Used by the guard and by `UpdateHazardLanding` (which falls back to the H7 take-off-level test late in a hop). Slope case:
  `Velox.Bots.HazardRules` window test (flat, 30 deg chute 335 cm down, 20 deg ramp up, void outside). L_HazardTest has no
  ramp (fixture is outside scope): the FT ramp check belongs to B4's Fornax chute, as the review allows. Consequence: a hop
  off a ledge onto a lower floor within the walkable gradient is no longer refused (not a death).
- **F6.** Overlay probe extent `EdgeProbeProjectRadiusCm` 45 x `OnMeshHalfHeightCm`. FtBotEdge seeds 1/2/3: PASS, 0 deaths,
  mirror 133/171/161, drop 98/52/21, hop refused 125/84/133, min edge -11/36/49 cm (H4: -18/5/5), target visible
  1.00/1.00/1.00.
- **F7.** The reversed back-off wish is probed; unsafe = no overlay wish (`bEdgeNoWish`, counter `EdgeNoWishCount`;
  FtBotEdge 0, no back-off on that map).
- **F8.** `HandleDamaged` returns for `Velox.Damage.Hazard` (`VeloxBotCombat::IsHeardDamage`, unit-tested with lava/void/other).
- **F11.** `AVeloxHazardVolume::AdvanceLavaTick`: the clock advances by `LavaTickSeconds`; more than one interval behind
  restarts at now (one hit per frame). Unit test: 39 ticks in 10 s at 8.5 ms frames.
- **F13.** `ProjectsOntoNav` traces the Pawn channel (overlap triggers, hazard boxes and the void slab do not block; pawns
  ignored) and asks `IsInHazard(hit + 1 cm, Lava)`; the projected-point test is Lava-only too, so void never backs off.
- **Ruling 3 (G4).** `ApplyHazardBehaviours` no longer zeroes the pad flight: the follower's arrive wish is the only input
  on every map (counted `PadDisciplineTicks`, steering ticks `PadArriveTicks`). On Pad_Gap the arrive wish steers on every
  flight tick (arrive 74/74 ticks: the arc ends ~50-100 cm short of the link end at x 1400), landings x 1294-1349 (H7 zeroed:
  1281-1335). FtBotKnockOff: knock-off 10/10, pad part **10/10**, new pad-knock part (300 cm/s sideways, 0.1-0.4 s into the
  flight, Crc32(4000+trial), `padknocks=N`): **9/10** landed (need 8); the miss is trial 1, where the path crossed the pad
  off-link and no knock was applied (lands on the platform anyway). In 7 of 9 knocked trials the hit relaunches the link
  (`Relaunched`), so the airborne recovery finishes the correction; in 2 the arrive wish does.
- Verification (soak lock, ports 17877/17878): `build.sh --targets VeloxEditor,Velox` green; automation 144/144;
  `fts.sh` OK 14/14; FtBotLavaAvoid seeds 1-5 PASS (0 lava entries, 8/8 routes arrived, dummy visible 1.00 each);
  FtBotLavaEscape 0.62/0.62/0.62 s; FtHazard PASS; navlint as above; `create_map.py --map dm1 --golden check` OK.
