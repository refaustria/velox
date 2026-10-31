# 04j — Seal the slot north of the YA ramp, move the D17 slot cases to a dedicated test room

**Milestone:** M4 Bots
**Agent:** dev
**Status:** done
**Depends on:** ADR 0005 D46 item 2 (source), D17 (off-mesh recovery), D41 (content rule), D43/D44/D45 (the slot analyses, the FT cases), 04i (the ramp fill, `FILL_NAV_KINDS`)

## Goal
No bot can stand in a no-mesh pocket on `L_ItemTest` that is "on-mesh" by the shared extent but unreachable along its segment (D46 item 2: Rapidus, Live t 48-58, strikes at X -420..-396 against the ramp's 48 cm face). The 100 cm slot between the YA ramp's north face (Y 1400) and the corridor wall (Y 1500) is filled flush with the ramp and platform surface from X -432 to 572.7, and the D45 `slot_centre` / `slot_tangent` cases (which used that slot as the D17 bed) run in a dedicated sealed test room.

## Scope - files/modules the agent may touch
- `Tools/editor/create_item_test_map.py` - `build_ramp()` (north fill), `build_d17_room()`, the corridor-wall cap, expected counts, docstring.
- `Content/Maps/L_ItemTest.umap` (LFS) - regenerated headless (04e's command).
- `Source/Velox/Tests/VeloxBotFunctionalTest.cpp` - FtOffMesh slot-case coordinates and the `SlotExitMaxY` constant only.
- this task file and `docs/tasks/m4-bots/README.md`.
(No other C++, no tunable values.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` D46 item 2 (rule, evidence, acceptance), D41, D44, D45.

## Acceptance criteria
- [x] `infra/ci/build.sh` CI OK.
- [x] `infra/ci/fts.sh` 3x `[fts] OK 7/7`; `slot_centre` and `slot_tangent` pass in the new room.
- [x] DumpNav probes: the strip north of the ramp/platform (Y 1425, X -430..513) is meshed flush (path ok over the whole stretch; no void, no 48-61 cm face).
- [x] One `BotDuel` round under the lock: no strike and no dwell with Y 1400-1500. (Budget verdict not judged here.)

## Verification steps
1. Regenerate: `UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_item_test_map.py -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput`.
2. `infra/ci/build.sh`; `infra/ci/fts.sh --port N` three times; `velox.bots.DumpNav` probes; `infra/ci/nettest.sh --port N --scripts BotDuel`.

## Report back
Standard dev format.

## Notes / outcome
**Done (dev, 2026-10-30, on 8b75f9c).**
- *North fill (D46 geometry).* `Ramp_NorthSlotSlab` (kind `ramp`, material `RampWalkable`): a second thin slab in the ramp's plane, Y 1400..1500, from X -432 (surface 45.0 cm, the step height) to the ramp top at X 172.7; `Ramp_NorthFill_1..4` (kind `platform`, Y 1400..1500) under it with the 04i tops (24 for X -432..-400, then 33 / 86 / 140); `Ramp_NorthTopPlatform` (X 172.7..572.7, Y 1400..1500, Z 0..207). West of X -432 the slot floor is unchanged (a <= 45 cm step onto the ramp).
- *Finding the ADR did not list: the wall top.* With the slot flush, the platform surface (207) is 43 cm under the corridor wall top (250): inside the step height, so Recast meshed a sliver on top of the wall (DumpNav at (300, 1450) projected to Z 233-253) and a player could step onto it. Added `Corridor_WallCap_RampTop` (kind `corridorwall`, X -432..572.7, Y 1500..1600, Z 250..265): 58 cm above the surface. After it the projection at Y 1450 stays on the surface (Z 193-209) and nothing is meshed on the wall.
- *D17 room* (`build_d17_room()`, folder `D17Room`, own `NavMeshBounds_D17Room`): inner floor X 3800..5600 / Y 600..1700, 250 high walls, no door, no link, 1700 cm east of the arena wall. Two 100 x 1200 cm slots (X 4100..5300): slot A (Y 1600..1700, the room's north wall on its north side; on its south side a 207 cm block with a 48 cm step block, walkable top, along its west 200 cm) and slot B (Y 600..700, the room's south wall; a 207 cm block north). A 300 cm lane (Y 1000..1300) joins the two meshed end zones (X 3800..4100, 5300..5600) so the goal is reachable from either exit. Slot floors carry no mesh (erosion), `start_off_mesh` confirmed by DumpNav.
- *FtOffMesh.* `slot_centre`, `slot_tangent` start (4700, 1662.5), goal the lane centre (4700, 1150); `SlotExitMaxY` 1450 -> 1650 (slot A's centre line). Both: attempt 1, no strike, `no_clear_point` 0, exit target (4100, 1634) (600 cm along X), one `target=tangent`, goal reached 3.8 s. `ramp_strip_wall` had to move too (its start (364, 1462) is now inside the fill): slot B, start (4700, 637.5), goal the lane; recovered 2.38 s, goal 3.7 s. Cases `ra_strip`, `mega_strip`, `cross_top`, `under_slab` unchanged.
- *Counts.* platform 7 -> 15, ramp 1 -> 2, corridorwall 1 -> 2, navbounds 1 -> 2, new `roomfloor` 1, `roomwall` 4. Pickups 23, links 15 unchanged.
- *Verification.* build.sh `96 tests, 0 not successful` / CI OK; `fts.sh` x3 `[fts] OK 7/7`; DumpNav (Walk): (-420, 1450) -> (-300, 1450) ok 2 points, (-300, 1450) -> (560, 1450) ok through (170, 1425, 170) to Z 210, west face (-500 -> -440) ok; `nettest.sh --port 7951 --scripts BotDuel`: `NET SOAK OK`, CONTENDED (budget not judged), stuck=1 `progress` at Celer (-50, 1863) (corridor, not the slot), max_dwell 3 s at (822, 754), offmesh recovered 0, no_nav_point 0, YA taken 5.
- *Open.* The D36 ten-run FFA set (D46 acceptance) is not part of this task. Poly heights on the flat platform top vary 193-212 across the mesh (detail-mesh interpolation, seen before the change at Y 1250 too): cosmetic, not a defect.
