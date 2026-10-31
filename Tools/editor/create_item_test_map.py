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
  Yellow Armor on a 15 degree ramp (y 1200) with a platform at its top and three fill boxes (Ramp_Fill_1..3, X -400..172.7,
  Z 0 up to the ramp underside at their low-X end) sealing the void under the thin ramp (ADR 0005 D41: no walkable floor
  under a walkable surface closer than the agent height); the 100 cm slot north of the ramp (Y 1400..1500) is filled flush
  with the ramp/platform surface from X -432 to 572.7 (ADR 0005 D46: no mesh pocket the bots can be projected into),
  a sealed D17 test room off the playable area (x 3800..5600, y 600..1700, own floor, walls and nav bounds; no door, no link;
  two 100 x 1200 cm slots between 207 cm blocks and the walls, one with a 48 cm step along its first 200 cm) for velox.bots.FtOffMesh slot_* cases,
  50 m corridor along the north wall (x -2500..2500, 250 high wall at y 1500, markers every 500 cm), Haste at its start,
  Quad near the centre (500, 0), 8 PlayerStarts,
  M4 (ADR 0005 sections 4 and 14): a third platform in the south-east quadrant (350 cm high, 400 x 400, no pad,
  Ammo.Rockets at its centre - moved there from its grid slot, which keeps a label - and a label) reachable only by a
  rocket jump, and AVeloxNavLink actors for the bots: one JumpPad
  link per pad (pad floor point -> the platform centre), Drop links at every free platform edge (60 cm inside the edge ->
  the floor 150 cm out; edges facing the corridor wall 100 cm away and the ramp's own edge get none), one RocketJump link
  onto the rockets platform (450 cm east of its east edge -> its centre), one NavMeshBoundsVolume over the arena, one CameraActor tagged Intermission above the arena, static sun and
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
# M4: grid slots whose item now stands elsewhere (one placed instance per definition: Velox.Items.ItemTestMap asserts 23
# distinct items, and the bots' item goals by tag stay unambiguous). Value: the slot's label text.
GRID_MOVED = {"Ammo.Rockets": "moved to the rocket-jump\nplatform (south-east)"}

PLATFORM_TOP = 300.0
MEGA_PAD = (-2300.0, 1200.0)      # (pad x, y): platform x -1700..-1300, y 1000..1400
RA_PAD = (1300.0, 1200.0)         # platform x 1900..2300, y 1000..1400

# Third platform (M4, ADR 0005 section 14): reachable only by a rocket jump, carries Ammo.Rockets as the reason to go.
ROCKETS_CENTER = (1800.0, -1000.0)
ROCKETS_TOP = 350.0
ROCKETS_SIZE = 400.0
ROCKET_JUMP_RUNUP = 450.0         # link start east of the platform's east edge (run-up + rise before the edge)

# Nav link conventions (Tools/editor/README.md): drop start 60 cm inside the edge, end on the floor 150 cm out.
DROP_INSET = 60.0
DROP_OUT = 150.0
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

# ADR 0005 D46 item 2: the 100 cm slot north of the ramp (ramp north face Y 1400, corridor wall Y 1500) is filled flush with
# the ramp/platform surface from X NSLOT_X0 (the ramp face is the 45 cm step height there) to the platform's east end.
NSLOT_X0 = -432.0
NSLOT_Y0 = RAMP_Y + RAMP_W / 2          # 1400
NSLOT_W = 100.0
NSLOT_FILL_TOPS = ((-432.0, -400.0, 24.0), (-400.0, -200.0, 33.0), (-200.0, 0.0, 86.0))   # (x0, x1, top); the last box ends at the ramp top

# D46 item 2: sealed D17 test room off the playable area (arena x -3000..3000; nothing leads here). Inner floor
# x 3800..5600, y 600..1700, 250 high walls. Two 100 x 1200 cm slots along X: slot A (y 1600..1700, wall on its north side,
# a 207 cm block on its south side with a 48 cm step along its west 200 cm) is the D45 slot_centre / slot_tangent bed; slot B (y 600..700, wall on its south
# side, a 207 cm block on its north side) is the old ramp_strip_wall bed. A 300 cm lane (y 1000..1300) joins the two meshed
# end zones (x 3800..4100 and 5300..5600). Keep VeloxBotFunctionalTest.cpp (VeloxBotOffMeshFt::Room*) in step.
ROOM_X0, ROOM_X1 = 3800.0, 5600.0
ROOM_Y0, ROOM_Y1 = 600.0, 1700.0
ROOM_WALL_H = 250.0
ROOM_SLOT_X0, ROOM_SLOT_X1 = 4100.0, 5300.0
ROOM_SLOT_W = 100.0
ROOM_STEP_H = 48.0
ROOM_STEP_LEN = 200.0
ROOM_BLOCK_D = 300.0
ROOM_TALL_H = 207.0

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
    "ground": 1, "wall": 4, "clip": 5, "cross": 2, "platform": 7 + 4 + 1 + 3, "ramp": 2, "roomfloor": 1, "roomwall": 4, "corridorwall": 2, "marker": 11,
    "pickup": 23, "jumppad": 2, "targetpoint": 2, "playerstart": 8, "camera": 1, "light": 2,
    "navbounds": 2,
    "navlink": 2 + 12 + 1,   # 2 JumpPad, 12 Drop (Mega 3, RA 3, rockets 4, ramp top 2), 1 RocketJump
}
MATS = {}
EAS = None
LINKS = []
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


