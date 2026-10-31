# 05 — DM1 layout table and blockout map (`L_DM1_Blockout`)

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** done (2026-10-31, branch m5-map-characters)
**Depends on:** 02 (library), 03 (lint), ADR 0006 §2–§3 and **E0.2, E0.3 (e), E0.5**; the final lint gate runs with 03b's lint (edge-strip kind) — start in parallel, re-run step 3 after 03b merges

## Goal
The first real map exists as a greybox generated from one declarative layout table: five rooms (A Mega Hall, B RA Yard, C North Lane, D South Hall, E Rail Tower), three levels, three pads, two rocket-jump shortcuts, all items of ADR 0006 §3, 8 spawns, nav bounds and links, KillZ and clip ceiling. It passes the nav lint with zero clusters and every item is reachable from every spawn.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/__init__.py`, `Tools/editor/maps/dm1_layout.py` (new), `Tools/editor/create_dm1_map.py` (new; `--mode blockout` only in this task, `--mode art` raises "task 15"), `Tools/editor/README.md`
- `Content/Maps/DM1/L_DM1_Blockout.umap` (new, LFS), `Content/Maps/Tests/FT_DM1_Reachability.umap` + its C++ test actor if `AFunctionalTest` subclass is needed (`Source/Velox/Tests/`), `infra/ci/fts.sh` (add the FT)
- `Source/Velox/Items/VeloxItemDefinition.h/.cpp`, `Source/Velox/Items/VeloxPickup.cpp`, `Tools/editor/create_item_content.py` (the `DisabledInModes` gating for the Quad), `Source/Velox/Tests/VeloxItemTests.cpp` (`Velox.Items.ModeGating`)
- `Config/DefaultGame.ini` (`+MapsToCook` for the blockout)

## Relevant ADRs / references
- ADR 0006 §3 (room table, flow rules, item list, spawns, gating), §2 (grid, module, clip-first, `Layout:` tags), §6 (lint gate), §7 (FT)
- ADR 0006 **E0.2** (gate counts `walkin` and `isolated` clusters and `inside=1` lowclear polys; remedies per kind; `NavLintIgnore` forbidden on DM1; `fill_nav_kinds` contract; `--grid 25` report-only run), **E0.3 (e)** (lowest floor top at Z = 0), **E0.5** (theme concrete and steel bunker — dressing only; display name `Casemate`; Quad FFA only)
- ADR 0005 §4 and `Tools/editor/README.md` (nav link conventions), D41 (seal or nav), task `m4-bots/07f` (KillZ −1000, clip ceiling)
- ADR 0004 §1 (item definitions and respawn times)

## Acceptance criteria
- [ ] `dm1_layout.py` is data only (namedtuples/dataclasses + lists; no `unreal` import), starts with `MAP_ID = "DM1"`, `DISPLAY_NAME = "Casemate"` (ADR 0006 E0.5, display name changed to "Praesidium" by the human; shown by a `text_label` next to the intermission camera, no UI binding yet) and `FLOOR_Z = 0` (E0.3 e: level 0 is the lowest walkable floor top, `KillZ −1000`), and encodes every §3 rule as a record: rooms with floor/ceiling heights, boxes, ramps (15°), stairs (20 cm risers), doors (240–400 cm), three pads with target points, items with definition paths and `disabled_in_modes`, 8 spawns with yaw, links (`JUMP_PAD` × 3, `ROCKET_JUMP` × 2, `DROP` at every free edge, `JUMP`/`JUMP_FAST` where gaps exist), lights (placeholders for task 15; ignored in blockout mode), clip ceiling height per room.
- [ ] `create_dm1_map.py --mode blockout` regenerates `L_DM1_Blockout` idempotently, asserts counts, sets `KillZ −1000`, one `NavMeshBoundsVolume`, perimeter clip, tags every actor `Layout:<id>`.
- [ ] Geometry rules hold and are asserted by the script itself at generation time: walkable widths ≥ 120 cm, ledges ≥ 120 cm deep, doors 240–400 cm, pad landing zones ≥ 300 × 300, corridor ceilings ≥ 320, room ceilings ≥ 480, atria 800, RJ surfaces ≥ 900 cm clearance, spawns ≥ 10 m apart, each spawn ≤ 8 m from a weapon, Mega–RA upper-ring path 45–50 m (measured with `velox.bots.DumpNav` and recorded in the notes).
- [ ] `infra/ci/navlint.sh /Game/Maps/DM1/L_DM1_Blockout` → `clusters=0` (walk-in **and** isolated; `edge` strips are not counted once 03b has landed), `lowclear polys=0` (incl. `inside=1`). **No actor on the map carries `NavLintIgnore`** (E0.2 b); findings are fixed in the layout table by kind: `walkin` → fill/seal/widen ≥ 120 cm, `isolated` → clip to the ceiling, slope > 45.5°, or widen + `DROP` links. `create_dm1_map.py` calls `vm.configure(fill_nav_kinds=<every solid kind except ramp and bridge>)` (E0.2 d); ramps over voids are sealed with pawn-blocking fill boxes (as `Ramp_Fill_*` on `L_ItemTest`) or keep ≥ 160 cm clearance. Additionally one `navlint.sh … --grid 25 --report-only` run, pasted into the notes (E0.2 a; not a gate).
- [ ] `velox.bots.DumpPadLandings` on the map: every pad landing within 60 cm of its `JUMP_PAD` link end.
- [ ] `FT_DM1_Reachability`: `FindPath` (Full filter) succeeds from every `PlayerStart` to every pickup and between all major items (Mega, RA, YA × 2, RG); in `fts.sh`, green.
- [ ] Quad gating: `UVeloxItemDefinition::DisabledInModes`, server-side removal in `AVeloxPickup::BeginPlay` (verify the client never sees the actor; otherwise hide + disable and say so), unit test `Velox.Items.ModeGating`; headless check: `L_DM1_Blockout?game=Duel -server` logs no Quad in the item registry, `?game=FFA` logs one.
- [ ] Standalone load clean: `UnrealEditor-Cmd "$VELOX_PROJECT" /Game/Maps/DM1/L_DM1_Blockout -game -nullrhi …` reaches `Bringing World … up for play` with no `LogVelox*` warnings about links or spawns; `VeloxNav built … seconds=` ≤ 2.0.
- [ ] Build green, `fts.sh` green (now 8/8).

## Verification steps
1. `infra/ci/build.sh`.
2. Generate: `UnrealEditor-Cmd "$VELOX_PROJECT" -run=pythonscript -script="$PWD/Tools/editor/create_dm1_map.py" -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -- --mode blockout` (check how the existing scripts receive arguments; use an env var if `--` is not passed through).
3. `infra/ci/navlint.sh /Game/Maps/DM1/L_DM1_Blockout` → exit 0; then `… --grid 25 --report-only` → pasted into the notes.
4. `UnrealEditor-Cmd "$VELOX_PROJECT" /Game/Maps/DM1/L_DM1_Blockout -server -nullrhi -ExecCmds="velox.bots.DumpPadLandings, velox.bots.DumpNav <Mega xyz> <RA xyz> Full" …` → path length in the notes.
5. `infra/ci/fts.sh` → `OK 8/8`.
6. `du -sh Content/Maps/DM1` and `df -h /` in the report.

## Report back
Standard format plus the counts line, the lint summary, the Mega–RA path length, and a top-down ASCII sketch of the layout with coordinates of the five rooms.

## Notes / outcome
**Done.** Display name is **Praesidium** (human's choice, overrides the E0.5 proposal "Casemate"; one constant `DISPLAY_NAME`).
- Map: `Content/Maps/DM1/L_DM1_Blockout.umap` (448 KB). Counts: bridge=4 bridgewall=4 camera=1 clip=12 floor=1 jumppad=3 lintel=9 navbounds=1 navlink=22 (3 JUMP_PAD, 2 ROCKET_JUMP, 17 DROP) pickup=35 platform=13+ playerstart=8 ramp=1 step=64 target=3 targetpoint=3 text=1 wall=43 light=2 (asserted exactly by the script from the table).
- Footprint 70 x 38 m (not 64 x 44): X -3500..3500, Y -2300..1500; C and D are 68 m lanes; A/B 18 x 14 m atria; E 12 x 14 m. Rooms/doors/ceilings in `maps/dm1_layout.py` (sketch in its docstring). No `JUMP`/`JUMP_FAST` links: the layout has no gaps. Upper ring uses solid 4 m walkways (z 320) between the ledges and E, so there are no free bridge edges.
- navlint `/Game/Maps/DM1/L_DM1_Blockout` (Grid 50): `clusters=0 (walkin=0 isolated=0) lowclear=0 result=PASS`, walkable 2398 m2, covered 99.96 %. No `NavLintIgnore` anywhere. Findings fixed on the way: wall tops of 200 cm walls were meshed (E walls now 100 cm, E room 12 x 14 m); the stair foot beside a wall produced walk-in strips (first 20 cm step dropped, foot step lengthened by 100 cm).
- `--grid 25 --report-only`: `walkable_m2=2210.94 covered_pct=99.86 clusters=1 (walkin=1)`: one edge strip of 21 cells (500 x 50 cm) along the south lip of `C_LGLedge` (centre -50,1225). Not a gate.
- `velox.bots.DumpPadLandings`: PadA/PadB/PadD landings 0.2 cm from their link ends (also simulated in Python at generation time). Nav build 0.66-1.2 s.
- Mega to RA (`DumpNav -2050 660 330 2050 -660 330 Full`): length 4567 cm = 45.7 m, 7 points, no links (walk-only upper ring).
- Quad gating: `DisabledInModes` (tags `Velox.Mode.*`; the ADR's `GameMode.Duel` does not exist, the real mode tag is `Velox.Mode.Duel`), server-side Destroy in `AVeloxPickup::BeginPlay` (log `VeloxPickupDisabled`). `?game=Duel -server`: 34 pickups, no Quad; `?game=FFA`: 35 with the Quad. Client side not tested with a real client: a startup actor destroyed on the server is destroyed on joining clients by the engine (UNetDriver destroyed-startup list); if the soaks show a stale client copy, hide + disable instead. `Velox.Items.ModeGating` green (Items suite 8/8).
- FT: no separate map; console FT `velox.bots.FtDm1Reachability` (FT_DM1_Reachability) on `L_DM1_Blockout`, registered in `fts.sh` as `FtDm1Reachability`: 8 starts x 35 pickups and all major-item pairs, 5 checks, PASS. `fts.sh` OK 8/8; `build.sh` (Editor + Game, tests) CI OK, 108 tests 0 not successful. Standalone `-game` load: reaches "Bringing World ... up for play", no LogVelox warnings.
- `MapsToCook` now lists the blockout (`Velox.Maps.LightingBuilt` exempts `_Blockout`).
