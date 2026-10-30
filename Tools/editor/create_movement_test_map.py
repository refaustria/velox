"""Velox M1 greybox movement test map: /Game/Maps/L_MovementTest (ADR 0002 section 7).

Run headless:
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_movement_test_map.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: the map is deleted and rebuilt on every run; the M_Greybox_* materials are reused/re-applied.
All dimensions are centimetres (1 Q3 unit = 2.5 cm, ADR 0002 section 1). Pawn capsule is 37.5 x 70 cm.
Layout (X forward, floor top at Z=0):
  strafe corridor  x 0..5120,  y -500..500 (walls 250 high), markers every 512 cm
  ramp bank        y 1000..3800, lanes 10/20/30/45/50 deg, 1600 cm slope -> 400 cm platform
  stairs           y 4700/5300/5900, 12 steps, risers 20/40/60 cm
  jump pads        y -1500/-2500/-3500: AVeloxJumpPad (AimAtTarget) + ATargetPoint + landing marker discs
  bunny-hop gaps   y 7600/8400/9200, gaps 500/700/900 cm over a 100 cm drop; walled floor-height runways behind
                   the start platforms (run-up 1200/2500/4500 cm to x -1600/-2900/-4900, markers every 512 cm,
                   PlayerStart_Gap<n> at each runway start); pit-exit steps beside each start platform
  bot nav links    (M4, ADR 0005 section 4) AVeloxNavLink: JumpPad links for pads A/B/C (pad floor point -> landing
                   marker / ledge), Drop links at the free edges of the pad A platform and the pad C ledge, gap links
                   500 cm -> Jump (both ways), 700 cm -> JumpFast, none for 900 cm (beyond a bot's reliable speed);
                   one NavMeshBoundsVolume over the whole map
"""
import math
import unreal

ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

MAP_PATH = "/Game/Maps/L_MovementTest"
MAT_DIR = "/Game/Materials"
CUBE = "/Engine/BasicShapes/Cube.Cube"              # 100 cm cube, pivot at centre
CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder"  # 100 cm diameter, 100 cm high, pivot at centre

ERRORS = []
COUNTS = {}


def log(msg):
    unreal.log("[VeloxMoveMap] " + msg)


def fail(msg):
    unreal.log_error("[VeloxMoveMap] " + msg)
    ERRORS.append(msg)


# ---------------------------------------------------------------- materials
COLORS = {
    "Floor": (0.18, 0.18, 0.20),
    "Wall": (0.08, 0.08, 0.10),
    "MarkerA": (1.0, 1.0, 1.0),
    "MarkerB": (0.9, 0.5, 0.0),
    "RampWalkable": (0.1, 0.4, 1.0),
    "RampBlocked": (1.0, 0.1, 0.1),
    "Platform": (0.45, 0.45, 0.5),
    "Stairs": (1.0, 0.85, 0.1),
    "Pad": (0.1, 1.0, 0.2),
    "Target": (0.0, 1.0, 1.0),
    "Gap": (1.0, 0.4, 0.8),
    "Pit": (0.03, 0.03, 0.03),
}
MATS = {}


def build_materials():
    if not EAL.does_directory_exist(MAT_DIR):
        EAL.make_directory(MAT_DIR)
    for key, rgb in COLORS.items():
        name = "M_Greybox_" + key
        path = f"{MAT_DIR}/{name}"
        if EAL.does_asset_exist(path):
            mat = EAL.load_asset(path)
        else:
            mat = ATOOLS.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
        if mat is None:
            fail("could not create " + path)
            continue
        MEL.delete_all_material_expressions(mat)
        mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        expr = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector, -300, 0)
        expr.set_editor_property("constant", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1.0))
        MEL.connect_material_property(expr, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        MEL.recompile_material(mat)
        if not EAL.save_loaded_asset(mat, False):
            fail("could not save " + path)
        MATS[key] = mat


# ---------------------------------------------------------------- spawn helpers
EAS = None
STATIC = unreal.ComponentMobility.STATIC
_MESH = {}


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


def text(label, loc, msg, size=40.0, folder="Markers"):
    # Yaw 180: text faces -X, i.e. readable for a player running down +X.
    a = EAS.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(*loc), unreal.Rotator(0, 0, 180))  # (roll, pitch, yaw)
    a.set_actor_label(label)
    comp = a.text_render
    comp.set_editor_property("text", msg)
    comp.set_editor_property("world_size", size)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    a.set_folder_path(folder)
    count("text")
    return a


