# Tools/editor

Editor Python scripts, run headless with the `pythonscript` commandlet (PythonScriptPlugin is enabled for Editor targets in `Velox.uproject`).

- `create_bootstrap_content.py` - creates `L_Empty`, Enhanced Input assets and the default data assets. Idempotent. See `docs/content-bootstrap.md`.
- `create_movement_test_map.py` - builds `/Game/Maps/L_MovementTest` (ADR 0002 section 7) plus flat unlit `/Game/Materials/M_Greybox_*` materials. Idempotent (deletes and rebuilds the map), asserts actor counts and raises on mismatch. Layout: 5120 cm strafe corridor with markers every 512 cm (text in Q3 units and metres), ramps 10/20/30/45/50 deg, stairs with 20/40/60 cm risers, 3 jump-pad placeholders with target markers (replace by `AVeloxJumpPad` in task 04), 3 bunny-hop gaps (500/700/900 cm over a 100 cm pit; each with a walled floor-height runway behind its 800 cm start platform, 1200/2500/4500 cm long, markers every 512 cm, a PlayerStart at the runway start, and 4 x 20 cm exit steps from the pit back onto the start platform), 5 PlayerStarts (corridor, pads, 3 gap runways), static sun and sky light. Materials are flat colours (no checker); geometry is `/Engine/BasicShapes` cubes and cylinders. Run:
  `UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_movement_test_map.py -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput`
  (first run compiles engine shaders on the CPU, allow ~10 minutes).

Notes on the 5.8 Python API (learned the hard way):
- `UPROPERTY(EditDefaultsOnly)` members of *structs* cannot be set with `set_editor_property`; pass them to the struct constructor kwargs instead (`unreal.VeloxInputAction(input_action=..., input_tag=...)`).
- `unreal.GameplayTag` has no constructor args; use `t = unreal.GameplayTag(); t.import_text('(TagName="InputTag.Move")')`.
- `unreal.Key` likewise: `k = unreal.Key(); k.set_editor_property("key_name", "SpaceBar")`.
- `InputMappingContext.map_key()` returns the mapping by value; attach modifiers by editing `default_key_mappings.mappings` and writing the struct back.

Map findings from the task-05 scripted runs (all fixed in `create_movement_test_map.py`; map regenerated):
- `unreal.Rotator` takes `(roll, pitch, yaw)`: ramps now use `Rotator(0, deg, 0)` (pitch; verified with `velox.move.Probe`, height rises along +X and is constant along Y) and text actors `Rotator(0, 0, 180)`.
- Marker/target meshes (`collide=False`) now also get `set_collision_profile_name("NoCollision")`; `set_collision_enabled` alone left a blocking profile at runtime.
- Corridor `PlayerStart` moved to (120, 0, 100) (was overlapping the back wall).
- Pad C target raised 40 cm (capsule centre z 750, marker disc at z 680) so the apex clears the ledge top (640); `PadRun.json` pad C leg holds forward for 1.0 s after the launch, then coasts onto the ledge.

Movement scripts and tooling: see `docs/movement-tooling.md`.

- `create_combat_test_map.py` - builds `/Game/Maps/L_CombatTest` (ADR 0003 section 6): 4000 x 4000 cm arena, 400 cm perimeter walls, 8 cover blocks, 4 PlayerStarts near the corners (first one at (-1700,-1200) faces +X), 3 `AVeloxDummySpawner` (`bAutoSpawn`) at 1000/2000/3000 cm from the first PlayerStart along +X. Each spawns an `AVeloxCharacter` possessed by `AVeloxDummyController`, which strafes along Y between +-`StrafeHalfWidth` (300 cm) at `StrafeSpeedScale` (1.0) of max ground speed through `AddMovementInput` / `SetLocalMoveInput` (server only) and logs its position every 2 s (`LogVelox`, Log). Reuses `M_Greybox_*` (run `create_movement_test_map.py` first on a fresh checkout), idempotent, asserts counts. Pass an absolute `-script=` path (a relative one resolves against the engine binaries dir). Smoke: `UnrealEditor-Cmd Velox.uproject L_CombatTest -game -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput`.

## Bot navigation in the map scripts (M4, ADR 0005 sections 3 and 4)

The server builds a Recast nav mesh at map load (`RuntimeGeneration=Dynamic`, `Config/DefaultEngine.ini`); nothing is baked or saved in the map. Every map script that bots should play on follows these conventions:

- **One `NavMeshBoundsVolume` over the whole map** (`nav_bounds(...)` helper). UE 5.8 has no working whole-world mode (`bWholeWorldNavigable` is not a config property), so without a volume no nav data is created. A volume spawned from Python gets the default 200 cm cube brush; the scripts scale it to `size / 200` and assert the resulting bounds.
- **`AVeloxNavLink` actors** (`unreal.VeloxNavLink`, `nav_link(...)` helper): the actor stands at the link start, `link_start` stays zero, `link_end` is the end relative to the actor (no rotation). Set `type` (`unreal.VeloxNavLinkType.JUMP_PAD | DROP | JUMP | JUMP_FAST | ROCKET_JUMP`), `jump_pad` for pad links, `both_ways` only for symmetric gap jumps, `required_speed_cm_s` for gaps. Snap radius 60 / snap height 100 are the class defaults (`link_snap_radius`, `link_snap_height`).
  - Every jump pad gets a `JUMP_PAD` link from the pad trigger centre on the floor to the landing point the author intends (the item / marker on the platform; `velox.bots.DumpPadLandings` on a server prints `AVeloxJumpPad::ComputeLandingPoint` next to the link end).
  - Every free platform edge gets a `DROP` link from 60 cm inside the edge (platform top) to the floor 150 cm outside, at the edge midpoint. Edges that face a wall closer than ~200 cm, a ramp's own edge, or a pad (the drop would land on it) get none.
  - Gap jumps: start 60 cm before the take-off edge, end 100 cm onto the landing platform; `JUMP` for gaps a bot clears at ground speed (500 cm), `JUMP_FAST` when strafe speed is needed (700 cm), no link beyond a bot's reliable speed (900 cm).
  - `ROCKET_JUMP` links (one in `L_ItemTest`, onto the 350 cm rockets platform): start on the floor 450 cm from the platform edge (run-up), end at the platform centre.
- The actor-count asserts include `navbounds` and `navlink`. Check a regenerated map headless: `UnrealEditor-Cmd Velox.uproject <Map> -server -nullrhi -ExecCmds="velox.bots.DumpPadLandings, velox.bots.DumpNav x y z x y z [Walk|Hop|Full]"` logs `VeloxNav built tiles= seconds= links= relevant_triggers=0`, the pad landings and the path points with `link=<type>` (`LogVeloxBots`).
- `L_ItemTest`: `Ammo.Rockets` stands on the rockets platform; its grid slot keeps the marker and a "moved" label so every other grid item keeps its position (one placed instance per definition, as `Velox.Items.ItemTestMap` asserts).
