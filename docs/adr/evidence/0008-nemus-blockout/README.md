# Nemus blockout evidence (M7 B2, ADR 0008 5.3)

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
