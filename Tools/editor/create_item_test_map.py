"""Velox M3 item test map: /Game/Maps/L_ItemTest (ADR 0004 section 8).

Run headless (needs /Game/Items/DA_Item_* from create_item_content.py and the M_Greybox_* materials from
create_movement_test_map.py, run both first on a fresh checkout):
  UnrealEditor-Cmd /abs/path/Velox.uproject -run=pythonscript -script=/abs/path/Tools/editor/create_item_test_map.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: the map is cleared and rebuilt on every run. Centimetres, floor top at Z=0, X forward.
Layout:
  arena 6000 x 4000 (x -3000..3000, y -2000..2000), perimeter walls 400 high / 100 thick,
  central cross of 200 high cover (600 x 100 along X and Y),
  item grid along the south wall: 18 items, one row at y -1700, 300 cm apart from x -2550, each with a text label
  (name and respawn time) on the wall. The first three are Health.Small, Armor.Shard and Ammo.Bullets: ItemRun.json walks
  over them from (-2700, -1700), the items after them (Health.Medium / Large) are refused at 100+ health so a longer run
  does not take anything else,
  Mega Health and Red Armor on 3 m platforms (400 x 400), each reachable only by a jump pad 600 cm before the platform
  edge (same geometry as pad A of L_MovementTest: apex 30 cm above the platform for the capsule),
  Yellow Armor on a 15 degree ramp (y 1200) with a platform at its top,
  50 m corridor along the north wall (x -2500..2500, 250 high wall at y 1500, markers every 500 cm), Haste at its start,
  Quad near the centre (500, 0), 8 PlayerStarts, one CameraActor tagged Intermission above the arena, static sun and
  sky light, WorldSettings.DefaultGameMode = AVeloxGameMode_FFA (AVeloxGameMode until task 05 adds the FFA class).
"""
import math
import unreal

EAL = unreal.EditorAssetLibrary

MAP_PATH = "/Game/Maps/L_ItemTest"
MAT_DIR = "/Game/Materials"
ITEM_DIR = "/Game/Items"
CUBE = "/Engine/BasicShapes/Cube.Cube"              # 100 cm cube, pivot at centre
CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder"  # 100 cm diameter, 100 cm high, pivot at centre

HALF_X = 3000.0
HALF_Y = 2000.0
WALL_H = 400.0
WALL_T = 100.0

GRID_Y = -1700.0
GRID_X0 = -2550.0
GRID_DX = 300.0
# Grid order: the ItemRun items first (Small, Shard, Bullets), then the rest.
GRID = [
    "Health.Small", "Armor.Shard", "Ammo.Bullets", "Health.Medium", "Health.Large",
    "Ammo.Shells", "Ammo.Rockets", "Ammo.Cells", "Ammo.Lightning", "Ammo.Slugs",
    "Weapon.MachineGun", "Weapon.Shotgun", "Weapon.RocketLauncher", "Weapon.LightningGun", "Weapon.Railgun", "Weapon.PlasmaGun",
    "Powerup.Regen", "Powerup.BattleSuit",
]

PLATFORM_TOP = 300.0
PLATFORM_SIZE = 400.0
PAD_TO_EDGE = 600.0      # pad centre to the platform's near edge (pad A of the movement map)
PAD_LAUNCH_Z = 70.0      # trigger centre above the floor
PAD_TARGET_Z = PLATFORM_TOP + 70.0 + 30.0   # capsule-centre apex: capsule bottom 30 cm above the platform top
PAD_TRIGGER_EXTENT = (50.0, 50.0, 70.0)
PAD_MAX_LAUNCH_SPEED = 4000.0
VALIDATION_GRAVITY_Z = -2000.0

RAMP_DEG = 15.0
RAMP_LEN = 800.0
RAMP_X0 = -600.0
RAMP_Y = 1200.0
RAMP_W = 400.0

CORRIDOR_X0 = -2500.0
CORRIDOR_X1 = 2500.0
CORRIDOR_WALL_Y = 1500.0
CORRIDOR_WALL_H = 250.0
MARKER_STEP = 500.0

QUAD_AT = (500.0, 0.0)
HASTE_AT = (CORRIDOR_X0 + 100.0, 1775.0)

STARTS = [  # (label, x, y, yaw)
    ("PlayerStart_1", -2700, -600, 0),
    ("PlayerStart_2", 2700, -600, 180),
    ("PlayerStart_3", -2700, 600, 0),
    ("PlayerStart_4", 2700, 600, 180),
    ("PlayerStart_5", -1200, -900, 90),
    ("PlayerStart_6", 1200, -900, 90),
    ("PlayerStart_7", -1000, 500, 0),
    ("PlayerStart_8", 1000, 500, 180),
]

