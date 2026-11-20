# 31 — SacrariumRun move-script route fix

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (branch m7-31-sacrarium-route)
**Depends on:** H10/B1c (merged)
**Wave:** blockout follow-up (lead-added 2026-11-13)

## Goal
`SacrariumRun` (Content/Data/MoveScripts/SacrariumRun.json) misses the Quad pad checkpoint ~1 in 2 runs (8/9 checkpoints) because the script's fixed walking direction passes the pad by ~20 cm (H10 notes, docs/tasks/m7-maps/29-h10-wall-stall-dwell.md). Fix the route in the script only (no map change) so the run passes reliably.

## Scope
- `Content/Data/MoveScripts/SacrariumRun.json`, `Tools/editor/maps/sacrarium_layout.py`, `Content/Maps/Sacrarium/L_Sacrarium_Blockout.umap`, golden, this file, evidence under `docs/adr/evidence/0008-sacrarium-blockout/`

Also (lead, from H15): bot paths to Armor_Yellow (1000,0,320) end on the floor under its pedestal instead of on it (H15 added an under-goal skip as a workaround). Find why (nav on the pedestal top, item projection, pickup Z) and fix it in the layout so the item is reachable on its pedestal.

## Acceptance criteria
- [x] Armor_Yellow: bot paths end on the pedestal; `result=under_goal` count 0 in one Sacrarium FFA x10 at 6 bots; navlint + fts row green
- [x] `infra/ci/nettest.sh --scripts SacrariumRun` at 100 ms / 2 % loss: 9/9 checkpoints, strays 0, in 5 of 5 consecutive runs

## Notes / outcome
- **SacrariumRun:** row 27 yaw -156.5 -> -136.4 (aimed from the clean-run landing at the Quad pad). 5/5 rounds PASS at 100 ms / 2 % loss, 9/9 checkpoints, strays 0, on the final layout (evidence `31-sacrariumrun-x5.txt`).
- **Armor_Yellow root cause:** the nav path to YA_E is correct (ends on the deck). The under_goal drops come from bots standing on the floor within 130 cm (2-D) beneath elevated items (H15 rule). Before: 5 under_goal in a 10-set (YA_E 2, Mega 2, Railgun 1).
- **Layout fix:** solid pedestals under YA_E (`Ped_YA_E`, 220 x 220 x 300) and Mega (`Ped_Mega`, to the chapel wall so no walk-in strip remains). An RA pedestal was tried and dropped (one 8 s roam dwell, allunreach_air run).
- **Results (final layout):** FFA x10 at 6 bots SET PASS, under_goal 0, max_dwell 4, stuck 1, coverage 38/38; navlint PASS (clusters 0); fts FtMapReachability-L_Sacrarium_Blockout PASS; sacrarium golden rewritten, dm1 golden check OK.
- **Residual:** Railgun (-900,-1000,640) is the same pattern (seen 2 of 20 runs earlier, 0 in the final set); a pedestal would block Gal_S. Candidate C++ fix: do not fire IsUnderGoal while a link/long leg remains. Lead decision.
