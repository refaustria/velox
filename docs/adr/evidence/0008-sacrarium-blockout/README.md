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