# ---------------------------------------------------------------- sections
def build_ground():
    # Main slab, top at Z=0: x -600..6000, y -4200..7000.
    box("ground", "Ground", (2700, 1400, -50), (6600, 11200, 100), "Floor", folder="Ground")


def build_corridor():
    L, W = 5120.0, 1000.0
    wall_h = 250.0
    for side, y in (("L", -550), ("R", 550)):
        box("wall", f"CorridorWall_{side}", (L / 2, y, wall_h / 2), (L + 200, 100, wall_h), "Wall", folder="Corridor")
    box("wall", "CorridorWall_Back", (-50, 0, wall_h / 2), (100, W, wall_h), "Wall", folder="Corridor")
    box("wall", "CorridorWall_End", (L + 50, 0, wall_h / 2), (100, W, wall_h), "Wall", folder="Corridor")
    # Distance markers every 512 cm: thin floor strips (no collision) + text labelled in Q3 units and metres.
    for i in range(11):
        x = i * 512.0
        box("marker", f"Marker_{i:02d}", (x, 0, 0.5), (20, W, 1), "MarkerA" if i % 2 == 0 else "MarkerB",
            collide=False, folder="Markers")
        text(f"MarkerText_{i:02d}", (x, -480, 140), f"{x / 2.5:.0f} u / {x / 100.0:.2f} m")


def build_ramps():
    L, T, PLAT, WID = 1600.0, 20.0, 400.0, 600.0
    for i, deg in enumerate((10, 20, 30, 45, 50)):
        y = 1000.0 + i * 700.0
        th = math.radians(deg)
        h = L * math.sin(th)
        run = L * math.cos(th)
        mat = "RampWalkable" if deg <= 45 else "RampBlocked"
        # Ramp slab: top surface runs from (0, y, 0) to (run, y, h); centre offset by half thickness along -normal.
        cx = L / 2 * math.cos(th) + T / 2 * math.sin(th)
        cz = L / 2 * math.sin(th) - T / 2 * math.cos(th)
        box("ramp", f"Ramp_{deg:02d}deg", (cx, y, cz), (L, WID, T), mat, rot=unreal.Rotator(0, deg, 0), folder="Ramps")
        box("platform", f"RampPlatform_{deg:02d}deg", (run + PLAT / 2, y, h / 2), (PLAT, WID, h), "Platform",
            folder="Ramps")
        text(f"RampText_{deg:02d}", (-100, y, 140), f"{deg} deg" + ("" if deg <= 45 else " (not walkable)"),
             folder="Ramps")


def build_stairs():
    TREAD, WID, N = 80.0, 400.0, 12
    for i, riser in enumerate((20, 40, 60)):
        y = 4700.0 + i * 600.0
        for s in range(N):
            hgt = (s + 1) * riser
            box("step", f"Stairs_{riser}cm_{s:02d}", (100 + s * TREAD + TREAD / 2, y, hgt / 2), (TREAD, WID, hgt),
                "Stairs", folder="Stairs")
        text(f"StairsText_{riser}", (-100, y, 140), f"{riser} cm risers" + (" (blocks)" if riser == 60 else ""),
             folder="Stairs")


