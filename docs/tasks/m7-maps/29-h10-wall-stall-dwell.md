# 29 (H10) — Bot fight stall against walls (dwell) + B1c Sacrarium re-soak

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H8 (both touch Bots/*; start after H8 is merged)
**Wave:** bot track (lead-added 2026-11-13 from B1b, docs/tasks/m7-maps/17-b1-sacrarium-build.md §B1b)

## Goal
1. Fix the fight stall B1b found: in FFA ×10 at 6 bots on L_Sacrarium_Blockout, max_dwell reached 7 and 9 s (limit 5). All dwell runs ≥ 6 s are class "other"; six of seven sit within ~38 cm of the aisle side of the atrium ring wall ((444..861,1338) N aisle, (1338,-852/-1147) E aisle, (-1338,1254) W aisle; one at (2662,-1438)). The bot hops against the wall (z 72–182) while firing selfsplash/notready at an enemy it can see but cannot reach. Root-cause it (combat wish vs path, LOS-but-no-path target, strafe into wall, selfsplash guard) and fix it generally (not map-specific). Raw logs: /mnt/oldroot/velox-scratch/m7-sacrarium-b1b/.
2. B1c: move `Ammo_Bullets@(-2500,-1700)` out of the Crypt corner in `Tools/editor/maps/sacrarium_layout.py` to a spot on a normal route (never taken in two of three 3-bot sets), regenerate L_Sacrarium_Blockout, re-run navlint, its fts row and SacrariumRun, then the three B1 soak sets.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/*`, `Source/Velox/Tests/VeloxBot*` (+ an FT reproducing the wall stall if feasible), `infra/ci/fts.sh` rows
- `Tools/editor/maps/sacrarium_layout.py`, `Content/Maps/Sacrarium/L_Sacrarium_Blockout.umap`, `Tools/editor/maps/golden/sacrarium.json` (re-write golden), evidence `docs/adr/evidence/0008-sacrarium-blockout/`
- this file and the B1 task file notes

## Acceptance criteria
- [ ] Root cause stated with log evidence; fix covered by a unit test or FT
- [ ] Sacrarium: FFA ×10 at 3 bots and at 6 bots — max_dwell ≤ 5, coverage 38/38, all map-independent invariants PASS; BotRun ×3 behaviour PASS (cost windows: A/B rule)
- [ ] DM1: BotRun ×3 on L_DM1_Blockout behaviour PASS, no regression in dwell/stuck vs the H7 numbers
- [ ] build + full automation + fts.sh green; DM1 golden OK; sacrarium golden updated
- [ ] B1 task (e) ticked if the Sacrarium sets pass

## Notes / outcome
(Filled by the agent.)
