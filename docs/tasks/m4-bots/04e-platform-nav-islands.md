# 04e — Remove nav islands inside solid platform boxes

**Milestone:** M4 Bots
**Agent:** dev
**Status:** todo
**Depends on:** 04d (ADR 0005 D24; cause in 04d-balance-followup.md notes)

## Goal
Recast no longer generates walkable islands at floor level inside the solid Mega/RA (300 cm) and ramp-top (207 cm) platform boxes on L_ItemTest (and the same pattern on the other test maps), so airborne route evaluations never project onto them.

## Scope — files/modules the agent may touch
- `Tools/editor/create_item_test_map.py` (and the other `Tools/editor/create_*_map.py` scripts if they build solid boxes the same way): set `bFillCollisionUnderneathForNavmesh = true` on solid platform boxes (or the equivalent that removes the inner nav), regenerate the maps
- `Content/Maps/L_ItemTest.umap` (+ other regenerated maps)
- `Source/Velox/Tests/VeloxBotFunctionalTest.cpp`: optional FT/check that no nav poly exists inside the platform footprints at floor level

## Acceptance criteria
- [ ] First, before any change: build.sh + fts.sh on the m4-bots head (04c-b and 04d merged together) — report the result
- [ ] `velox.bots.DumpNav -1500 1200 10 → 0 -1000 10 Walk` no longer finds a poly at (−1500, 1200, 10) (or projects to the platform top); same for the RA side (2100, 1200, 10) and the ramp-top footprint
- [ ] Check `StartRoam` projection does not land inside platforms
- [ ] build.sh green, fts.sh OK 6/6 ×2, FtNavigation numbers not worse than in the 04b report
- [ ] FFA 3 Medium 120 s ×3 (runner `scratchpad/a04d/ffa.sh` + `summary.py`): `start=air` all-unreachable = 0, other D20 bands still pass
