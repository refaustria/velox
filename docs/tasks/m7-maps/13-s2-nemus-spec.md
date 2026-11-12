# 13 (S2) — Nemus spec module (pure Python)

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (pure Python; engine gate deferred)
**Depends on:** P4
**Wave:** 1 spec modules (parallel)

## Goal
Write the Nemus layout as a spec module from its concept. Pure Python, no editor, no build; may run in parallel with the other spec tasks. Deviations from a concept's coordinates are allowed where the validator demands them (the graph and the rules are binding, ADR 0006 §3); list each in the task notes.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/nemus_layout.py`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.2 (spec contract), §1.3-1.4, §2.1 (theme pieces), §4 (Nemus decisions), §5.3; concept `docs/maps/nemus.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [x] `create_map.py --map nemus --validate-only` green
- [x] LOS rules of its concept section 17.1 pass
- [x] Deviations from the concept listed in the task notes
- [x] Golden file is NOT written here (the B-task does it)
- [x] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
`maps/nemus_layout.py` written: 120 solids (boxes only, no `Wall` records), 8 Octs (6 minor trunks Trunk_300, Mother Tree lower Trunk_600 + upper Trunk_300), 3 stairs, 8 ramps (Radix Magna, Radix Australis, 6 hump faces), 5 pads, 41 items, 11 spawns, 34 links (5 JUMP_PAD, 3 ROCKET_JUMP, 2 JUMP, 1 JUMP_FAST, 23 DROP). `CLIP_MODE "walls"` + `Ceil_Sky` clip (needed by the rocket-jump clearance rule), `CLIP_H` 1680. `create_map.py --map nemus --validate-only` and `--selftest` green, `--mode art --validate-only` plans 600 kit instances with 0 protrusions, DM1 golden OK. No Nemus golden written. Nothing run in the engine.

**Pad landings (simulated by `pad_arc` vs concept, concept Z minus 320).**

| Pad | Concept landing | Authored | Simulated | Delta |
|---|---|---|---|---|
| P1 (moved, D2) | (-2922, 0, 320) | (-2900, 0, 320) | (-2904, 0, 316.6) | 4 cm vs authored, 22 cm vs concept |
| P2 | (-2899, 1823, 320) | same | (-2899, 1823, 319.9) | 0 |
| P3 | (-2899, -1823, 320) | same | (-2899, -1823, 319.9) | 0 |
| P4 | (3110, 369, 640) | same | (3110, 366, 637.4) | 3 cm |
| P5 | (1800, -1789, 320) | same | (1800, -1788, 319.2) | 1 cm |

All pad targets are the concept's (shifted by -320); no arc hits a solid before its apex.

**LOS rules of concept section 17.1.** `LOS_BLOCKED = [("spawns","majors"), ("Corona","RA")]` with `MAJOR_IDS = (Quad, RA, Mega)`: spawns -> Quad / RA / Mega 0 % visible (33 pairs), 15 Corona sample points (west, north, south strips) -> RA 0 % visible. Longest sightline into a spawn (P4 method): S11 61.2 m (map longest), S5 59.7, S8 / S10 59.0, S4 56.4, S6 56.0, S7 50.9, S3 50.2, S9 49.7, S2 42.6, S1 34.4 m. RA -> Mega on the upper ring (RA, terrace corner (-2560, -1800), 400 hop, west log, 400 break, Mega): 827 + 560 + 1740 + 560 + 200 = 38.9 m (window 38..45). Spawn spacing >= 10 m, spawn -> nearest weapon 4.9..7.5 m, ammo 6.7..11.6 m from its weapon, RJ clearance 1360 / 1360 / 1680, free-edge total 498.5 m (report only).

