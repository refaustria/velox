# 38 — Fornax minor items onto routes + coverage re-soak

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done
**Depends on:** H15 (merged)
**Wave:** blockout follow-up (lead-added 2026-11-14)

## Goal
Fornax is done except item coverage in FFA ×10 at 3 bots: 27/30 (B4), 28/30 (B4b), 26/30 (H15). Only small minor items are missed (Health_Small, some Armor_Shard on the north stairs / Tap ramp), different ones per set. Move the never/rarely taken minor items onto positions on normal routes (concept intent kept, as B2b did for Nemus), regenerate, re-run navlint, the Fornax fts row, FornaxRun and one FFA ×10 at 3 bots + BotDuel ×3.

## Scope
- `Tools/editor/maps/fornax_layout.py`, `Content/Maps/Fornax/L_Fornax_Blockout.umap`, `Tools/editor/maps/golden/fornax.json`, evidence `docs/adr/evidence/0008-fornax-blockout/`, the B4 task file notes, this file

## Acceptance criteria
- [x] FFA ×10 at 3 bots: coverage 30/30 (or 29/30 with the one miss named and on a route), all invariants PASS, hazard gate PASS, all JUMP links ≥ 90 %
- [x] BotDuel ×3 PASS; navlint, fts row, FornaxRun green; DM1 golden OK; B4 (e) ticked if passing

## Notes / outcome
Branch `wip/m7-38-fornax-items`, engine 5.8.4 (compiled and run); raw logs /mnt/oldroot/velox-scratch/38-fornax-items/, summaries in docs/adr/evidence/0008-fornax-blockout/ (`*-m7-38*.txt`).
- Item moves (fornax_layout.py): H5_S1 (-300,-1075) and H5_S2 (0,-1300) on the Hearth-Slag door / Slag Run line; H5_T2 (1300,-800) and H5_T3 (1300,-1200) either side of Pad 2 in the Tap Room; Shard_N1/N2 (700,1050 / 700,1250, z 320) and, after the first soak, Shard_N3 (500,1200, z 320) on the West Landing along the Flue-to-Crane line. Concept intent kept: six smalls and six shards stay, on routes. golden/fornax.json unchanged by the moves (items are not in the golden); DM1 golden OK.
- First FFA x10 at 3 (first layout): 29/30, only miss Shard_N3 on the north stairs; one item iteration (N3 to the landing), regenerated.
- Final: navlint PASS (clusters 0, lowclear 0, nav 1.24 s); fts row FtMapReachability-L_Fornax_Blockout PASS; FornaxRun PASS (8/8, corrections 0; one earlier run failed 'Tap ramp up to the gallery' once on the first layout, 2 reruns passed, the final layout passed once).
- FFA x10 at 3 bots: item_coverage 30/30; left_arena/world_kill/no_nav_point/giveup/bursts 0, sum_stuck 0, max_dwell 4 s, nav 1.24 s, Drop 100 % (128/128), Jump 93.3 % (97/104), JumpPad 100 % (58/58); hazard gate PASS (self 0, credited 3, limit 6). The set line still says FAIL only for the per-run `budget>300` host-cost windows (report-only map, same as H15).
- BotDuel x3 PASS 3/3 (stuck 0, dwell <= 4), hazard gate per round credited [2,0,1] self [0,0,0] PASS.
- Port 7777 is held by production, soaks ran on 7941. Note: infra/.env created in the worktree (gitignored).
