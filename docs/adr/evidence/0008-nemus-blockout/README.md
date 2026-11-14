# Nemus blockout evidence (M7 B2 / B2b, ADR 0008 5.3)

## B2b (task 36, 2026-11-14): layout fix, item moves, re-soak
Engine 5.8.4, origin/m7-maps incl. H9 / H11 / H12; raw logs /mnt/oldroot/velox-scratch/m7-nemus-b2b/ (set1/ = first full set on the first layout). The B2 evidence is kept in b2/.
Layout (one iteration after the first full set): Hump3 moved 200 cm east (crest 770..890; the 6 B2 position probes had not removed the lowclear poly), H5_Hump3 with it; never-taken items moved onto bot routes (concept intent kept: same 41 pickups and types): Ammo_Shells (1500,1450), H25_Via (0,2000), Ammo_Lightning (0,1700), the four aqueduct-deck smalls to (-600,1400), (-200,1400), (-250,-1250), (250,-1250) (the east deck is only reachable over Pons N).

| Check | Result | File |
|---|---|---|
| navlint | PASS clusters 0, lowclear 0, nav build 1.17 s (1.22-1.26 s in FFA) | navlint.txt |
| FtMapReachability Nemus | PASS checks 7 | fts-map-reachability.txt |
| NemusRun 100 ms / 2 % | PASS, 4 pads | nemusrun.txt |
| FFA x10 at 3 (first set) | coverage 38/41 (untaken Shells, H25_Temple, H25_Root); JUMP 95.7 % (44/46), Drop 100 %, JumpPad 99.4 %, sum_stuck 1, dwell 4, nav 1.22 s | ffa3-set.txt |
| FFA x10 at 3 (repeat, same build) | coverage 40/41 (untaken Shells); JUMP 100 % (37/37), Drop 96.9 %, JumpPad 98.8 %, sum_stuck 1, dwell 5, nav 1.22 s | ffa3-repeat-set.txt |
| FFA x10 at 6 | coverage 39/41 (untaken Shard_Log1, H25_Root); JUMP 100 % (49/49), Drop 100 %, JumpPad 99.6 %, sum_stuck 3, dwell 4, nav 1.26 s | ffa6-set.txt |
| BotRun x3 | behaviour PASS (stuck 0, majors 4/4, world_kills 0/0/0, JUMP 8/8, Drop 5/5), cost NOT JUDGED (host load) | botrun-set.txt |

Every map-independent invariant is PASS in the three FFA sets except item coverage in the first 3-bot set (38 < 39). The set verdict line still reads FAIL because ffa.sh judges coverage against "all 41" and counts the per-run `allunreach_air` / `budget>300` flags (a bot in the air with no reachable goal after a pad launch; cost windows under host load) as per-run failures; neither is a set invariant. The first layout (set1/, before the item moves): 3 bots 36/41, 6 bots 41/41 with Drop 88.2 % (6 misses on three Drop links, none in the later 6-bot set) and one 9 s dwell (beside the Pons S east end, progress stall).

corona-share.txt is from B2 (the Corona is unchanged).

## B2 (original, files in b2/)

Engine 5.8.4, merged with origin/m7-maps incl. H7. Raw logs: /mnt/oldroot/velox-scratch/m7-nemus/ (s1 = FFA x10 at 3, s3 = FFA x10 at 6 and BotRun x3, run9 = NemusRun, it* = navlint probes).

| 5.3 | Result | File |
|---|---|---|
| (a) validate-only + golden | PASS (133 solids, 41 items, 11 spawns, 34 links) | Tools/editor/maps/golden/nemus.json |
| (b) navlint | FAIL by one poly: clusters=0, lowclear=1 (Hump3_Dn, `inside=1` nav poly 30-50 cm past the crest edge) | navlint.txt |
| (c) FtMapReachability (WalkExempt Railgun) | PASS checks=7 | fts-map-reachability.txt |
| (d) NemusRun 100 ms / 2 % | PASS, 4 pads (P1 P2 P3 P5), 5/5 checkpoints incl. Radix Magna foot | nemusrun.txt |
| (e) FFA x10 at 3 | FAIL: JUMP 0/33 (+aborted), sum_stuck 33 (all `link_timeout` on the two JUMP links), coverage 28/41, nav_build 3.0 s (host load; 1.4-1.9 s on quieter runs); JumpPad 99.0 % (198/200), Drop ok | ffa3-set.txt |
| (e) FFA x10 at 6 | FAIL: JUMP 2/47, sum_stuck 49, dwell 6 s (2 runs), coverage 32/41, nav_build 3.2 s; JumpPad 99.1 % (339/342) | ffa6-set.txt |
| (e) BotRun x3 | FAIL: JUMP 0/11, stuck 11, cost windows (load1 about 10); majors 4/4 | botrun-set.txt |
| (f) evidence | this directory | |

Per pad (FFA3 + FFA6, ok/(ok+failed), aborted not counted): JumpPad overall 537/545 = 98.5 %; every pad above 90 %. The two JUMP links (400 cm gaps, Jump_TerraceLog and Jump_LogBreak) fail for the bots in every attempt (they fall 40-80 cm short of the far edge or time out); a probe with both gaps narrowed to 300 cm failed the same way (0/29), so the layout was kept at the concept's 400 and the failure is bot-side (dev-hard). JUMP_FAST (JumpFast_Gap): no attempt in 20 runs, not measurable, kept.

Corona (RG crown, concept's biggest risk): no kill with the killer on the Corona in FFA3 (0 of 53) or FFA6 (0 of 111); bots spend 0.2 % of their samples there, longest stay 2.0 s (inside the DM1 camp band). See corona-share.txt. BotRun `world_kills=5` per round is an artefact: BotWatch.json teleports the idle human to (-2700, -1700, 100), which is inside the Templum on this map.

Untaken items FFA6: Ammo_Shells (2700, 1450), Health_Medium (2400, 2350), Health_Small x4 on the east aqueduct deck, Armor_Shard x3 on the west log (behind the failing terrace JUMP link). Nav build 1.39-1.43 s in navlint, 1.9-3.2 s under FFA load.

Layout iterations (nemus_layout.py): Ceil caps over the tree wall, gate wings, column stubs, plugs over the trunk tops; nav volume capped at Z 1900; Rivus hump crests (120 cm flat between the 15 degree slabs, Hump1 moved 60 cm west, Hump3 30 cm east, two drops shifted); Radix Australis 15 degrees (a 20 degree foot left a crawl-space nav hole) and 350 wide; three 300 x 200 rim notches (top 280) joining the tier-3 ring to the forest (the four tier-3 shards were a nav island); pad P1 moved to the Cavea west rim (-1200, 0) because `VeloxNav link_span` showed the pit pad's link spanning three 1000 cm tiles (never connected); the last lowclear poly was probed with six hump position variants without removing it.