# Jump pads (ADR 0002 sections 4 and 7). The launch point is the trigger centre, placed at the capsule centre of a
# pawn standing on the pad (capsule half height 70 cm, floor Z=0). Q3 AimAtTarget puts the apex at the target, so the
# pad A target sits 30 cm above the platform's near edge: from the pad centre the arc clears the edge by ~28 cm, lands
# at ~x 1276 and ground friction stops the pawn at ~x 1445 (platform ends at 1500). Default gravity -2000 cm/s^2 gives
# launch speeds A 1558, B 1789, C 1600 cm/s. Entering the trigger from above launches from higher up (Q3 has the same
# trigger-centre approximation, ADR 0002 section 4) and overshoots.
PAD_TRIGGER_EXTENT = (50.0, 50.0, 70.0)   # half extents; 1 m pad, as high as the standing capsule
PAD_LAUNCH_Z = 70.0                        # trigger centre height above the floor
PAD_MAX_LAUNCH_SPEED = 4000.0              # ADR 0002 section 4 default, checked below
VALIDATION_GRAVITY_Z = -2000.0             # UVeloxMovementSettings default (ADR 0002 section 1)
PADS = []

# Bot nav links (ADR 0005 section 4; conventions in Tools/editor/README.md).
DROP_INSET = 60.0          # drop start inside the platform edge
DROP_OUT = 150.0           # drop end on the floor outside the edge
GAP_TAKEOFF_INSET = 60.0   # gap link start inside the start platform edge (x 400)
GAP_LAND_INSET = 100.0     # gap link end inside the landing platform edge
GAP_LINKS = {              # gap -> (link type name, both ways, required take-off speed cm/s); 900: no link
    500: ("JUMP", True, 740.0),
    700: ("JUMP_FAST", False, 1100.0),
}
LINKS = []


def nav_link(label, link_type, start, end, pad=None, both_ways=False, required_speed=0.0, folder="NavLinks"):
    """AVeloxNavLink at the start point (LinkStart = 0), LinkEnd relative."""
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