ERRORS = []
COUNTS = {}
EXPECTED = {
    "ground": 1, "wall": 4, "cross": 2, "platform": 3, "ramp": 1, "corridorwall": 1, "marker": 11,
    "pickup": 23, "jumppad": 2, "targetpoint": 2, "playerstart": 8, "camera": 1, "light": 2,
}
MATS = {}
EAS = None
STATIC = unreal.ComponentMobility.STATIC
_MESH = {}
PADS = []


def log(msg):
    unreal.log("[VeloxItemMap] " + msg)


def fail(msg):
    unreal.log_error("[VeloxItemMap] " + msg)
    ERRORS.append(msg)


def load_materials():
    for key in ("Floor", "Wall", "Platform", "MarkerA", "MarkerB", "Pad", "Target", "RampWalkable"):
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


def box(kind, label, center, size, mat, rot=None, collide=True, folder=None, mesh_path=CUBE):
    """Static mesh box. size = full extents in cm (cube/cylinder is 100 cm, so scale = size / 100)."""
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center), rot or unreal.Rotator(0, 0, 0))
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


def text(label, loc, msg, yaw, size=36.0, folder="Labels"):
    """Text label; the front of a TextRenderActor faces its local +X, so yaw 90 faces +Y (north), 180 faces -X."""
    a = EAS.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(*loc), unreal.Rotator(0, 0, yaw))  # (roll, pitch, yaw)
    a.set_actor_label(label)
    comp = a.text_render
    comp.set_editor_property("text", msg)
    comp.set_editor_property("world_size", size)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    a.set_folder_path(folder)
    count("text")
    return a


def item_asset(name):
    path = f"{ITEM_DIR}/DA_Item_{name.replace('.', '_')}"
    da = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
    if da is None:
        fail(f"missing {path}: run create_item_content.py first")
    return da


def place_item(name, floor_point, label=None, folder="Items"):
    """AVeloxPickup with its origin on the floor point (ADR 0004 section 1)."""
    da = item_asset(name)
    a = EAS.spawn_actor_from_class(unreal.VeloxPickup, unreal.Vector(*floor_point))
    a.set_actor_label(label or ("Pickup_" + name.replace(".", "_")))
    a.set_folder_path(folder)
    if da is not None:
        a.set_editor_property("definition", da)
    count("pickup")
    return a, da


def respawn_text(name, da):
    if da is None:
        return name
    secs = float(da.get_editor_property("respawn_seconds"))
    kind = " (mode)" if name.startswith("Weapon.") else ""
    return f"{name}\n{secs:g} s{kind}"


# ---------------------------------------------------------------- sections
def build_arena():
    box("ground", "Ground", (0, 0, -50), (2 * HALF_X + 200, 2 * HALF_Y + 200, 100), "Floor", folder="Arena")
    t = WALL_T
    box("wall", "Wall_N", (0, HALF_Y + t / 2, WALL_H / 2), (2 * HALF_X + 2 * t, t, WALL_H), "Wall", folder="Arena")
    box("wall", "Wall_S", (0, -HALF_Y - t / 2, WALL_H / 2), (2 * HALF_X + 2 * t, t, WALL_H), "Wall", folder="Arena")
    box("wall", "Wall_E", (HALF_X + t / 2, 0, WALL_H / 2), (t, 2 * HALF_Y, WALL_H), "Wall", folder="Arena")
    box("wall", "Wall_W", (-HALF_X - t / 2, 0, WALL_H / 2), (t, 2 * HALF_Y, WALL_H), "Wall", folder="Arena")
    box("cross", "Cross_X", (0, 0, 100), (600, 100, 200), "Platform", folder="Cover")
    box("cross", "Cross_Y", (0, 0, 100), (100, 600, 200), "Platform", folder="Cover")


def build_grid():
    for i, name in enumerate(GRID):
        x = GRID_X0 + i * GRID_DX
        _, da = place_item(name, (x, GRID_Y, 0))
        text(f"Label_{name.replace('.', '_')}", (x, -HALF_Y + 10, 260), respawn_text(name, da), 90, folder="Labels")
        box("marker", f"GridMarker_{i + 1:02d}", (x, GRID_Y, 0.5), (90, 90, 1), "MarkerA" if i % 2 == 0 else "MarkerB",
            collide=False, folder="Items")