**Deviations from the concept (graph and rules binding).**
- **D1 datum: every concept Z is lowered by 320** (pit -320, tiers -240 / -160 / -80, forest floor 0, ruin ring 320, Corona 640, clip ceiling 1680). Reason: the generator has ONE ground height for spawns (`PlayerStart` at `FLOOR_Z + 100`, `spawn_geometry`), pads (`veloxmap.jump_pad` spawns at absolute Z 70, `mapgen.pad_launch/pad_arc` start at `FLOOR_Z`) and the spawn line of sight. Eleven spawns and four pads stand on the concept's L1, so L1 must be Z 0. Consequence: the lowest floor (pit) is -320, an exception to ADR 0006 E0.3 (e) like Orbita's; `left_arena` (floor - 1500) stays below KillZ -1000, so no band change is needed, but the lead should note it in B2 / B6. A generator feature that removes the cause: `Spawn.z` / `Pad.z` (floor height per record, default `FLOOR_Z`) read by `spawn_starts_and_camera`, `spawn_pads_and_links`, `pad_launch`, `pad_arc`, `pad_arc_clearance_problem`, the LOS point lists and the pad ring of the art plan.
- **D2 P1 moved from the Orchestra floor to the Cavea's west rim** at (-1200, 0) (same cause as D1: a pad on the pit floor is not at `FLOOR_Z`). It lands in the Templum gate at (-2900, 0, 320) (concept -2922), apex target (-2227, 0, 630). The Quad carrier now climbs the three west tiers (about 1.5 s) before the Quad run; concept risk 6 (snowball) gets milder, the pit loses its fifth fast exit (stairs N / S and Radix Magna remain). If `Pad.z` lands, P1 can go back to (-300, 0, -320) with the concept target (-1950, 0, 730).
- D3 Mother Tree moved 30 cm west, centre (2850, 0) instead of (2880, 0): the Oct gap rule (0 or >= 120 cm to the tree wall) fails by 25 cm at the concept position. Upper-trunk flats 2650..3050, Corona west strip 190 wide (was 220), east strip 250 (RG at 3190 unchanged), Tabulatum west ledge 390.
- D4 Quad stands on the pit floor (-320), no altar box (concept: "low altar" but item Z 0): with a 20 cm altar S7 / S9 see the Quad. The altar is an art-pass pedestal.
- D5 spawns: S3 (-2000, 600) -> (-1800, 2300), yaw 0 (Via Sacra): from the forecourt S3 sees the Mega on the south log across the open pit (nothing but the log's own lip hides a target 60 cm above the log top); behind the aqueduct wall nothing sees Quad, RA or Mega. S7 / S9 (1600, +-1450) -> (1800, +-1450): at 1600 the rim clears the Quad line by 2.5 cm. S11 stays 10 m from S3. This concentrates four spawns (S1, S2, S3, S11) on the Via Sacra.
- D6 Via Sacra west trunk (-1200, 2500) -> (-1200, 2550) (touches the tree wall instead of leaving a 38 cm gap; lane south of it stays 400 clear).
- D7 trunk octagons are the ADR's four-box `Oct` (kind `wall`, 1600 high for Trunk_300), Corona and Pons are `bridge` boxes 20 cm thick (300 clear below), aqueduct deck over the arches is a `bridge` box (not a lintel, which is no standing surface), L1 is `floor`-kind slabs (so pad arcs start on them as on DM1's Ground), tier rings are cut for the pit stairs (|x| < 150) and the Radix Magna trench (|y| < 200), forest floor east is cut |y| < 200 up to x 1300 (the root crosses the rim at 1281).
- D8 drops: 23 instead of about 22; the Pons east sides and the Templum corner pockets are left out (tree wall < 500 cm ahead), "Corona west -> root" lands on the Tabulatum west ledge (a 300 x 300 clear top cannot sit on a 20 degree slope). Links `Jump_TerraceLog`, `Jump_LogBreak` are one record each (the JUMP link is bidirectional); `JumpFast_Gap` west -> east.
- D9 Health_Medium "west tier 2" at (-700, 0, -160) and tier-3 shards at (+-900, +-900, -80) as in the concept; Bullets has no weapon (no `AMMO_FOR` entry), RL2 / SG2 / PG2 have no ammo entry of their own (one ammo item per type).
- D10 `LIGHTS = []`, `CAPTURES = ()` (art pass later), `ROOMS = []` (open sky).

**For B2.** The art plan has 600 kit instances (concept 403; the DM1 estimator counts one per tile and the plan tiles the 15 x 32 m ground in 800 cm tiles): check `audit_map.py` draw calls against the 600 budget before the art pass. Walkable area and nav build time unmeasured. `Radix_Magna` fill "solid" is sealed; the two `Pons` bridges and the Corona are floating (300 clear).