def build_pads():
    def pad(name, x, y, target_loc):
        tp = EAS.spawn_actor_from_class(unreal.TargetPoint, unreal.Vector(*target_loc))
        tp.set_actor_label(f"JumpPad{name}_TargetPoint")
        tp.set_folder_path("JumpPads")
        count("targetpoint")

        a = EAS.spawn_actor_from_class(unreal.VeloxJumpPad, unreal.Vector(x, y, PAD_LAUNCH_Z))
        a.set_actor_label(f"JumpPad{name}")
        a.set_folder_path("JumpPads")
        a.set_editor_property("mode", unreal.VeloxJumpPadMode.AIM_AT_TARGET)
        a.set_editor_property("target", tp)
        a.set_editor_property("override_velocity", True)
        a.set_editor_property("max_launch_speed", PAD_MAX_LAUNCH_SPEED)
        trigger = a.get_editor_property("trigger")
        trigger.set_box_extent(unreal.Vector(*PAD_TRIGGER_EXTENT), False)
        # Visible pad: flat green disc on the floor, as wide as the trigger, no collision (set by the class).
        pm = a.get_editor_property("pad_mesh")
        pm.set_static_mesh(mesh(CYLINDER))
        pm.set_material(0, MATS["Pad"])
        pm.set_relative_location(unreal.Vector(0, 0, 5 - PAD_LAUNCH_Z), False, False)
        pm.set_relative_scale3d(unreal.Vector(PAD_TRIGGER_EXTENT[0] / 50.0, PAD_TRIGGER_EXTENT[1] / 50.0, 0.1))
        count("jumppad")
        PADS.append(a)
        return a

    def target(name, x, y, z):
        box("target", f"JumpPad{name}_Target", (x, y, z + 2), (120, 120, 4), "Target", collide=False,
            folder="JumpPads", mesh_path=CYLINDER)

    # A: platform 800 cm away / 320 cm up (platform x 1100..1500, top Z=320).
    pad_a = pad("A", 500, -1500, (1100, -1500, 420))
    box("platform", "JumpPadA_Platform", (1300, -1500, 160), (400, 400, 320), "Platform", folder="JumpPads")
    target("A", 1300, -1500, 320)
    nav_link("JumpPadA_Link", unreal.VeloxNavLinkType.JUMP_PAD, (500, -1500, 0), (1300, -1500, 320), pad=pad_a)
    drop_links("JumpPadA_Platform", 1300, -1500, 400, 320, "NSEW")
    text("PadA_Text", (200, -1500, 140), "Pad A: +800 fwd / +320 up", folder="JumpPads")
    # B: flat, 1600 cm (target level with the launch point: minimum-speed 45 degree arc).
    pad_b = pad("B", 500, -2500, (2100, -2500, PAD_LAUNCH_Z))
    target("B", 2100, -2500, 0)
    nav_link("JumpPadB_Link", unreal.VeloxNavLinkType.JUMP_PAD, (500, -2500, 0), (2100, -2500, 0), pad=pad_b)
    text("PadB_Text", (200, -2500, 140), "Pad B: flat 1600", folder="JumpPads")
    # C: vertical 640 cm (capsule centre rises 640 cm); ledge beside the pad, marker disc at the apex.
    pad_c = pad("C", 500, -3500, (500, -3500, PAD_LAUNCH_Z + 680))
    box("platform", "JumpPadC_Ledge", (900, -3500, 320), (400, 400, 640), "Platform", folder="JumpPads")
    target("C", 500, -3500, 680)
    # Vertical pad: the pawn steers onto the ledge after the launch (PadRun holds forward), so the link ends on the ledge.
    nav_link("JumpPadC_Link", unreal.VeloxNavLinkType.JUMP_PAD, (500, -3500, 0), (900, -3500, 640), pad=pad_c)
    # The west edge faces pad C 400 cm away: a drop there would land on the pad, so it gets no link.
    drop_links("JumpPadC_Ledge", 900, -3500, 400, 640, "NSE")
    text("PadC_Text", (200, -3500, 140), "Pad C: vertical 680", folder="JumpPads")


def check_pads():
    world = unreal.EditorLevelLibrary.get_editor_world()
    for a in PADS:
        # Informational; the runtime check is velox.bots.DumpPadLandings on the server.
        log(f"{a.get_actor_label()}: compute_landing_point {a.compute_landing_point(world)}")
        v = a.compute_launch_velocity(VALIDATION_GRAVITY_Z)
        speed = v.length()
        log(f"{a.get_actor_label()}: launch {v} |v|={speed:.0f} cm/s")
        if not (0.0 < speed <= PAD_MAX_LAUNCH_SPEED):
            fail(f"{a.get_actor_label()}: launch speed {speed:.0f} outside (0, {PAD_MAX_LAUNCH_SPEED:.0f}]")


# Runway length before each gap (cm, behind the 800 cm start platform): ADR 0002 strafe gain reaches 1.4x (1120 cm/s)
# after ~2048 cm and 1.79x by 4096 cm; required take-off speeds are ~740 / 1040 / 1330 cm/s.
GAP_RUNWAYS = {500: 1200.0, 700: 2500.0, 900: 4500.0}
GAP_LANE_Y = {500: 7600.0, 700: 8400.0, 900: 9200.0}
# Pit-exit steps: (y of the start platform edge they climb to, +1/-1 = direction they extend away from that edge).
PIT_STEPS = {500: (7800.0, 1), 700: (8600.0, 1), 900: (9400.0, 1)}
RUNWAY_WALL_H = 250.0
GAP_STARTS = []