FILL_NAV_KINDS = ("platform", "cross")   # solid boxes that must not carry nav inside them


def box(kind, label, center, size, mat, rot=None, collide=True, folder=None, mesh_path=CUBE):
    """Static mesh box. size = full extents in cm (cube/cylinder is 100 cm, so scale = size / 100)."""
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center), rot or unreal.Rotator(0, 0, 0))
    a.set_actor_label(label)
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    c = a.static_mesh_component
    c.set_editor_property("static_mesh", mesh(mesh_path))
    c.set_mobility(STATIC)
    c.set_material(0, MATS[mat])
    if kind in FILL_NAV_KINDS:
        # Solid box standing on the ground: without this Recast leaves a walkable island at floor level inside it (M4-04e).
        c.set_editor_property("fill_collision_underneath_for_navmesh", True)
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
        if name in GRID_MOVED:
            # The slot stays (marker, label) so every other grid item keeps its position.
            text(f"Label_{name.replace('.', '_')}", (x, -HALF_Y + 10, 260), f"{name}\n{GRID_MOVED[name]}", 90, folder="Labels")
        else:
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
    # Bots: pad trigger centre on the floor -> the item at the platform centre (the authored landing point).
    nav_link(f"{tag}_PadLink", unreal.VeloxNavLinkType.JUMP_PAD, (pad_x, y, 0.0), (cx, y, PLATFORM_TOP), pad=a)
    drop_links(tag, cx, y, PLATFORM_SIZE, PLATFORM_TOP, "SEW")
    box("marker", f"{tag}_PadTargetMarker", (edge, y, PLATFORM_TOP + 2), (120, 120, 4), "Target", collide=False,
        folder=tag, mesh_path=CYLINDER)


def nav_link(label, link_type, start, end, pad=None, both_ways=False, required_speed=0.0, folder="NavLinks"):
    """AVeloxNavLink at the start point (LinkStart = 0), LinkEnd relative; ADR 0005 section 4."""
    a = EAS.spawn_actor_from_class(unreal.VeloxNavLink, unreal.Vector(*start))
    a.set_actor_label(label)
    a.set_folder_path(folder)
    a.set_editor_property("type", link_type)
    a.set_editor_property("link_start", unreal.Vector(0.0, 0.0, 0.0))
    a.set_editor_property("link_end", unreal.Vector(end[0] - start[0], end[1] - start[1], end[2] - start[2]))
    a.set_editor_property("both_ways", both_ways)
    a.set_editor_property("required_speed_cm_s", required_speed)
    if pad is not None:
        a.set_editor_property("jump_pad", pad)
    count("navlink")
    LINKS.append(a)
    return a


def drop_links(tag, cx, cy, size, top, edges):
    """Drop links from the platform top (DROP_INSET inside the edge) to the floor (DROP_OUT outside), edge midpoints."""
    h = size / 2
    for e in edges:
        d = {"N": (0.0, 1.0), "S": (0.0, -1.0), "E": (1.0, 0.0), "W": (-1.0, 0.0)}[e]
        start = (cx + d[0] * (h - DROP_INSET), cy + d[1] * (h - DROP_INSET), top)
        end = (cx + d[0] * (h + DROP_OUT), cy + d[1] * (h + DROP_OUT), 0.0)
        nav_link(f"{tag}_Drop{e}", unreal.VeloxNavLinkType.DROP, start, end)


