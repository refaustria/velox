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

Findings from the task-05 scripted runs on `L_MovementTest` (to fix in `create_movement_test_map.py`, then re-run it):
- `unreal.Rotator` takes `(roll, pitch, yaw)`. The ramp slabs use `unreal.Rotator(deg, 0, 0)`, which **rolls** them: probes show the surface height falling with Y (`velox.move.Probe 800 1700 1000 800 1700 0` hits z 279 and the height changes by 0.364 per cm of Y at the 20 degree lane) instead of rising along X. Use `unreal.Rotator(0, deg, 0)` for pitch. The text actors use `Rotator(0, 180, 0)` (pitch 180) where the comment says yaw 180: should be `Rotator(0, 0, 180)`.
- `JumpPadC_Target` (and probably the other marker discs) report `NO_COLLISION` in the editor but block the pawn at runtime (`velox.move.Probe 500 -3500 300 500 -3500 800` hits a `StaticMeshActor` at z 640); the pad C flight bumps its head at z 640 and never reaches the ledge. Set the `NoCollision` collision profile (`set_collision_profile_name("NoCollision")`) instead of `set_collision_enabled`.
- The corridor `PlayerStart` at (0, 0, 100) overlaps the back wall (face at x = 0) and spawns the pawn at x ~ 38; the movement scripts start at (40, 0, 100).
- Pad C: the apex target is exactly the ledge height. The capsule clears the ledge top by about 2 cm for a window of ~0.09 s around the apex, so a human cannot land on it; raise `PAD_LAUNCH_Z + 640` by ~40 cm.

Movement scripts and tooling: see `docs/movement-tooling.md`.