def build_gaps():
    # Pit floor 100 cm below the platforms; start platform x -400..400, landing platform after the gap.
    box("pit", "GapPit", (1600, 8400, -150), (4400, 3000, 100), "Pit", folder="Gaps")
    for gap in (500, 700, 900):
        y = GAP_LANE_Y[gap]
        box("gapplat", f"GapStart_{gap}", (0, y, -50), (800, 400, 100), "Gap", folder="Gaps")
        land_start = 400.0 + gap
        box("gapplat", f"GapLand_{gap}", (land_start + 400, y, -50), (800, 400, 100), "Platform", folder="Gaps")
        text(f"GapText_{gap}", (-300, y, 140), f"gap {gap} cm", folder="Gaps")
        if gap in GAP_LINKS:
            kind, both, speed = GAP_LINKS[gap]
            nav_link(f"Gap{gap}_Link", getattr(unreal.VeloxNavLinkType, kind), (400.0 - GAP_TAKEOFF_INSET, y, 0.0),
                     (land_start + GAP_LAND_INSET, y, 0.0), both_ways=both, required_speed=speed)

        # Straight runway at floor height, ending where the start platform begins (x=-400), walled on both sides.
        L = GAP_RUNWAYS[gap]
        x0 = -400.0 - L
        box("runway", f"GapRunway_{gap}", (x0 + L / 2, y, -50), (L, 400, 100), "Floor", folder="Gaps")
        for side, dy in (("L", -210.0), ("R", 210.0)):
            box("gapwall", f"GapRunwayWall_{gap}_{side}", (x0 + L / 2, y + dy, RUNWAY_WALL_H / 2),
                (L, 20, RUNWAY_WALL_H), "Wall", folder="Gaps")
        box("gapwall", f"GapRunwayWall_{gap}_Back", (x0 - 10, y, RUNWAY_WALL_H / 2), (20, 440, RUNWAY_WALL_H),
            "Wall", folder="Gaps")
        text(f"GapRunwayText_{gap}", (x0 + 100, y - 150, 140), f"gap {gap} cm: run-up {L:.0f} cm",
             size=30.0, folder="Gaps")
        i = 0
        while x0 + i * 512.0 <= -400.0:
            x = x0 + i * 512.0
            box("gapmarker", f"GapMarker_{gap}_{i:02d}", (x, y, 0.5), (20, 400, 1), "MarkerA" if i % 2 == 0 else "MarkerB",
                collide=False, folder="Markers")
            text(f"GapMarkerText_{gap}_{i:02d}", (x, y - 150, 140), f"{i * 512} cm", size=30.0, folder="Markers")
            i += 1
        ps = EAS.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x0 + 120.0, y, 100))
        ps.set_actor_label(f"PlayerStart_Gap{gap}")
        count("playerstart")

        # Pit exit: four 20 cm steps (100 cm treads) in the free pit strip beside the start platform, rising from the
        # pit floor (Z=-100) to the platform edge, so a player who fell can walk back up onto the start platform.
        edge_y, d = PIT_STEPS[gap]
        for k in range(4):
            top = -20.0 - 20.0 * k
            h = top + 100.0
            box("pitstep", f"GapExit_{gap}_{k}", (0, edge_y + d * (50.0 + 100.0 * k), -100.0 + h / 2), (800, 100, h),
                "Stairs", folder="Gaps")


