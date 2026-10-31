# 05 — DM1 layout table and blockout map (`L_DM1_Blockout`)

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 02 (library), 03 (lint), ADR 0006 §2–§3

## Goal
The first real map exists as a greybox generated from one declarative layout table: five rooms (A Mega Hall, B RA Yard, C North Lane, D South Hall, E Rail Tower), three levels, three pads, two rocket-jump shortcuts, all items of ADR 0006 §3, 8 spawns, nav bounds and links, KillZ and clip ceiling. It passes the nav lint with zero clusters and every item is reachable from every spawn.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/__init__.py`, `Tools/editor/maps/dm1_layout.py` (new), `Tools/editor/create_dm1_map.py` (new; `--mode blockout` only in this task, `--mode art` raises "task 15"), `Tools/editor/README.md`
- `Content/Maps/DM1/L_DM1_Blockout.umap` (new, LFS), `Content/Maps/Tests/FT_DM1_Reachability.umap` + its C++ test actor if `AFunctionalTest` subclass is needed (`Source/Velox/Tests/`), `infra/ci/fts.sh` (add the FT)
- `Source/Velox/Items/VeloxItemDefinition.h/.cpp`, `Source/Velox/Items/VeloxPickup.cpp`, `Tools/editor/create_item_content.py` (the `DisabledInModes` gating for the Quad), `Source/Velox/Tests/VeloxItemTests.cpp` (`Velox.Items.ModeGating`)
- `Config/DefaultGame.ini` (`+MapsToCook` for the blockout)

## Relevant ADRs / references
- ADR 0006 §3 (room table, flow rules, item list, spawns, gating), §2 (grid, module, clip-first, `Layout:` tags), §6 (lint gate), §7 (FT)
- ADR 0005 §4 and `Tools/editor/README.md` (nav link conventions), D41 (seal or nav), task `m4-bots/07f` (KillZ −1000, clip ceiling)
- ADR 0004 §1 (item definitions and respawn times)

## Acceptance criteria
- [ ] `dm1_layout.py` is data only (namedtuples/dataclasses + lists; no `unreal` import) and encodes every §3 rule as a record: rooms with floor/ceiling heights, boxes, ramps (15°), stairs (20 cm risers), doors (240–400 cm), three pads with target points, items with definition paths and `disabled_in_modes`, 8 spawns with yaw, links (`JUMP_PAD` × 3, `ROCKET_JUMP` × 2, `DROP` at every free edge, `JUMP`/`JUMP_FAST` where gaps exist), lights (placeholders for task 15; ignored in blockout mode), clip ceiling height per room.
- [ ] `create_dm1_map.py --mode blockout` regenerates `L_DM1_Blockout` idempotently, asserts counts, sets `KillZ −1000`, one `NavMeshBoundsVolume`, perimeter clip, tags every actor `Layout:<id>`.
- [ ] Geometry rules hold and are asserted by the script itself at generation time: walkable widths ≥ 120 cm, ledges ≥ 120 cm deep, doors 240–400 cm, pad landing zones ≥ 300 × 300, corridor ceilings ≥ 320, room ceilings ≥ 480, atria 800, RJ surfaces ≥ 900 cm clearance, spawns ≥ 10 m apart, each spawn ≤ 8 m from a weapon, Mega–RA upper-ring path 45–50 m (measured with `velox.bots.DumpNav` and recorded in the notes).
- [ ] `infra/ci/navlint.sh /Game/Maps/DM1/L_DM1_Blockout` → `clusters=0`, `lowclear polys=0`.
- [ ] `velox.bots.DumpPadLandings` on the map: every pad landing within 60 cm of its `JUMP_PAD` link end.
- [ ] `FT_DM1_Reachability`: `FindPath` (Full filter) succeeds from every `PlayerStart` to every pickup and between all major items (Mega, RA, YA × 2, RG); in `fts.sh`, green.
- [ ] Quad gating: `UVeloxItemDefinition::DisabledInModes`, server-side removal in `AVeloxPickup::BeginPlay` (verify the client never sees the actor; otherwise hide + disable and say so), unit test `Velox.Items.ModeGating`; headless check: `L_DM1_Blockout?game=Duel -server` logs no Quad in the item registry, `?game=FFA` logs one.
- [ ] Standalone load clean: `UnrealEditor-Cmd "$VELOX_PROJECT" /Game/Maps/DM1/L_DM1_Blockout -game -nullrhi …` reaches `Bringing World … up for play` with no `LogVelox*` warnings about links or spawns; `VeloxNav built … seconds=` ≤ 2.0.
- [ ] Build green, `fts.sh` green (now 8/8).

## Verification steps
1. `infra/ci/build.sh`.
2. Generate: `UnrealEditor-Cmd "$VELOX_PROJECT" -run=pythonscript -script="$PWD/Tools/editor/create_dm1_map.py" -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -- --mode blockout` (check how the existing scripts receive arguments; use an env var if `--` is not passed through).
3. `infra/ci/navlint.sh /Game/Maps/DM1/L_DM1_Blockout` → exit 0.
4. `UnrealEditor-Cmd "$VELOX_PROJECT" /Game/Maps/DM1/L_DM1_Blockout -server -nullrhi -ExecCmds="velox.bots.DumpPadLandings, velox.bots.DumpNav <Mega xyz> <RA xyz> Full" …` → path length in the notes.
5. `infra/ci/fts.sh` → `OK 8/8`.
6. `du -sh Content/Maps/DM1` and `df -h /` in the report.

## Report back
Standard format plus the counts line, the lint summary, the Mega–RA path length, and a top-down ASCII sketch of the layout with coordinates of the five rooms.

## Notes / outcome
