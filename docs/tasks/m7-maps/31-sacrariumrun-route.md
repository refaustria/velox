# 31 — SacrariumRun move-script route fix

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** H10/B1c (merged)
**Wave:** blockout follow-up (lead-added 2026-11-13)

## Goal
`SacrariumRun` (Content/Data/MoveScripts/SacrariumRun.json) misses the Quad pad checkpoint ~1 in 2 runs (8/9 checkpoints) because the script's fixed walking direction passes the pad by ~20 cm (H10 notes, docs/tasks/m7-maps/29-h10-wall-stall-dwell.md). Fix the route in the script only (no map change) so the run passes reliably.

## Scope
- `Content/Data/MoveScripts/SacrariumRun.json`, `Tools/editor/maps/sacrarium_layout.py`, `Content/Maps/Sacrarium/L_Sacrarium_Blockout.umap`, golden, this file, evidence under `docs/adr/evidence/0008-sacrarium-blockout/`

Also (lead, from H15): bot paths to Armor_Yellow (1000,0,320) end on the floor under its pedestal instead of on it (H15 added an under-goal skip as a workaround). Find why (nav on the pedestal top, item projection, pickup Z) and fix it in the layout so the item is reachable on its pedestal.

## Acceptance criteria
- [ ] Armor_Yellow: bot paths end on the pedestal; `result=under_goal` count 0 in one Sacrarium FFA ×10 at 6 bots; navlint + fts row green
- [ ] `infra/ci/nettest.sh --scripts SacrariumRun` at 100 ms / 2 % loss: 9/9 checkpoints, strays 0, in 5 of 5 consecutive runs

## Notes / outcome
(Filled by the agent.)