def build_lighting_and_starts():
    ps = EAS.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(120, 0, 100))
    ps.set_actor_label("PlayerStart_Corridor")
    count("playerstart")
    ps2 = EAS.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-300, -2500, 100))
    ps2.set_actor_label("PlayerStart_Pads")
    count("playerstart")
    sun = EAS.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 800), unreal.Rotator(0, -45, 30))
    sun.set_actor_label("Sun")
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_mobility(STATIC)
    count("light")
    sky = EAS.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 900))
    sky.set_actor_label("SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_mobility(STATIC)
    count("light")


EXPECTED = {
    "ground": 1, "wall": 4, "marker": 11, "text": 11 + 5 + 3 + 3 + 3 + 3 + 17,
    "ramp": 5, "platform": 5 + 2,  # 5 ramp tops + pad A platform + pad C ledge
    "step": 36, "jumppad": 3, "targetpoint": 3, "target": 3, "pit": 1, "gapplat": 6,
    "runway": 3, "gapwall": 9, "gapmarker": 17, "pitstep": 12,
    "playerstart": 5, "light": 2,
    "navbounds": 1,
    "navlink": 3 + 4 + 3 + 2,  # pad links A/B/C, drops (pad A platform 4, pad C ledge 3), gaps 500/700
}


def build_map():
    global EAS
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not EAL.does_directory_exist("/Game/Maps"):
        EAL.make_directory("/Game/Maps")
    if EAL.does_asset_exist(MAP_PATH):
        # new_level refuses an existing destination and delete_asset leaves the package registered, so
        # rebuild in place: load the map and destroy everything in it.
        log("map exists, clearing it for rebuild")
        if not les.load_level(MAP_PATH):
            fail("could not load existing " + MAP_PATH)
            return
        for a in EAS.get_all_level_actors():
            EAS.destroy_actor(a)
    elif not les.new_level(MAP_PATH, False):
        fail("new_level failed for " + MAP_PATH)
        return

    build_ground()
    build_corridor()
    build_ramps()
    build_stairs()
    build_pads()
    build_gaps()
    build_lighting_and_starts()
    # Everything: ground slab, gap runways (x to -4910), pit and gap lanes (y to 9900), ramp tops (z to ~1230).
    nav_bounds("NavMeshBounds", (500.0, 2850.0, 600.0), (11200.0, 14500.0, 1800.0))
    check_pads()

    log("spawned: " + ", ".join(f"{k}={v}" for k, v in sorted(COUNTS.items())))
    if COUNTS != EXPECTED:
        fail(f"actor counts differ from expected: got {COUNTS}, expected {EXPECTED}")
    level_actors = EAS.get_all_level_actors()
    n_level = len(level_actors)
    n_spawned = sum(COUNTS.values())
    log(f"level actors in world: {n_level} (spawned by script: {n_spawned})")
    if n_level < n_spawned:
        fail(f"level has fewer actors ({n_level}) than spawned ({n_spawned})")
    n_sm = sum(1 for a in level_actors if isinstance(a, unreal.StaticMeshActor))
    n_ps = sum(1 for a in level_actors if isinstance(a, unreal.PlayerStart))
    exp_sm = sum(v for k, v in EXPECTED.items() if k not in ("text", "playerstart", "light", "jumppad", "targetpoint", "navlink", "navbounds"))
    n_pads = sum(1 for a in level_actors if isinstance(a, unreal.VeloxJumpPad))
    n_tp = sum(1 for a in level_actors if isinstance(a, unreal.TargetPoint))
    n_links = sum(1 for a in level_actors if isinstance(a, unreal.VeloxNavLink))
    log(f"VeloxNavLinks={n_links} (expected {EXPECTED['navlink']})")
    if n_links != EXPECTED["navlink"]:
        fail(f"nav link count {n_links} differs from {EXPECTED['navlink']}")
    log(f"StaticMeshActors={n_sm} (expected {exp_sm}) PlayerStarts={n_ps} (expected 5) "
        f"VeloxJumpPads={n_pads} (expected 3) TargetPoints={n_tp} (expected 3)")
    if n_sm != exp_sm or n_ps != 5 or n_pads != 3 or n_tp != 3:
        fail("level actor class counts mismatch")
    # Map check runs AVeloxJumpPad::CheckForErrors (launch speed validation); the log shows its summary line.
    unreal.SystemLibrary.execute_console_command(EAS.get_editor_world() if hasattr(EAS, "get_editor_world")
                                                 else unreal.EditorLevelLibrary.get_editor_world(), "MAP CHECK")
    if not les.save_current_level():
        fail("save_current_level failed")


def main():
    build_materials()
    if not ERRORS:
        build_map()
    if ERRORS:
        raise RuntimeError("movement test map failed: " + "; ".join(ERRORS))
    log("done")


main()
