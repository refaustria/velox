# Culmen blockout evidence (M7 B3, ADR 0008 5.3)

Engine 5.8.4, base origin/m7-maps (725c438). Raw logs: /mnt/oldroot/velox-scratch/m7-culmen/ (r2-r4 = regen + navlint + FT per
layout state, d1-d4 = pad dump + CulmenRun, s1 = aborted first set (BrSE defect), s2 = first full FFA x10 at 3, s3 = final layout;
`culstat.py` there makes the per-link / rail / item tables). Host load1 about 1-6 during the sets; other agents' soaks interleaved on
the lock (sets are serial, never concurrent).

| 5.3 | Result | File |
|---|---|---|
| (a) validate-only + golden | PASS (62 solids, 46 items, 10 spawns, 42 links, 11 pads); golden written | Tools/editor/maps/golden/culmen.json |
| (b) navlint | PASS clusters=0 lowclear=0, nav build 1.09-1.20 s at `NAV_TILE_SIZE_UU` 4000 (8 tiles, 37.7 KB), no `link_span` | navlint.txt |
| (c) FtMapReachability (WalkExempt Railgun) | PASS checks=7 failed=0, links_all 42/42, pad_links 11 | fts-map-reachability.txt |
| pad landings vs `DumpPadLandings` | all 11 within 60 cm: P1/P2 0.2, P3/P4 1.8, P5/P6 1.6, P7 0.4, P8 0.0, P9 0.3, P10/P11 29.1 cm (S3's 30 cm apex deviation D4) | padlandings.txt |
| (d) CulmenRun 100 ms / 2 % | PASS 8/8 checks, 7 pads live (P7 P8 P5 P4 P6 P3 P10, no input in the flight), corrections 0 (d2, d3, d4) | culmenrun.txt |
| (e) BotRun x3 | set behaviour PASS, cost PASS (rounds 1/2 cost PASS, round 3 slow throughout, not judged); stuck 1/0/0, dwell <= 4, majors 5/5, world_kills 0 | botrun-set.txt |
| (e) FFA x10 at 3 | FAIL: max_dwell_s 34 and item_coverage 40/46; every other invariant PASS (stuck 0, Drop 100 %, JumpPad 99.0 %, nav 1.09 s) | ffa3-set.txt, ffa3-links.txt |
| (e) FFA x10 at 6 | FAIL: max_dwell_s 11; every other invariant PASS (stuck 3, Drop 100 %, JumpPad 97.9 %, coverage 46/46) | ffa6-set.txt, ffa6-links.txt |
| (e) FFA x10 at 8 (report-only) | see ffa8-set.txt | ffa8-set.txt, ffa8-links.txt |
| extra gate: Haste, both far 50 HP | Haste taken every round in every set; H50_W 1/10 rounds (3 bots) and 4/10 (6 bots), H50_E 0/10 (3 bots, s3) and 2/10 (6 bots) | ffa*-links.txt |
| (f) evidence | this directory (first-set/ = s2, before the item iteration) | |

**Dwell cause (all three >= 6 s episodes, dwell-kiosks.txt):** bots stranded on the 250 cm concourse kiosk tops: Kiosk2 (-1058,-1438)
34 s, Kiosk1 (-1976,-1764) 12 s, Kiosk4 (1937,-1482) 11 s, each after a fight jump / fall from the Viaduct (pawn z 322 = top 250 + 72),
goal none/roam (no nav on the top, no link down). S3 deviation D6 noted that a player dropping from the Viaduct can stand on them in
the blockout. Not fixed (iteration budget spent); proposed fix: kiosks out of reach from the 320 decks (height >= ~550, or a
`DROP` link off each top plus nav fill), then re-run the three FFA sets.

**Links (final layout).** Every pad link has attempts: at 6 bots P1 60/61, P2 49/49, P3 2/3, P4 16/17, P5 11/11, P6 2/2, P7 58/58,
P8 23/25, P9 90/93, P10 21/21, P11 44/44. **JUMP_FAST (6 links, G1 gaps): 0 attempts in every set** (s2, s3 at 3 / 6 / 8 bots and
BotRun): bots reach the Viaduct by P9, the bridges BrSW / BrSE and the drops; the H11 "can't build speed above ground speed" question
stays unmeasured on Culmen. **ROCKET_JUMP RJ1-RJ4: 0 attempts** (Medium bots). Drops 100 % (105/105 at 6 bots).

**Rail on the long lanes** (victim's last 1 Hz position on the Trench L0 strip or the Viaduct deck): 3 bots 10.0 % of all kills
(Trench 1/4, Viaduct 0/3); 6 bots 21.6 % of all kills, Trench 10/26 = 38.5 %, Viaduct 3/30 = 10 %.

**Kit (art plan, `--mode art --validate-only`):** 383 kit instances (Floor_400 144, Wall_400 134, Bridge_400 40, Wall_Trim 48,
Wall_Door400 6, Pad_Ring 11), 0 protrude; draw calls are measured on the Mac.

**Layout changes against S3/P6 (`culmen_layout.py`):**
- `NAV_TILE_SIZE_UU` 4000: P3-P6 and P8 spanned 2 tiles at 1000 (validator link-span rule).
- S10 (2000,-1250) sat on the Terrace E inner edge (x 2000): every path from it was partial (FT 93 failures). Moved to (2500, 900,
  z 320) yaw 180, the only Terrace E point that keeps >= 10 m to S7 and no spawn-to-RA sightline (7.3 m to SG-E).
- BrSE (bring-up defect): `_pair` point-mirrored BrSW onto the Trench (2200..2500, 1400..1900) where Drop_TE_Trench landed on it
  (link_timeout every attempt in s1). Now the X mirror (2200..2500, -1900..-1400): Terrace E south end to the Viaduct, as BrSW.
- The one item iteration after s2 (42/46: three NW roof-ramp shards and T_H5_4 never taken): NW_Shard_2..4 onto the P1 landing ->
  RA line ((-3650,600), (-3700,0), (-3550,-300), z 640); T_H5_4 (2600,1600) -> (2000,1600). s3 then took them; its 3-bot set missed
  six others (H50_E, H25_6 on Terrace E, H25_7 on the Spire deck, T_H5_1..3 in the Trench), its 6-bot set took 46/46.

Haste DA: `DA_Item_Powerup_Haste` regenerated with `create_item_content.py` (only that asset changed); `DisabledInModes` =
`Velox.Mode.Duel` (the script re-reads and asserts it).
