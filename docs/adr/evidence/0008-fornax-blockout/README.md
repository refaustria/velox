# Fornax blockout evidence (M7 B4, ADR 0008 5.3)

Engine 5.8.4, base origin/m7-maps (H2-H11). Raw logs: /mnt/oldroot/velox-scratch/m7-fornax/ (s1 = first sets, invalid: a missing
floor strip; s2 = first valid sets; s3 = after the one layout iteration; c* = navlint/FT/FornaxRun runs; chute1.log = chute probe).
Host load1 about 1-2 during s2/s3 (quiet); many cost windows were still classed slow (report-only map, cost not judged).

| 5.3 | Result | File |
|---|---|---|
| (a) validate-only + golden | PASS (110 solids, 30 items, 5 spawns, 19 links) | Tools/editor/maps/golden/fornax.json |
| (b) navlint | PASS clusters=0 lowclear=0 hazard_m2=96, nav build 1.1-1.35 s, no `link_span` warning | navlint.txt |
| (c) FtMapReachability (WalkExempt Mega, MinMajors=4) | PASS checks=7 | fts-map-reachability.txt |
| (d) FornaxRun 100 ms / 2 % | PASS 8/8 checkpoints, both pads live, corrections 0 | fornaxrun.txt |
| (e) BotDuel x3 | PASS 3/3 (stuck 0, dwell <= 3, majors 3/3) | botduel-set.txt |
| (e) BotRun x3 | FAIL 0/3 only on 5 world kills of `Player` per round: BotWatch.json parks the idle client at (-2700,-1700,100), outside Fornax's footprint (X >= -2400), so it falls to KillZ (same artefact as Nemus). Bots: stuck 0, dwell <= 4, majors 3/3 | botrun-set.txt |
| (e) FFA x10 at 3 | FAIL: link_success[Jump] 88.7 % (102/115) and item_coverage 27/30; every other invariant PASS (world_kill 0, stuck 1, dwell 5, Drop 96.0 %, JumpPad 98.6 %, nav 1.19 s) | ffa3-set.txt |
| hazard gate | PASS: hazard_death_self 1 over FFA x10 (<= 6); per BotDuel round [0, 0, 1] (<= 1). Credited: FFA 3, BotDuel 1, BotRun 0 | links-hazard.txt |
| RA / Mega every round | PASS in every set (FFA 50 RA / 40 Mega takes, BotDuel 17 / 12, BotRun 15 / 12) | links-hazard.txt |
| (f) evidence | this directory | |

Per link (s3, FFA + BotDuel + BotRun, ok/(ok+failed)): PadLink_Bellows 74/74, PadLink_Tap 24/25; JUMP Isle_In 71/89 (79.8 %),
Isle_Out 64/64, T1a_N 7/7, T1a_S 15/15, T1b_S 10/11, gutter hops 5/7 (rarely used); DROP Drop_Bridge_W 74/76, Drop_Gal_C1 92/92,
Drop_Landing_E 21/22, Drop_Gal_C2 2/6. Lava entries (FFA) 23, 27 damage ticks; by piece Pool_W 10, Pool_E 6, Pool_N 4, T1 3.

Isle_In (east floor -> RA island, 250 cm of lava, 300 cm island) is the Jump failure: the H11 take-off happens at the ledge at the
bot's running speed (about 950 cm/s, `VeloxBotTakeoff past_start=157 speed=958 need=286`), so the hop carries about 600 cm and
overshoots the island into Pool_W. Moving the link start cannot change that (the take-off is ledge-triggered); a bot-side speed cap
on short-landing gap hops, or dropping the link (bots walk the slag bridge), is the lead's call.

Item coverage: the untaken items are low-value minors on side routes and change between sets (s2: Health_Small on the Tap ramp;
s3: two Armor_Shard on the north stairs and one Health_Small on the Tap ramp; s1: a Slag Run Health_Small).

Chute (30 deg) hop behaviour, review F5 (chute-hops.txt): a Hard bot sent up and down the Chute 12 times traversed it every time
(no stuck, no lava); the hop landing guard refused 45 hop ticks on the Chute (17 elsewhere), each with the predicted landing at
the take-off height (`landing ... Z` = `at ... Z`), i.e. bots walk the Chute instead of hopping it. In the sets bots were airborne
on the Chute in 0 of 5 (Hard) and 2 of 34 (Medium) 1 Hz samples versus 41-48 % on flat floor.

Layout changes against S4/S4b (fornax_layout.py):
- navlint: Furnace, Mass, Tunnel_Roof and Wall_N_F rise through their flush Ceil_* clip (top + 100) - zero-headroom polys on their
  tops (lowclear 12); rising above the nav bounds instead left nav inside the solids. Wall_N_F ends at X 200 and Wall_C_L2 runs to
  Y 1800 (a 1 m pocket on the wall top). Trench T2 starts at X 900 (the landing face) instead of 1000 (a 1 m nook, walk-in cluster).
  The Chute is 200 wide (X -700..-500, concept 300) beside a 100 cm solid cheek: against the Hearth west wall the 30 deg slab left a
  nav strip 40-100 cm above the floor at the foot (walk-in cluster at (-750, 450)). Hearth floor slabs re-cut (one slab under the
  Chute foot).
- The one iteration (after s2): Drop_Gal_B removed (gallery -> 300 cm mid strip under the bridge, 9/25 ok in s2; bots land
  beside the strip's nav); no other gallery edge into the Casting Hall meets E3 (d). Drop 88.7 % -> 96.0 %.
- s1 is invalid: a comment swallowed the X 900..1000 floor strip (Ground_5b/5c), 10 bot world kills there; fixed before s2.

## B4b (task M7 H14, take-off speed / direction control on short gap landings)
`h14-isle-in.txt` (Isle_In before/after: 47/55 -> 57/61), `ffa3-set-b4b.txt` (FFA x10 at 3: Jump 92.4 %, hazard gate PASS; FAIL
max_dwell 6 in one run, item coverage 28/30), `botduel-set-b4b.txt` (BotDuel x3 behaviour + cost PASS), `h14-gapft-isle.txt`
(FT_BotGapJump island lane, A/B). Details in docs/tasks/m7-maps/34-h14-short-gap-takeoff-speed.md.

## B4c (task M7 H15, fight dwell rule)
`ffa3-set-h15.txt` (FFA x10 at 3, H15 final build): max_dwell 3 (B4b 6), hazard gate PASS (self 0), stuck 0, kills 157 (B4b 155);
FAIL item_coverage 26/30 (Health_Small only, minors as in B4/B4b; no under_goal drop on this map); per-run FAILs budget>300 (host).
Dwell classes: ../0008-orbita-blockout/h15-dwell-classes.txt. Details in docs/tasks/m7-maps/37-h15-open-fight-standoff.md.
