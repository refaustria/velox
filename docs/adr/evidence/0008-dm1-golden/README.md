# ADR 0008 evidence: DM1 golden (task M7 P1)

Taken at base `origin/m7-maps` (359b68c + ADR 0008), from the UNMODIFIED generator `Tools/editor/create_dm1_map.py` and the
committed `.umap` files (not rewritten).

## Golden JSON (pure Python)

`Tools/editor/maps/golden/dm1.json`. Produced by:

    python3 -I Tools/editor/maps/golden_dump.py --out Tools/editor/maps/golden/dm1.json

`golden_dump.py` calls the generator's `validate_layout`, `all_solids`, `art_plan`, `expected_counts`, `art_counts`, `pad_arc`
(via `_api()`; P2 adapts only that function to `mapgen` / the shim). Content: `problems` (empty), sorted `figure_lines` (floats via
`repr`), `all_solids`, `art_plan` tuples, `expected_counts`, `art_counts`, `pad_landings`. Keys sorted, 1-space indent, one trailing newline.

Determinism: generated three times (PYTHONHASHSEED unset, 7, 99): byte-identical.
sha256 `2205d7d525584403bcd7aa50c8a89588abd526320564e75b38a9c3515d6f406f`.
P2 re-run / check: run the same command to a temp file and `cmp` it with `golden/dm1.json`.

## Actor dumps (engine)

`L_DM1_Blockout.actors.txt` (224 actors) and `L_DM1.actors.txt` (777 actors) from the committed maps:

    flock -o <scratchpad>/soak.lock /opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor-Cmd <Velox.uproject> -run=pythonscript \
      -script="<abs>/Tools/editor/audit_map.py /Game/Maps/DM1/<Map> --dump-actors <abs out.txt>" -nullrhi -unattended -nopause -nosplash -stdout

Format: one block per actor (class, `Layout:` tags, location, rotation, scale, root collision profile, then per static mesh component
mesh, materials, collision profile, collision enabled, nav flag, world location and scale), sorted; floats with 3 decimals.
P2 gate 3: dump the regenerated maps the same way and `diff` against these (transforms within 0.01 cm).

Provenance: dumped 2026-11-12 20:27 CET under the soak lock (UE 5.8.4, the editor module from the main checkout's Binaries;
`Source/` unchanged by this task), with the `dump_actors` code as committed here. `collision=` is printed as the engine's
enum repr (`<CollisionEnabled.QUERY_AND_PHYSICS: 3>`). The engine was no longer under /opt at the end of the task, so the dumps were
not taken a second time; P2 dumps the regenerated maps with the same code.
