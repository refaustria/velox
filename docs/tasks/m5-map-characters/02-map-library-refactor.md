# 02 — `Tools/editor/veloxmap.py`: shared map-generation library

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0006 §2

## Goal
The three test-map generators share one library so the DM1 generator (task 05) builds on proven helpers instead of a fourth copy. Behaviour of the existing maps does not change: same actor counts, same transforms, same soak results.

## Scope — files/modules the agent may touch
- `Tools/editor/veloxmap.py` (new), `Tools/editor/create_movement_test_map.py`, `create_combat_test_map.py`, `create_item_test_map.py` (refactor to import the library), `Tools/editor/README.md`
- `Content/Maps/L_MovementTest.umap`, `L_CombatTest.umap`, `L_ItemTest.umap` — regenerated only if a byte-identical regeneration is impossible; otherwise do not commit them.

## Relevant ADRs / references
- ADR 0006 §2 (helper list, `Layout:<id>` tag, clip-first collision), ADR 0005 §3–§4 and `Tools/editor/README.md` (nav bounds, link conventions), task `m4-bots/07f` (KillZ, clip walls/ceiling)

## Acceptance criteria
- [ ] `veloxmap.py` exposes at least: `box(name, center, size, material, kind, collide=True, tags=())`, `ramp(...)`, `stairs(...)`, `cylinder_marker(...)`, `text_label(...)`, `nav_bounds(center, size)`, `nav_link(...)`, `jump_pad(...)`, `pickup(definition_path, location, tags)`, `player_start(location, yaw)`, `clip_walls(half_x, half_y, height, ceiling_z)`, `set_kill_z(z)`, `static_sun_and_sky()`, `greybox_materials()`, `rebuild_map(path)`, `assert_counts(expected: dict)`, `save_map()`. Every spawned actor receives the `Layout:<name>` tag.
- [ ] The three generators import the library and keep their public behaviour: after regeneration, `assert_counts` passes with the *unchanged* expected dictionaries and `velox.bots.DumpNav` paths listed in `Tools/editor/README.md` still resolve (`-server -nullrhi -ExecCmds=...`).
- [ ] `infra/ci/fts.sh` 7/7; `infra/ci/nettest.sh` with `PadRun StrafeRun BotRun` (rounds=1) PASS on an idle host under the soak lock.
- [ ] `Tools/editor/README.md` documents the library (one paragraph + the helper list) and states the regeneration commands.

## Verification steps
1. `infra/ci/build.sh` (module must be built for the Velox classes).
2. For each map: `"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$VELOX_PROJECT" -run=pythonscript -script="$PWD/Tools/editor/create_<x>_map.py" -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput` → exit 0, counts line unchanged.
3. `infra/ci/fts.sh` → `OK 7/7`.
4. `flock -o "$SOAK_LOCK" infra/ci/nettest.sh --scripts "PadRun StrafeRun BotRun"` (check the script's actual flag names) → PASS.
5. `git status --short Content/` — list what changed and why; prefer committing nothing under `Content/`.

## Report back
Standard format plus the counts lines of the three maps before and after.

## Notes / outcome
