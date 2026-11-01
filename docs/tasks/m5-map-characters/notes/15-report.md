# Task 15 report: DM1 art pass (`create_dm1_map.py --mode art` -> `/Game/Maps/DM1/L_DM1`)

Base 321d519 plus the merged 07b runner (origin/m5-map-characters, no `Source/` change, no rebuild needed). Evidence: `docs/adr/evidence/0006-dm1-art/`
(`host.txt` per set with loadavg and the foreign-UE-process list at start and end, `run.out.gz`, `links-by-record.txt`, `audit/`).
All sets ran 2026-11-01 07:15 to 07:53 local, under the soak lock, no foreign UE process at any start or end, load 0.46 to 1.47.

## Result: done

- `--mode art` writes `L_DM1`: 137 collision solids + 12 layout clips as invisible `Clip_*` (hidden, not rendered, no shadow, BlockAll, same nav influence and
  `fill_collision_underneath_for_navmesh` as the blockout boxes), **518 kit instances** (Floor_400 147, Wall_400 135, Wall_Trim 116, Ceiling_400 44, Light_Fixture 31,
  Bridge_400 26, Wall_Door400 16, Pad_Ring 3), 31 static point lights, 7 sphere reflection captures, static SkyLight with `TC_Sky_DM1`, LightmassImportanceVolume,
  world settings 3 bounces / quality 1 / level scale 1 / compressed / volumetric lightmap cell 200. Pads, items, spawns, links, nav bounds, KillZ, camera, game mode
  are the blockout's calls. Lighting is not baked (fallback C); `L_DM1` is not in `+MapsToCook`.
- Offline: `python3 -I Tools/editor/create_dm1_map.py --validate-only --mode art` checks the layout and mirrors the protrusion audit on the plan (0 over 4.99 cm).

## Audit (`audit_map.py /Game/Maps/DM1/L_DM1 --protrusion --lighting --diff-collision L_DM1_Blockout L_DM1`) RESULT PASS

draw_calls_est 568/600 (kit 518 + pickups + pad discs), triangles 18 180/300 000, lightmap 12.35/48 MB, textures 1.33/256 MB, lm_res>64 50/120,
kit meshes 8/25, translucent 0, protrusion 518 checked / 0 > 5 cm, **collision diff 0 differences (blocking 150/150, gameplay actors 68/68)**.
`--lighting`: registry=0 lightmapped=0/671 vlm=0 (expected, task 16 part B).

## Gates

navlint `/Game/Maps/DM1/L_DM1`: walkable 2398 m2, covered 99.96 %, clusters=0, lowclear=0 PASS.
FT_DM1_Reachability on L_DM1 (`velox.bots.FtDm1Reachability` is map-independent; run by hand with the `fts.sh` command line, map swapped): result=PASS checks=5 failed=0.
`-game -nullrhi` load: no map errors or warnings. DM1Run on L_DM1: PASS (pads 3, strays 0).

## Confirmation sets under `bands/L_DM1.sh` (no re-derivation)

| Set | Result |
|---|---|
| BotRun x3 | set PASS: kills 7 5 7 (sum 19, band 11), pickups 64 65 59, majors 4/4, stuck 0, max dwell 4 5 3, not_at_goal 0 |
| BotDuel x3 | set PASS: frags 3 4 5 (sum 12, band 7), camp episodes 10 15 11 (sum 36, band 21), Mega within_3s 4 / within_10s 6, stuck 0, not_at_goal 0 |
| FFA x10 | SET PASS: sum_kills 54 (band 34), stuck 0, bursts 0, max dwell 4, Drop 95.4 % (146/153; bound 93.1 %), JumpPad 100 % (91/91), coverage 35/35, nav 0.93 s |

Per-record link table (FFA x10; ok/missed, aborted not counted): ALedge_N 10/0, DBalcony_1 2/0, _2 3/0, _3 20/0, _4 10/0, **DockW_N 17/3, DockE_N 24/2**, Top_W 36/1,
Top_E 24/1, PadLink_A 13, PadLink_B 16, PadLink_D 62. Dock N in FFA: 41/46 = 89.1 % (06b: 40/44 = 90.9 %); over all 15 soaks 58/67 = 86.6 % (BotDuel DockE_N 8/11).
Drop overall stays above the 93.1 % trigger, so no layout task; dock N is the watch item for 07c. BotRun Drop 35/36, JumpPad 12/13; BotDuel Drop 27/30 (90.0 %), JumpPad 18/19.

`not_at_goal` (07b counter): BotRun 0, BotDuel 0, FFA 11 lines (6 episodes, runs 3, 4, 5, 9). The targets are z 184 to 202 (and 505) points: Fight-goal chases of enemies that are
airborne (run 3: a pad D flight, target (-24, -796, 203) right after "JumpPad PadLink_D ok"). They are not stacked items. The 06b point (108, 152, 157) is the same class
(E, an enemy in the air); the one stacked item in the layout (E_H25 on the mid bridge, over the E ground floor) was not the target of any line. 06b logged 1 (FFA) and 2 (BotRun).
Report-only; not a layout issue; no change made.

## Deviations

1. `Ramp_15` and `Stairs_5x20` are not used: the audit's protrusion rule tests the mesh bounding box, which for a wedge contains the walkable air above the slope, so any wedge
   instance is flagged by construction. The ramp is 6 `Bridge_400` plates (Plate material) pitched 15 degrees and squashed to the 20 cm clip slab; stairs are one `Floor_400` slab
   per clip step (the 20 cm slab edge is the riser) plus wall fill tiles on the exposed sides. The under-ramp cavity shows a sawtooth between the stepped fill clips and the plates (visual only).
2. Door pieces: `Wall_Door300` has the same bounding-box problem (hole) and is not used; door-sized openings get `Wall_Door400` lintel caps on both faces and recessed jamb tiles.
3. Tiles are scaled to stay within 600 draw calls (the audit counts components): walls up to 1000 cm, floors on an 800 cm grid, ceilings on 1200 cm (texel density 40 to 60 % of nominal).
   Floor tiles beyond the ground carry a lightmap override of 64, stair slabs 32. Not used: Pillar, Pedestal, Railing_Stub, Skirting, Ceiling_Beam, Ledge_Edge, Ramp_15, Stairs_5x20, Wall_Half, Wall_Door300.
4. `Wall_Trim` is sunk 0.3 cm (its 5 cm proud trim measured exactly 5.0 and tripped the strict audit limit in the first run).
5. `veloxmap.py`: `box(..., clip=True)` (and `ramp`) give the nav-affecting invisible clip; the existing `clip_box` stays the nav-neutral one. New helpers `kit_piece`, `static_point_light`,
   `static_spot_light`, `sphere_reflection_capture`, `sky_light`, `lightmass_importance_volume`, `lightmass_world_settings`. `kit_piece` forces a profile change before NoCollision, because
   the kit meshes already default to NoCollision and the unchanged value was not serialised (a reloaded component came back BlockAll: the first audit showed 518 blocking kit instances).
6. Layout: `Light` records extended (`lumens rgb radius ceiling`), 31 lights, `CAPTURES`, `LIGHTMASS_VOLUME`; geometry untouched. No directional light in the art map (every room has a ceiling). No landing-marker discs.
7. Runner: `fts.sh` and `nettest.sh` hard-code `L_DM1_Blockout` for the FT and DM1Run; I ran both on L_DM1 by hand (a temporary copy of `nettest.sh` with the map swapped, deleted). `infra/ci` unchanged.
   The proper fix (a `--map` for DM1Run and the FT) belongs to a runner task.