def build_rockets_platform():
    """M4 third platform: 350 cm, 400 x 400, no pad, Ammo.Rockets at its centre; reached by the RocketJump link only."""
    tag = "Rockets"
    cx, cy = ROCKETS_CENTER
    box("platform", f"{tag}_Platform", (cx, cy, ROCKETS_TOP / 2), (ROCKETS_SIZE, ROCKETS_SIZE, ROCKETS_TOP), "Platform", folder=tag)
    _, da = place_item("Ammo.Rockets", (cx, cy, ROCKETS_TOP), folder=tag)
    east = cx + ROCKETS_SIZE / 2
    text(f"Label_{tag}", (east + 60, cy, 200), respawn_text("Ammo.Rockets", da) + "\n3.5 m platform, rocket jump", 0, folder=tag)
    nav_link(f"{tag}_RocketJumpLink", unreal.VeloxNavLinkType.ROCKET_JUMP, (east + ROCKET_JUMP_RUNUP, cy, 0.0), (cx, cy, ROCKETS_TOP))
    drop_links(tag, cx, cy, ROCKETS_SIZE, ROCKETS_TOP, "NSEW")


NAV_BOUNDS_BRUSH = 200.0   # a Python-spawned NavMeshBoundsVolume gets the default 200 cm cube brush; scale = size / 200


def nav_bounds(label, center, size):
    """NavMeshBoundsVolume over the whole map (ADR 0005 section 3; bWholeWorldNavigable is not a config property in 5.8)."""
    v = EAS.spawn_actor_from_class(unreal.NavMeshBoundsVolume, unreal.Vector(*center))
    v.set_actor_label(label)
    v.set_folder_path("Navigation")
    v.set_actor_scale3d(unreal.Vector(size[0] / NAV_BOUNDS_BRUSH, size[1] / NAV_BOUNDS_BRUSH, size[2] / NAV_BOUNDS_BRUSH))
    origin, extent = v.get_actor_bounds(False)
    log(f"{label}: origin {origin} extent {extent}")
    want = unreal.Vector(size[0] / 2, size[1] / 2, size[2] / 2)
    if abs(extent.x - want.x) > 1.0 or abs(extent.y - want.y) > 1.0 or abs(extent.z - want.z) > 1.0:
        fail(f"{label}: bounds extent {extent} differs from {want} (brush is not the default 200 cm cube)")
    count("navbounds")
    return v


def build_platforms():
    build_platform_item("MegaHealth", "Health.Mega", *MEGA_PAD)
    build_platform_item("RedArmor", "Armor.Red", *RA_PAD)
    build_rockets_platform()


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
    # D41: seal the void under the ramp so no floor-level nav exists below the ramp surface (underside at the low-X end
    # of each box = (X + 600) * tan(15 deg) - thick / cos(15 deg); residual clearance < 60 cm).
    for i, (x0, x1, top) in enumerate(((-400.0, -200.0, 33.0), (-200.0, 0.0, 86.0), (0.0, run + RAMP_X0, 140.0)), start=1):
        box("platform", f"Ramp_Fill_{i}", ((x0 + x1) / 2, RAMP_Y, top / 2), (x1 - x0, RAMP_W, top), "Platform", folder="YellowArmor")
    # D46 item 2: the slot north of the ramp (Y 1400..1500) flush with the ramp and platform surface from NSLOT_X0 to the
    # platform's east end: a second thin slab in the ramp's plane (same construction, Y 100 wide), fill boxes under it, and a
    # solid block north of the platform. The surface then runs to the corridor wall and gets mesh with the ramp.
    ny = NSLOT_Y0 + NSLOT_W / 2
    zs = (NSLOT_X0 - RAMP_X0) * math.tan(rad)
    nlen = (RAMP_X0 + run - NSLOT_X0) / math.cos(rad)
    ncx = NSLOT_X0 + nlen / 2 * math.cos(rad) + thick / 2 * math.sin(rad)
    ncz = zs + nlen / 2 * math.sin(rad) - thick / 2 * math.cos(rad)
    box("ramp", "Ramp_NorthSlotSlab", (ncx, ny, ncz), (nlen, NSLOT_W, thick), "RampWalkable",
        rot=unreal.Rotator(0, RAMP_DEG, 0), folder="YellowArmor")
    for i, (x0, x1, top) in enumerate(NSLOT_FILL_TOPS + ((0.0, run + RAMP_X0, 140.0),), start=1):
        box("platform", f"Ramp_NorthFill_{i}", ((x0 + x1) / 2, ny, top / 2), (x1 - x0, NSLOT_W, top), "Platform", folder="YellowArmor")
    box("platform", "Ramp_NorthTopPlatform", (RAMP_X0 + run + 200, ny, rise / 2), (400, NSLOT_W, rise), "Platform", folder="YellowArmor")
    # Bots: east and south edges of the ramp top (north faces the corridor wall, west is the ramp).
    drop_links("RampTop", RAMP_X0 + run + 200, RAMP_Y, 400.0, rise, "SE")
    mid = RAMP_LEN / 2
    _, da = place_item("Armor.Yellow", (RAMP_X0 + mid * math.cos(rad), RAMP_Y, mid * math.sin(rad)), folder="YellowArmor")
    text("Label_YellowArmor", (RAMP_X0 - 100, RAMP_Y, 140), respawn_text("Armor.Yellow", da) + "\n15 deg ramp", 0, folder="YellowArmor")


