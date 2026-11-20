# Culmen blockout evidence (M7 B3, ADR 0008 5.3)

Engine 5.8.4. Second run (2026-11-20, branch `wip/m7-19-b3-culmen`): base = earlier WIP 52e488c + merge of `m7-maps` (H14/H15 bot
changes included) + the one layout iteration **kiosks 250 -> 700** (`KIOSK_TOP_Z` in `culmen_layout.py`). Raw logs:
`/mnt/oldroot/velox-scratch/m7-culmen/run2/` (r1 = regen + navlint + FT, d1 = pad dump + CulmenRun x3, s1 = soak sets; helper scripts
and `culstat.py` there). Host load1 about 0.9-2.0 during the sets (quiet). Files of the first run (kiosks 250) are in `pre-kiosk/`
(and `first-set/` = its s2 set before the item iteration).

| 5.3 | Result | File |
|---|---|---|
| (a) validate-only + golden | PASS (62 solids, 46 items, 10 spawns, 42 links, 11 pads); golden rewritten (kiosk z1 700, kit 387) | Tools/editor/maps/golden/culmen.json |
| (b) navlint | PASS walkable 3757 m2, covered 99.98 %, clusters=0 lowclear=0, nav build 1.14-1.29 s | navlint.txt |
| (c) FtMapReachability (WalkExempt Railgun) | PASS checks=7 failed=0, links_all 42/42, pad_links 11 | fts-map-reachability.txt |
| pad landings vs `DumpPadLandings` | all 11 within 60 cm; roof pads P1/P2 0.2 cm, P3/P4 1.8, P5/P6 1.6; P10/P11 29.1 cm (S3 D4) | padlandings.txt |
| (d) CulmenRun 100 ms / 2 % | PASS 3/3 rounds, 8/8 checks each, 7 pads, C=0 (corrections 0) | culmenrun.txt |
| (e) BotRun x3 | set behaviour PASS, cost PASS; stuck 0/1/0, dwell <= 3, majors 5/5. Round 3 per-round FAIL only on `allunreach_air` 1 (D33, open B6 item) | botrun-set.txt |
| (e) FFA x10 at 3 | **FAIL item_coverage 43/46**; every other invariant PASS (stuck 0, dwell 4, Drop 100 %, JumpPad 97.5 %, nav 1.29 s) | ffa3-set.txt, ffa3-links.txt |
| (e) FFA x10 at 6 | PASS every invariant (stuck 1, dwell 4, Drop 100 %, JumpPad 97.9 %, coverage 46/46, nav 1.17 s) | ffa6-set.txt, ffa6-links.txt |
| (e) FFA x10 at 8 (report-only) | every invariant PASS (stuck 1, dwell 4, Drop 99.4 %, JumpPad 99.1 %, coverage 46/46); cost not judged | ffa8-set.txt, ffa8-links.txt |
| extra gate: Haste, both far 50 HP | Haste 10/10 rounds in every set. H50_W 2/10 (3 bots), 6/10 (6), 8/10 (8); **H50_E 0/10 (3 bots)**, 5/10 (6), 3/10 (8) | ffa*-links.txt |
| (f) evidence | this directory | |

Per-run FAIL flags in the FFA sets are `allunreach_air` (open B6 ruling) and, at 3 bots, `budget>300` in 4 runs (cost; host); neither is
a map-independent invariant.

**Kiosk iteration (dwell fixed).** First run: three >= 6 s dwells (34 / 12 / 11 s), all bots stranded on the 250 cm kiosk tops (no nav,
no link down) after a jump or fall from the 320 Viaduct (`pre-kiosk/dwell-kiosks.txt`). Jump apex is 675^2 / 4000 = 114 cm, so a 700 top
is out of reach from the decks (434) and from the flat 480 Porticus S top (594) that Kiosk2 / Kiosk4 abut. After it: max dwell 4 s in
all three FFA sets, no episode >= 6 s (`dwell.txt`).

**Remaining failure: coverage at 3 bots.** Untaken in FFA x10 at 3: `H50_E` (3600, 2000), `T_H5_1` (1400, 1600), `T_H5_3` (2200, 1600):
all in the east Trench, all health. The first run's 3-bot set missed the same area (H50_E, T_H5_1..3). Bots do pass through: Drop_TE_Trench
8/8 (landing (2300, 1650)) and P11 (3400, 1650) 17/17, so the line landing -> P11 is used, but the health sits off it (T_H5 at y 1600 west of
the landing, H50_E 350 cm north-east of P11) and three bots rarely need health. Proposed fix (an item iteration, not done: the
iteration budget went to the kiosks): T_H5 row onto the landing -> P11 line (x 2400..3200, y 1650) and H50_E nearer the P11 approach,
as task 38 did for Fornax; or a B6 ruling that minor health at 3 bots on a 46-item map is judged at 6 bots.

**Links.** JUMP_FAST (6 links) and ROCKET_JUMP RJ1-RJ4: 0 attempts in every set, as in the first run. Rail share on the Trench lane:
1/7 kills (3 bots), 11/31 = 35.5 % (6 bots).