def build_platform_item(tag, item, pad_x, y):
    """Platform 400 x 400 x 300, jump pad 600 cm before its near edge; the item stands at the platform centre."""
    edge = pad_x + PAD_TO_EDGE
    cx = edge + PLATFORM_SIZE / 2
    box("platform", f"{tag}_Platform", (cx, y, PLATFORM_TOP / 2), (PLATFORM_SIZE, PLATFORM_SIZE, PLATFORM_TOP), "Platform", folder=tag)
    tp = EAS.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(edge, y, PAD_TARGET_Z))
    tp.set_actor_label(f"{tag}_PadTarget")
    tp.set_folder_path(tag)
    count("targetpoint")
    a = EAS.spawn_actor_from_class(unreal.VeloxJumpPad, unreal.Vector(pad_x, y, PAD_LAUNCH_Z))
    a.set_actor_label(f"{tag}_JumpPad")
    a.set_folder_path(tag)
    a.set_editor_property("mode", unreal.VeloxJumpPadMode.AIM_AT_TARGET)
    a.set_editor_property("target", tp)
    a.set_editor_property("override_velocity", True)
    a.set_editor_property("max_launch_speed", PAD_MAX_LAUNCH_SPEED)
    a.get_editor_property("trigger").set_box_extent(unreal.Vector(*PAD_TRIGGER_EXTENT), False)
    pm = a.get_editor_property("pad_mesh")
    pm.set_static_mesh(mesh(CYLINDER))
    pm.set_material(0, MATS["Pad"])
    pm.set_relative_location(unreal.Vector(0, 0, 5 - PAD_LAUNCH_Z), False, False)
    pm.set_relative_scale3d(unreal.Vector(PAD_TRIGGER_EXTENT[0] / 50.0, PAD_TRIGGER_EXTENT[1] / 50.0, 0.1))
    count("jumppad")
    PADS.append(a)
    _, da = place_item(item, (cx, y, PLATFORM_TOP), folder=tag)
    text(f"Label_{tag}", (pad_x - 150, y, 140), respawn_text(item, da) + "\n3 m platform, jump pad", 0, folder=tag)
    box("marker", f"{tag}_PadTargetMarker", (edge, y, PLATFORM_TOP + 2), (120, 120, 4), "Target", collide=False,
        folder=tag, mesh_path=CYLINDER)


def build_platforms():
    build_platform_item("MegaHealth", "Health.Mega", -2300.0, 1200.0)
    build_platform_item("RedArmor", "Armor.Red", 1300.0, 1200.0)


def build_ramp():
    rad = math.radians(RAMP_DEG)
    run, rise = RAMP_LEN * math.cos(rad), RAMP_LEN * math.sin(rad)
    thick = 20.0
    # Same construction as the movement map: pitch about Y, rising along +X from (RAMP_X0, y, 0).
    cx = RAMP_X0 + RAMP_LEN / 2 * math.cos(rad) + thick / 2 * math.sin(rad)
    cz = RAMP_LEN / 2 * math.sin(rad) - thick / 2 * math.cos(rad)
    box("ramp", "Ramp_YellowArmor", (cx, RAMP_Y, cz), (RAMP_LEN, RAMP_W, thick), "RampWalkable",
        rot=unreal.Rotator(0, RAMP_DEG, 0), folder="YellowArmor")
    box("platform", "Ramp_TopPlatform", (RAMP_X0 + run + 200, RAMP_Y, rise / 2), (400, RAMP_W, rise), "Platform", folder="YellowArmor")
    mid = RAMP_LEN / 2
    _, da = place_item("Armor.Yellow", (RAMP_X0 + mid * math.cos(rad), RAMP_Y, mid * math.sin(rad)), folder="YellowArmor")
    text("Label_YellowArmor", (RAMP_X0 - 100, RAMP_Y, 140), respawn_text("Armor.Yellow", da) + "\n15 deg ramp", 0, folder="YellowArmor")


