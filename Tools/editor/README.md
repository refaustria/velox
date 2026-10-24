# Tools/editor

Editor Python scripts, run headless with the `pythonscript` commandlet (PythonScriptPlugin is enabled for Editor targets in `Velox.uproject`).

- `create_bootstrap_content.py` - creates `L_Empty`, Enhanced Input assets and the default data assets. Idempotent. See `docs/content-bootstrap.md`.
- `create_movement_test_map.py` - builds `/Game/Maps/L_MovementTest` (ADR 0002 section 7) plus flat unlit `/Game/Materials/M_Greybox_*` materials. Idempotent (deletes and rebuilds the map), asserts actor counts and raises on mismatch. Layout: 5120 cm strafe corridor with markers every 512 cm (text in Q3 units and metres), ramps 10/20/30/45/50 deg, stairs with 20/40/60 cm risers, 3 jump-pad placeholders with target markers (replace by `AVeloxJumpPad` in task 04), 3 bunny-hop gaps (500/700/900 cm), 2 PlayerStarts, static sun and sky light. Materials are flat colours (no checker); geometry is `/Engine/BasicShapes` cubes and cylinders. Run:
  `UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_movement_test_map.py -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput`
  (first run compiles engine shaders on the CPU, allow ~10 minutes).

Notes on the 5.8 Python API (learned the hard way):
- `UPROPERTY(EditDefaultsOnly)` members of *structs* cannot be set with `set_editor_property`; pass them to the struct constructor kwargs instead (`unreal.VeloxInputAction(input_action=..., input_tag=...)`).
- `unreal.GameplayTag` has no constructor args; use `t = unreal.GameplayTag(); t.import_text('(TagName="InputTag.Move")')`.
- `unreal.Key` likewise: `k = unreal.Key(); k.set_editor_property("key_name", "SpaceBar")`.
- `InputMappingContext.map_key()` returns the mapping by value; attach modifiers by editing `default_key_mappings.mappings` and writing the struct back.