def build_d17_room():
    """D46 item 2: sealed room with two 100 x 1200 cm slots for FtOffMesh slot_centre / slot_tangent / ramp_strip_wall."""
    t = WALL_T
    cx, cy = (ROOM_X0 + ROOM_X1) / 2, (ROOM_Y0 + ROOM_Y1) / 2
    lx, ly = ROOM_X1 - ROOM_X0, ROOM_Y1 - ROOM_Y0
    box("roomfloor", "Room_Floor", (cx, cy, -50), (lx + 2 * t + 200, ly + 2 * t + 200, 100), "Floor", folder="D17Room")
    box("roomwall", "Room_Wall_N", (cx, ROOM_Y1 + t / 2, ROOM_WALL_H / 2), (lx + 2 * t, t, ROOM_WALL_H), "Wall", folder="D17Room")
    box("roomwall", "Room_Wall_S", (cx, ROOM_Y0 - t / 2, ROOM_WALL_H / 2), (lx + 2 * t, t, ROOM_WALL_H), "Wall", folder="D17Room")
    box("roomwall", "Room_Wall_E", (ROOM_X1 + t / 2, cy, ROOM_WALL_H / 2), (t, ly, ROOM_WALL_H), "Wall", folder="D17Room")
    box("roomwall", "Room_Wall_W", (ROOM_X0 - t / 2, cy, ROOM_WALL_H / 2), (t, ly, ROOM_WALL_H), "Wall", folder="D17Room")
    sx, sl = (ROOM_SLOT_X0 + ROOM_SLOT_X1) / 2, ROOM_SLOT_X1 - ROOM_SLOT_X0
    # Slot A: the room's north wall on its north side; south of it a 207 cm block (like the ramp top) with a 48 cm step block
    # (walkable top, just above MaxStepHeight 45) along its west ROOM_STEP_LEN cm, the reproduction of the old slot's face.
    a_y1 = ROOM_Y1 - ROOM_SLOT_W
    ay = a_y1 - ROOM_BLOCK_D / 2
    box("platform", "Room_StepBlock", (ROOM_SLOT_X0 + ROOM_STEP_LEN / 2, ay, ROOM_STEP_H / 2), (ROOM_STEP_LEN, ROOM_BLOCK_D, ROOM_STEP_H), "Platform", folder="D17Room")
    box("platform", "Room_TallBlockA", ((ROOM_SLOT_X0 + ROOM_STEP_LEN + ROOM_SLOT_X1) / 2, ay, ROOM_TALL_H / 2),
        (sl - ROOM_STEP_LEN, ROOM_BLOCK_D, ROOM_TALL_H), "Platform", folder="D17Room")
    # Slot B: 207 cm block (the ramp top's height) north of the slot, the room's south wall on the other side.
    b_y0 = ROOM_Y0 + ROOM_SLOT_W
    box("platform", "Room_TallBlockB", (sx, b_y0 + ROOM_BLOCK_D / 2, ROOM_TALL_H / 2), (sl, ROOM_BLOCK_D, ROOM_TALL_H), "Platform", folder="D17Room")
    nav_bounds("NavMeshBounds_D17Room", (cx, cy, 300.0), (lx + 2 * t + 200, ly + 2 * t + 200, 1000.0))
    text("Label_D17Room", (ROOM_X0 + 5, cy, 150), "D17 test room (FtOffMesh slots)\nnot reachable in play", 0, folder="D17Room")


