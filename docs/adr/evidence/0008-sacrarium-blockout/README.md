# Sacrarium blockout evidence (M7 B1, ADR 0008 5.3)

Engine 5.8.4. Raw logs: /mnt/oldroot/velox-scratch/m7-sacrarium/ (s1 = first full sets, run1..run6 = SacrariumRun iterations).

| 5.3 | Result | File |
|---|---|---|
| (a) validate-only + golden | PASS (127 solids, 38 items, 9 spawns, 22 links) | Tools/editor/maps/golden/sacrarium.json |
| (b) navlint | PASS clusters=0 lowclear=0 (after the layout iteration below) | navlint.txt |
| (c) FtMapReachability (WalkExempt Railgun) | PASS checks=7 | fts-map-reachability.txt |
| (d) SacrariumRun 100 ms / 2 % | PASS, 3 pads, 9/9 checkpoints | sacrariumrun.txt |
| (e) BotRun x3 / FFA x10 at 3 / FFA x10 at 6 | FFA3 FAIL (link_success JumpPad 86.0 %, item coverage 36/38); FFA6 FAIL (JumpPad 85.8 %, coverage 37/38, sum_stuck 6 and dwell 6); BotRun x3 FAIL (JumpPad 82.5 % 33/40; stuck 0, dwell <= 4, majors 4/4; cost windows 1-3 of each round over) | ffa3-set.txt, ffa6-set.txt, botrun-set.txt |
| (f) evidence | this directory | |

Probable cause (not verified by a probe): VeloxNavLink_23 (PadSouth, shrine pad onto the loft) misses 20/23 (FFA3) and 36/52 (FFA6); the other pads are 94-99 % ok. A bot launches from the trigger edge (about 26 cm north of the pad centre); the long arc amplifies that to about 110 cm on the loft against the link end (-300, -996), which `missed` counts as not arrived (the scripted client lands at (-377, -883) and passes the run). Untaken in both sets: Ammo_Bullets in the Crypt (-2500, -1700); FFA3 also Health_Medium H25_S (1000, -1600).

Loft kill share (killer's last 1 Hz sample on z >= 600): FFA3 3 of 33 kills (9.1 %), FFA6 2 of 103 (1.9 %); bots spend 6.3-6.6 % of their samples on the loft. See loft-share.txt.

Layout iteration (the one allowed): the solid masses between aisles and chapels had meshed tops under the flush Ceil_* clips (29 lowclear polys, 3 clusters). Masses are now hollow 100 cm skins without floor inside, plus three ceiling clips over the 1200 cm wall ends.

## B1b (after H7, 2026-11-13) - `b1b/`

Fresh build of origin/m7-maps with H7, no layout change. Raw: /mnt/oldroot/velox-scratch/m7-sacrarium-b1b/. Host load1 10-20 throughout.

| Set | Result | File |
|---|---|---|
| FFA x10 at 3 bots | invariants PASS except item_coverage 37/38 (Ammo_Bullets Crypt); JumpPad 100 % (163/163), Drop 91.9 %, stuck 5, dwell 3; per-run budget>300 only | b1b/ffa3-set.txt |
| FFA x10 at 6 bots | FAIL max_dwell 7 (3 runs >= 6), nav_build 2.381 s (load); JumpPad 98.4 % (242/246), stuck 3, coverage 38/38 | b1b/ffa6-set.txt |
| FFA x10 at 6 bots, repeat | FAIL max_dwell 9 (3 runs >= 6); nav 1.93 PASS; JumpPad 98.8 % (242/245), stuck 2, coverage 38/38 | b1b/ffa6b-set.txt |
| BotRun x3 | behaviour PASS (stuck 1, dwell <= 4, majors 4/4, JumpPad 37/37); cost FAIL (host load, report-only band) | b1b/botrun-set.txt |

Dwell >= 6 rows (class other) sit on the aisle side of the atrium ring wall within 38 cm of it; see the B1b notes in docs/tasks/m7-maps/17-b1-sacrarium-build.md.

## B1c (after H10, 2026-11-13) - `b1c/`

H10 fix (unique bot PlayerIds) + `BulletsAmmo` moved to (-1600,-1100). Raw: /mnt/oldroot/velox-scratch/m7-h10/. Host load1 9-33.

| Set | Result | File |
|---|---|---|
| FFA x10 at 6 bots | all invariants PASS: max_dwell 5 (B1b 7/9), stuck 1, Drop 92.4 %, JumpPad 98.9 %, coverage 38/38, nav 1.49 s; kills 230 (B1b 101) | b1c/ffa6-set.txt |
| FFA x10 at 3 bots | FAIL Drop 89.3 % (loft drops overshoot), else PASS (dwell 4, stuck 5, coverage 38/38) | b1c/ffa3-set.txt |
| FFA x10 at 3 bots, repeat | all invariants PASS (Drop 92.9 %, dwell 4, stuck 5, coverage 38/38) | b1c/ffa3b-set.txt |
| BotRun x3 | r1 PASS; r2/r3 INVALID (host frame deficit); behaviour clean | b1c/botrun-set.txt |
| DM1 BotRun x3 | set PASS (stuck 1/1/0, dwell 3/3/3) | b1c/dm1-botrun-set.txt |
| navlint / fts | PASS / OK 15/15 | b1c/navlint.txt, b1c/fts.txt |
| SacrariumRun | FAIL `loft (pad Quad)` (also on the B1 umap in a clean run: script knife edge) | b1c/sacrariumrun.txt, b1c/sacrariumrun-base-umap.txt |

## M7 H15 (fight dwell rule + under-goal drop)
`ffa6-set-h15.txt` (FFA x10 at 6, H15 final build): **SET PASS**, max_dwell 4 (H10 5), dwell>=6 runs 0, stuck 3, coverage 38/38,
kills 235 (H10 230). The first H15 build (dwell rule only) failed here with 7 s (a Fight -> Item -> Fight flicker reset the window)
and two 6 s item stalls under Armor_Yellow (1000,0,320); both fixed, see docs/tasks/m7-maps/37-h15-open-fight-standoff.md.
