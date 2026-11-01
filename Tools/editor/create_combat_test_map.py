"""Velox M2 combat test map: /Game/Maps/L_CombatTest (ADR 0003 section 6).

Run headless:
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_combat_test_map.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: the map is cleared and rebuilt on every run. Reuses the /Game/Materials/M_Greybox_* materials created by
create_movement_test_map.py (run that first on a fresh checkout). Centimetres, floor top at Z=0.
Layout (X forward):
  arena 4000 x 4000 cm (x,y -2000..2000), perimeter walls 400 high / 100 thick, 8 cover blocks,
  4 PlayerStarts near the corners (+-1700, +-1200; the first, (-1700,-1200), faces +X towards the dummies),
  3 AVeloxDummySpawner (bAutoSpawn) at 1000/2000/3000 cm from the first PlayerStart along +X, each strafing
  +-300 cm along Y at full ground speed, static sun + sky light,
  M4 (ADR 0005 section 3): one NavMeshBoundsVolume over the arena so the server builds the runtime nav mesh for bots
  (the arena is flat with cover blocks only, so no AVeloxNavLink is needed).
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import veloxmap as vm  # noqa: E402


MAP_PATH = "/Game/Maps/L_CombatTest"

HALF = 2000.0
WALL_H = 400.0
FIRST_START = (-1700.0, -1200.0)
DUMMY_DISTANCES = (1000.0, 2000.0, 3000.0)
DUMMY_Z = 110.0
STRAFE_HALF_WIDTH = 300.0
STRAFE_SPEED_SCALE = 1.0

EXPECTED = {"ground": 1, "wall": 4, "clip": 5, "cover": 8, "marker": 3, "dummyspawner": 3, "playerstart": 4, "light": 2, "navbounds": 1}

# (x, y, size x, size y, size z): all clear of the dummy lane y -1500..-900 and of the PlayerStarts.
COVER = [
    (-1200, 500, 200, 200, 200),
    (-400, 1000, 300, 200, 250),
    (500, 300, 200, 400, 200),
    (1200, 900, 200, 200, 300),
    (200, -100, 300, 300, 150),
    (1300, -100, 200, 200, 200),
    (-1300, 1500, 400, 150, 250),
    (900, 1600, 150, 300, 200),
]
STARTS = [  # (label, x, y, yaw)
    ("PlayerStart_SW", FIRST_START[0], FIRST_START[1], 0),
    ("PlayerStart_SE", 1700, -1200, 180),
    ("PlayerStart_NW", -1700, 1200, 0),
    ("PlayerStart_NE", 1700, 1200, 180),
]



KILL_Z = -1000.0       # lowest walkable floor is Z=0; anything below this has left the map and dies via FellOutOfWorld


def build_arena():
    vm.box("Ground", (0, 0, -50), (2 * HALF + 200, 2 * HALF + 200, 100), "Floor", "ground", folder="Arena")
    t = 100.0
    vm.box("Wall_N", (0, HALF + t / 2, WALL_H / 2), (2 * HALF + 2 * t, t, WALL_H), "Wall", "wall", folder="Arena")
    vm.box("Wall_S", (0, -HALF - t / 2, WALL_H / 2), (2 * HALF + 2 * t, t, WALL_H), "Wall", "wall", folder="Arena")
    vm.box("Wall_E", (HALF + t / 2, 0, WALL_H / 2), (t, 2 * HALF, WALL_H), "Wall", "wall", folder="Arena")
    vm.box("Wall_W", (-HALF - t / 2, 0, WALL_H / 2), (t, 2 * HALF, WALL_H), "Wall", "wall", folder="Arena")
    for i, (x, y, sx, sy, sz) in enumerate(COVER):
        vm.box(f"Cover_{i + 1:02d}", (x, y, sz / 2), (sx, sy, sz), "Platform", "cover", folder="Cover")


def build_starts_dummies_lights():
    for label, x, y, yaw in STARTS:
        vm.player_start(label, (x, y, 100), yaw, folder="Starts")
    for i, d in enumerate(DUMMY_DISTANCES):
        x, y = FIRST_START[0] + d, FIRST_START[1]
        sp = vm.spawn(unreal.VeloxDummySpawner, f"DummySpawn_{i + 1}", (x, y, DUMMY_Z), None, "Dummies", "dummyspawner")
        sp.set_editor_property("auto_spawn", True)
        sp.set_editor_property("strafe_half_width", STRAFE_HALF_WIDTH)
        sp.set_editor_property("strafe_speed_scale", STRAFE_SPEED_SCALE)
        # Floor marker (no collision) showing the strafe lane centre.
        vm.box(f"DummyMarker_{i + 1}", (x, y, 0.5), (60, 2 * STRAFE_HALF_WIDTH, 1), "MarkerA", "marker",
               collide=False, folder="Dummies")
    vm.static_sun_and_sky()


def build_map():
    les = vm.rebuild_map(MAP_PATH)
    if les is None:
        return
    build_arena()
    build_starts_dummies_lights()
    vm.clip_walls(HALF, HALF, wall_t=100.0)
    vm.set_kill_z(KILL_Z)
    # Arena plus walls, floor slab to above the highest cover block (300 cm).
    vm.nav_bounds((0.0, 0.0, 300.0), (2 * HALF + 400, 2 * HALF + 400, 1000.0))

    vm.assert_counts(EXPECTED)
    actors = vm.EAS.get_all_level_actors()
    n_sm = sum(1 for a in actors if isinstance(a, unreal.StaticMeshActor))
    n_ps = sum(1 for a in actors if isinstance(a, unreal.PlayerStart))
    n_sp = sum(1 for a in actors if isinstance(a, unreal.VeloxDummySpawner))
    exp_sm = EXPECTED["ground"] + EXPECTED["wall"] + EXPECTED["clip"] + EXPECTED["cover"] + EXPECTED["marker"]
    vm.log(f"StaticMeshActors={n_sm} (expected {exp_sm}) PlayerStarts={n_ps} (expected 4) DummySpawners={n_sp} (expected 3)")
    if n_sm != exp_sm or n_ps != 4 or n_sp != 3:
        vm.fail("level actor class counts mismatch")
    vm.save_map(les)


def main():
    vm.configure(log_prefix="[VeloxCombatMap]", fill_nav_kinds=("cover",))
    vm.greybox_materials(keys=("Floor", "Wall", "Platform", "MarkerA"))
    if not vm.ERRORS:
        build_map()
    vm.raise_if_errors("combat test map")
    vm.log("done")


main()