def build_corridor():
    length = CORRIDOR_X1 - CORRIDOR_X0
    box("corridorwall", "Corridor_Wall", ((CORRIDOR_X0 + CORRIDOR_X1) / 2, CORRIDOR_WALL_Y + WALL_T / 2, CORRIDOR_WALL_H / 2),
        (length, WALL_T, CORRIDOR_WALL_H), "Wall", folder="Corridor")
    # D46 item 2: the filled slot puts the ramp-top surface (207 cm) 43 cm under the 250 cm wall top, a climbable step: Recast
    # would mesh a sliver on the wall and a player could step onto it. A 15 cm cap over the filled stretch makes it 58 cm.
    cap_x0, cap_x1 = NSLOT_X0, RAMP_X0 + RAMP_LEN * math.cos(math.radians(RAMP_DEG)) + 400.0
    box("corridorwall", "Corridor_WallCap_RampTop", ((cap_x0 + cap_x1) / 2, CORRIDOR_WALL_Y + WALL_T / 2, CORRIDOR_WALL_H + 7.5),
        (cap_x1 - cap_x0, WALL_T, 15.0), "Wall", folder="Corridor")
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
    world = unreal.EditorLevelLibrary.get_editor_world()
    for a in PADS:
        v = a.compute_launch_velocity(VALIDATION_GRAVITY_Z)
        log(f"{a.get_actor_label()}: launch {v} |v|={v.length():.0f} cm/s")
        if v.length() <= 0.0 or v.length() > PAD_MAX_LAUNCH_SPEED:
            fail(f"{a.get_actor_label()}: launch speed {v.length():.0f} out of range")
        # Informational; the runtime check is velox.bots.DumpPadLandings on the server.
        log(f"{a.get_actor_label()}: compute_landing_point {a.compute_landing_point(world)}")


KILL_Z = -1000.0       # lowest walkable floor is Z=0; anything below this has left the map and dies via FellOutOfWorld
CLIP_H = 1800.0        # invisible clip walls / ceiling top (above any knockback apex: 1875 cm/s at 2000 cm/s^2 is about 880 cm)


def clip_box(label, center, size):
    """Invisible BlockAll volume, no navigation influence (M4-07f): seals the open-top arena against knockback."""
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center))
    a.set_actor_label(label)
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    c = a.static_mesh_component
    c.set_editor_property("static_mesh", mesh(CUBE))
    c.set_mobility(STATIC)
    c.set_collision_profile_name("BlockAll")
    c.set_editor_property("can_ever_affect_navigation", False)
    c.set_editor_property("fill_collision_underneath_for_navmesh", False)
    a.set_actor_hidden_in_game(True)
    a.set_folder_path("Clip")
    count("clip")
    return a


def build_clip(hx, hy, wall_t):
    """Four clip walls coinciding with the visible perimeter walls (inner face at +-hx/+-hy), 1800 cm high, plus a ceiling."""
    h = CLIP_H
    clip_box("Clip_N", (0, hy + wall_t / 2, h / 2), (2 * hx + 2 * wall_t, wall_t, h))
    clip_box("Clip_S", (0, -hy - wall_t / 2, h / 2), (2 * hx + 2 * wall_t, wall_t, h))
    clip_box("Clip_E", (hx + wall_t / 2, 0, h / 2), (wall_t, 2 * hy, h))
    clip_box("Clip_W", (-hx - wall_t / 2, 0, h / 2), (wall_t, 2 * hy, h))
    clip_box("Clip_Ceiling", (0, 0, h + wall_t / 2), (2 * hx + 2 * wall_t, 2 * hy + 2 * wall_t, wall_t))


def set_kill_z():
    settings = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    settings.set_editor_property("kill_z", KILL_Z)
    got = settings.get_editor_property("kill_z")
    log(f"WorldSettings.KillZ = {got}")
    if abs(got - KILL_Z) > 0.01:
        fail("could not set WorldSettings.KillZ")


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
    build_d17_room()
    build_corridor()
    build_quad()
    build_starts_camera_lights()
    # Arena plus walls, floor slab to above the platforms (bots path on walkable surfaces only).
    nav_bounds("NavMeshBounds", (0.0, 0.0, 300.0), (2 * HALF_X + 400, 2 * HALF_Y + 400, 1000.0))
    build_clip(HALF_X, HALF_Y, WALL_T)
    set_kill_z()
    set_game_mode()
    check_pads()

    # Markers: 18 grid + 11 corridor + 2 pad targets = 31; platforms: 2 item platforms + ramp top + rockets platform = 4.
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
    links = [a for a in actors if isinstance(a, unreal.VeloxNavLink)]
    kinds = {}
    for a in links:
        k = str(a.get_editor_property("type"))
        kinds[k] = kinds.get(k, 0) + 1
    log(f"VeloxNavLinks={len(links)} by type {kinds} (expected {EXPECTED['navlink']}: 2 JumpPad, 12 Drop, 1 RocketJump)")
    if len(links) != EXPECTED["navlink"]:
        fail(f"nav link count {len(links)} differs from {EXPECTED['navlink']}")
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