def build_corridor():
    length = CORRIDOR_X1 - CORRIDOR_X0
    box("corridorwall", "Corridor_Wall", ((CORRIDOR_X0 + CORRIDOR_X1) / 2, CORRIDOR_WALL_Y + WALL_T / 2, CORRIDOR_WALL_H / 2),
        (length, WALL_T, CORRIDOR_WALL_H), "Wall", folder="Corridor")
    inner_y = (CORRIDOR_WALL_Y + WALL_T + HALF_Y) / 2
    n = int(length / MARKER_STEP) + 1
    for i in range(n):
        x = CORRIDOR_X0 + i * MARKER_STEP
        box("marker", f"CorridorMarker_{i:02d}", (x, inner_y, 0.5), (30, 380, 1), "MarkerA" if i % 2 == 0 else "MarkerB",
            collide=False, folder="Corridor")
        text(f"CorridorText_{i:02d}", (x, CORRIDOR_WALL_Y + WALL_T + 2, 180), f"{i * MARKER_STEP / 100:g} m", 90, size=40.0, folder="Corridor")
    # The text count belongs to "text"; markers are counted as "marker" (n = 11).
    _, da = place_item("Powerup.Haste", (HASTE_AT[0], HASTE_AT[1], 0), folder="Corridor")
    text("Label_Haste", (CORRIDOR_X0 - 60, HASTE_AT[1], 200), respawn_text("Powerup.Haste", da) + "\n50 m corridor", 0, folder="Corridor")


def build_quad():
    _, da = place_item("Powerup.Quad", (QUAD_AT[0], QUAD_AT[1], 0), folder="Powerups")
    text("Label_Quad", (QUAD_AT[0] + 100, QUAD_AT[1], 200), respawn_text("Powerup.Quad", da), 0, folder="Powerups")


def build_starts_camera_lights():
    for label, x, y, yaw in STARTS:
        ps = EAS.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x, y, 100), unreal.Rotator(0, 0, yaw))
        ps.set_actor_label(label)
        ps.set_folder_path("Starts")
        count("playerstart")
    cam = EAS.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(0, -2600, 2200), unreal.Rotator(0, -40, 90))
    cam.set_actor_label("IntermissionCamera")
    cam.set_editor_property("tags", [unreal.Name("Intermission")])
    cam.set_folder_path("Starts")
    count("camera")
    sun = EAS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 800), unreal.Rotator(0, -45, 30))
    sun.set_actor_label("Sun")
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_mobility(STATIC)
    count("light")
    sky = EAS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 900))
    sky.set_actor_label("SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_mobility(STATIC)
    count("light")


def set_game_mode():
    world = unreal.EditorLevelLibrary.get_editor_world()
    settings = world.get_world_settings()
    cls = getattr(unreal, "VeloxGameMode_FFA", None)
    if cls is None:
        log("VeloxGameMode_FFA not available yet (task 05): using VeloxGameMode")
        cls = unreal.VeloxGameMode
    settings.set_editor_property("default_game_mode", cls)
    got = settings.get_editor_property("default_game_mode")
    log(f"WorldSettings.DefaultGameMode = {got.get_name() if got else None}")
    if got is None:
        fail("could not set WorldSettings.DefaultGameMode")


def check_pads():
    for a in PADS:
        v = a.compute_launch_velocity(VALIDATION_GRAVITY_Z)
        log(f"{a.get_actor_label()}: launch {v} |v|={v.length():.0f} cm/s")
        if v.length() <= 0.0 or v.length() > PAD_MAX_LAUNCH_SPEED:
            fail(f"{a.get_actor_label()}: launch speed {v.length():.0f} out of range")


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
    build_grid()
    build_platforms()
    build_ramp()
    build_corridor()
    build_quad()
    build_starts_camera_lights()
    set_game_mode()
    check_pads()

    # Markers: 18 grid + 11 corridor + 2 pad targets = 31; platforms: 2 item platforms + ramp top = 3.
    expected = dict(EXPECTED)
    expected["marker"] = len(GRID) + 11 + 2
    log("spawned: " + ", ".join(f"{k}={v}" for k, v in sorted(COUNTS.items())))
    for k, v in expected.items():
        if COUNTS.get(k, 0) != v:
            fail(f"{k}: spawned {COUNTS.get(k, 0)}, expected {v}")
    actors = EAS.get_all_level_actors()
    pickups = [a for a in actors if isinstance(a, unreal.VeloxPickup)]
    defs = {a.get_editor_property("definition").get_name() for a in pickups if a.get_editor_property("definition")}
    log(f"VeloxPickups={len(pickups)} distinct definitions={len(defs)} (expected 23/23)")
    if len(pickups) != 23 or len(defs) != 23:
        fail("pickup count or distinct definition count differs from 23")
    unreal.SystemLibrary.execute_console_command(unreal.EditorLevelLibrary.get_editor_world(), "MAP CHECK")
    if not les.save_current_level():
        fail("save_current_level failed")


def main():
    load_materials()
    if not ERRORS:
        build_map()
    if ERRORS:
        raise RuntimeError("item test map failed: " + "; ".join(ERRORS))
    log("done")


main()
