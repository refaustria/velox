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
  +-300 cm along Y at full ground speed, static sun + sky light.
"""
import unreal

EAL = unreal.EditorAssetLibrary

MAP_PATH = "/Game/Maps/L_CombatTest"
MAT_DIR = "/Game/Materials"
CUBE = "/Engine/BasicShapes/Cube.Cube"              # 100 cm cube, pivot at centre
CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder"  # 100 cm diameter, 100 cm high, pivot at centre

HALF = 2000.0
WALL_H = 400.0
FIRST_START = (-1700.0, -1200.0)
DUMMY_DISTANCES = (1000.0, 2000.0, 3000.0)
DUMMY_Z = 110.0
STRAFE_HALF_WIDTH = 300.0
STRAFE_SPEED_SCALE = 1.0

ERRORS = []
COUNTS = {}
EXPECTED = {"ground": 1, "wall": 4, "cover": 8, "marker": 3, "dummyspawner": 3, "playerstart": 4, "light": 2}

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

MATS = {}
EAS = None
STATIC = unreal.ComponentMobility.STATIC
_MESH = {}


def log(msg):
    unreal.log("[VeloxCombatMap] " + msg)


def fail(msg):
    unreal.log_error("[VeloxCombatMap] " + msg)
    ERRORS.append(msg)


def load_materials():
    for key in ("Floor", "Wall", "Platform", "MarkerA"):
        path = f"{MAT_DIR}/M_Greybox_{key}"
        mat = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if mat is None:
            fail(f"missing {path}: run create_movement_test_map.py first")
        MATS[key] = mat


def mesh(path):
    if path not in _MESH:
        _MESH[path] = unreal.load_asset(path)
    return _MESH[path]


def count(kind):
    COUNTS[kind] = COUNTS.get(kind, 0) + 1


def box(kind, label, center, size, mat, collide=True, folder=None, mesh_path=CUBE):
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center))
    a.set_actor_label(label)
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    c = a.static_mesh_component
    c.set_editor_property("static_mesh", mesh(mesh_path))
    c.set_mobility(STATIC)
    c.set_material(0, MATS[mat])
    if not collide:
        c.set_collision_profile_name("NoCollision")
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    if folder:
        a.set_folder_path(folder)
    count(kind)
    return a


def build_arena():
    box("ground", "Ground", (0, 0, -50), (2 * HALF + 200, 2 * HALF + 200, 100), "Floor", folder="Arena")
    t = 100.0
    box("wall", "Wall_N", (0, HALF + t / 2, WALL_H / 2), (2 * HALF + 2 * t, t, WALL_H), "Wall", folder="Arena")
    box("wall", "Wall_S", (0, -HALF - t / 2, WALL_H / 2), (2 * HALF + 2 * t, t, WALL_H), "Wall", folder="Arena")
    box("wall", "Wall_E", (HALF + t / 2, 0, WALL_H / 2), (t, 2 * HALF, WALL_H), "Wall", folder="Arena")
    box("wall", "Wall_W", (-HALF - t / 2, 0, WALL_H / 2), (t, 2 * HALF, WALL_H), "Wall", folder="Arena")
    for i, (x, y, sx, sy, sz) in enumerate(COVER):
        box("cover", f"Cover_{i + 1:02d}", (x, y, sz / 2), (sx, sy, sz), "Platform", folder="Cover")


def build_starts_dummies_lights():
    for label, x, y, yaw in STARTS:
        ps = EAS.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x, y, 100), unreal.Rotator(0, 0, yaw))
        ps.set_actor_label(label)
        ps.set_folder_path("Starts")
        count("playerstart")
    for i, d in enumerate(DUMMY_DISTANCES):
        x, y = FIRST_START[0] + d, FIRST_START[1]
        sp = EAS.spawn_actor_from_class(unreal.VeloxDummySpawner, unreal.Vector(x, y, DUMMY_Z))
        sp.set_actor_label(f"DummySpawn_{i + 1}")
        sp.set_folder_path("Dummies")
        sp.set_editor_property("auto_spawn", True)
        sp.set_editor_property("strafe_half_width", STRAFE_HALF_WIDTH)
        sp.set_editor_property("strafe_speed_scale", STRAFE_SPEED_SCALE)
        count("dummyspawner")
        # Floor marker (no collision) showing the strafe lane centre.
        box("marker", f"DummyMarker_{i + 1}", (x, y, 0.5), (60, 2 * STRAFE_HALF_WIDTH, 1), "MarkerA",
            collide=False, folder="Dummies")
    sun = EAS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 800), unreal.Rotator(0, -45, 30))
    sun.set_actor_label("Sun")
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_mobility(STATIC)
    count("light")
    sky = EAS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 900))
    sky.set_actor_label("SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_mobility(STATIC)
    count("light")


def build_map():
    global EAS
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not EAL.does_directory_exist("/Game/Maps"):
        EAL.make_directory("/Game/Maps")
    if EAL.does_asset_exist(MAP_PATH):
        log("map exists, clearing it for rebuild")
        if not les.load_level(MAP_PATH):
            fail("could not load existing " + MAP_PATH)
            return
        for a in EAS.get_all_level_actors():
            EAS.destroy_actor(a)
    elif not les.new_level(MAP_PATH, False):
        fail("new_level failed for " + MAP_PATH)
        return

    build_arena()
    build_starts_dummies_lights()

    log("spawned: " + ", ".join(f"{k}={v}" for k, v in sorted(COUNTS.items())))
    if COUNTS != EXPECTED:
        fail(f"actor counts differ from expected: got {COUNTS}, expected {EXPECTED}")
    actors = EAS.get_all_level_actors()
    n_sm = sum(1 for a in actors if isinstance(a, unreal.StaticMeshActor))
    n_ps = sum(1 for a in actors if isinstance(a, unreal.PlayerStart))
    n_sp = sum(1 for a in actors if isinstance(a, unreal.VeloxDummySpawner))
    exp_sm = EXPECTED["ground"] + EXPECTED["wall"] + EXPECTED["cover"] + EXPECTED["marker"]
    log(f"StaticMeshActors={n_sm} (expected {exp_sm}) PlayerStarts={n_ps} (expected 4) DummySpawners={n_sp} (expected 3)")
    if n_sm != exp_sm or n_ps != 4 or n_sp != 3:
        fail("level actor class counts mismatch")
    unreal.SystemLibrary.execute_console_command(unreal.EditorLevelLibrary.get_editor_world(), "MAP CHECK")
    if not les.save_current_level():
        fail("save_current_level failed")


def main():
    load_materials()
    if not ERRORS:
        build_map()
    if ERRORS:
        raise RuntimeError("combat test map failed: " + "; ".join(ERRORS))
    log("done")


main()
