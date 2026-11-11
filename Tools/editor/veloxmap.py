"""Shared map-generation library for the Velox editor scripts (ADR 0006 section 2).

Imported by create_movement_test_map.py, create_combat_test_map.py, create_item_test_map.py (and later create_dm1_map.py):

    import os, sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import veloxmap as vm

State is module level (one map per editor process): the editor actor subsystem, the per-kind actor COUNTS, the loaded greybox
materials and the ERRORS list. Call configure() first, then greybox_materials(), then rebuild_map(), spawn with the helpers,
assert_counts(), save_map() and finally raise_if_errors(). Centimetres, floor top at Z=0, X forward.

Every actor spawned through a helper carries the actor tag "Layout:<name>" (name = the helper's `name`, the actor label, unless
`layout=` overrides it) so lint and audit output can speak the layout's language.
"""
import math
import unreal

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary

CUBE = "/Engine/BasicShapes/Cube.Cube"              # 100 cm cube, pivot at centre
CYLINDER = "/Engine/BasicShapes/Cylinder.Cylinder"  # 100 cm diameter, 100 cm high, pivot at centre
MAT_DIR = "/Game/Materials"
STATIC = unreal.ComponentMobility.STATIC

# Greybox colours (flat unlit emissive, M_Greybox_<key>).
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

# Nav link conventions (Tools/editor/README.md, ADR 0005 section 4).
DROP_INSET = 60.0          # drop start inside the platform edge
DROP_OUT = 150.0           # drop end on the floor outside the edge
NAV_BOUNDS_BRUSH = 200.0   # a Python-spawned NavMeshBoundsVolume gets the default 200 cm cube brush; scale = size / 200

# Jump pads (ADR 0002 sections 4 and 7).
PAD_LAUNCH_Z = 70.0                        # trigger centre height above the floor
PAD_TRIGGER_EXTENT = (50.0, 50.0, 70.0)    # half extents; 1 m pad, as high as the standing capsule
PAD_MAX_LAUNCH_SPEED = 4000.0
VALIDATION_GRAVITY_Z = -2000.0             # UVeloxMovementSettings default

CLIP_H = 1800.0        # invisible clip walls / ceiling top (above any knockback apex: about 880 cm)

# Module state.
LOG_PREFIX = "[VeloxMap]"
FILL_NAV_KINDS = ()    # kinds of solid boxes that must not carry nav inside them (M4-04e); set per map via configure()
ERRORS = []
COUNTS = {}
MATS = {}
PADS = []
LINKS = []
EAS = None
_MESH = {}


def configure(log_prefix=None, fill_nav_kinds=None):
    global LOG_PREFIX, FILL_NAV_KINDS
    if log_prefix is not None:
        LOG_PREFIX = log_prefix
    if fill_nav_kinds is not None:
        FILL_NAV_KINDS = tuple(fill_nav_kinds)


def log(msg):
    unreal.log(LOG_PREFIX + " " + msg)


def fail(msg):
    unreal.log_error(LOG_PREFIX + " " + msg)
    ERRORS.append(msg)


def raise_if_errors(what):
    if ERRORS:
        raise RuntimeError(what + " failed: " + "; ".join(ERRORS))


def count(kind, n=1):
    COUNTS[kind] = COUNTS.get(kind, 0) + n


def mesh(path):
    if path not in _MESH:
        _MESH[path] = unreal.load_asset(path)
    return _MESH[path]


# ---------------------------------------------------------------- tags and generic spawn
def tag_actor(actor, name, layout=None, tags=()):
    """Add Layout:<name> (or Layout:<layout>) plus any extra tags to the actor, keeping tags it already has."""
    have = [str(t) for t in actor.get_editor_property("tags")]
    for t in (f"Layout:{layout or name}",) + tuple(tags):
        if t not in have:
            have.append(t)
    actor.set_editor_property("tags", [unreal.Name(t) for t in have])


def spawn(cls, name, location, rot=None, folder=None, kind=None, tags=(), layout=None):
    """spawn_actor_from_class + label + folder + Layout tag (+ count under `kind` when given)."""
    a = EAS.spawn_actor_from_class(cls, unreal.Vector(*location), rot or unreal.Rotator(0, 0, 0))
    a.set_actor_label(name)
    if folder:
        a.set_folder_path(folder)
    tag_actor(a, name, layout, tags)
    if kind:
        count(kind)
    return a


# ---------------------------------------------------------------- materials
def greybox_materials(create=False, keys=None):
    """Fill MATS. create=True (re)builds every M_Greybox_* material; otherwise loads `keys` (default: all) and fails on a miss."""
    if create:
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
        return MATS
    for key in (keys or COLORS.keys()):
        path = f"{MAT_DIR}/M_Greybox_{key}"
        mat = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if mat is None:
            fail(f"missing {path}: run create_movement_test_map.py first")
        MATS[key] = mat
    return MATS


# ---------------------------------------------------------------- map lifecycle
def rebuild_map(path):
    """Load `path` and destroy everything in it, or create it. Returns the LevelEditorSubsystem (None on failure).
    (new_level refuses an existing destination and delete_asset leaves the package registered, so rebuild in place.)"""
    global EAS
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    COUNTS.clear()
    PADS.clear()
    LINKS.clear()
    folder = path.rsplit("/", 1)[0]
    if not EAL.does_directory_exist(folder):
        EAL.make_directory(folder)
    if EAL.does_asset_exist(path):
        log("map exists, clearing it for rebuild")
        if not les.load_level(path):
            fail("could not load existing " + path)
            return None
        for a in EAS.get_all_level_actors():
            EAS.destroy_actor(a)
    elif not les.new_level(path, False):
        fail("new_level failed for " + path)
        return None
    return les


def assert_counts(expected, exact=True):
    """Log the per-kind counts line and compare with `expected` (exact: whole dict; otherwise only the keys in `expected`)."""
    log("spawned: " + ", ".join(f"{k}={v}" for k, v in sorted(COUNTS.items())))
    if exact:
        if COUNTS != expected:
            fail(f"actor counts differ from expected: got {COUNTS}, expected {expected}")
    else:
        for k, v in expected.items():
            if COUNTS.get(k, 0) != v:
                fail(f"{k}: spawned {COUNTS.get(k, 0)}, expected {v}")


def save_map(les, map_check=True):
    """MAP CHECK (runs AVeloxJumpPad::CheckForErrors) and save the current level."""
    if map_check:
        unreal.SystemLibrary.execute_console_command(unreal.EditorLevelLibrary.get_editor_world(), "MAP CHECK")
    if not les.save_current_level():
        fail("save_current_level failed")


# ---------------------------------------------------------------- geometry
def box(name, center, size, material, kind, rot=None, collide=True, tags=(), folder=None, mesh_path=CUBE,
        fill_nav=None, layout=None, clip=False):
    """Static mesh box. size = full extents in cm (cube/cylinder is 100 cm, so scale = size / 100).
    clip=True (art mode, ADR 0006 section 2): the same box as invisible collision: no material, hidden in game, not rendered, no shadow,
    collision and navigation influence exactly as the visible box (BlockAll, same fill_collision_underneath_for_navmesh)."""
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*center), rot or unreal.Rotator(0, 0, 0))
    a.set_actor_label(name)
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    c = a.static_mesh_component
    c.set_editor_property("static_mesh", mesh(mesh_path))
    c.set_mobility(STATIC)
    if clip:
        a.set_actor_hidden_in_game(True)
        c.set_editor_property("hidden_in_game", True)
        c.set_editor_property("visible", False)
        c.set_editor_property("cast_shadow", False)
    else:
        c.set_material(0, MATS[material])
    if fill_nav is None:
        fill_nav = kind in FILL_NAV_KINDS
    if fill_nav:
        # Solid box standing on the ground: without this Recast leaves a walkable island at floor level inside it (M4-04e).
        c.set_editor_property("fill_collision_underneath_for_navmesh", True)
    if not collide:
        c.set_collision_profile_name("NoCollision")
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    if folder:
        a.set_folder_path(folder)
    tag_actor(a, name, layout, tags)
    count(kind)
    return a


def ramp(name, origin, length, width, thickness, deg, material, kind="ramp", tags=(), folder=None, layout=None, clip=False):
    """Thin slab whose top surface runs from `origin` (x, y, z) rising along +X at `deg` degrees over `length` cm
    (the slab centre is offset half a thickness along -normal)."""
    th = math.radians(deg)
    cx = origin[0] + length / 2 * math.cos(th) + thickness / 2 * math.sin(th)
    cz = origin[2] + length / 2 * math.sin(th) - thickness / 2 * math.cos(th)
    return box(name, (cx, origin[1], cz), (length, width, thickness), material, kind, rot=unreal.Rotator(0, deg, 0),
               tags=tags, folder=folder, layout=layout, clip=clip)


def stairs(name, x0, y, tread, width, riser, steps, material, kind="step", tags=(), folder=None, layout=None):
    """Solid steps rising along +X from x0: step s spans x0 + s*tread .. +tread and is (s+1)*riser high. Names <name>_<nn>."""
    out = []
    for s in range(steps):
        hgt = (s + 1) * riser
        out.append(box(f"{name}_{s:02d}", (x0 + s * tread + tread / 2, y, hgt / 2), (tread, width, hgt), material, kind,
                       tags=tags, folder=folder, layout=layout))
    return out


def cylinder_marker(name, center, size, material, kind="target", tags=(), folder=None, layout=None):
    """Non-colliding cylinder disc (target / landing markers)."""
    return box(name, center, size, material, kind, collide=False, tags=tags, folder=folder, mesh_path=CYLINDER, layout=layout)


def text_label(name, location, msg, yaw=180.0, size=40.0, folder="Markers", tags=(), layout=None):
    """TextRenderActor; its front faces local +X, so yaw 180 reads from a player running down +X, 90 faces +Y."""
    a = spawn(unreal.TextRenderActor, name, location, unreal.Rotator(0, 0, yaw), folder, "text", tags, layout)  # (roll, pitch, yaw)
    comp = a.text_render
    comp.set_editor_property("text", msg)
    comp.set_editor_property("world_size", size)
    comp.set_editor_property("horizontal_alignment", unreal.HorizTextAligment.EHTA_CENTER)
    return a


# ---------------------------------------------------------------- gameplay actors
def player_start(name, location, yaw=0.0, folder=None, tags=(), layout=None):
    return spawn(unreal.PlayerStart, name, location, unreal.Rotator(0, 0, yaw), folder, "playerstart", tags, layout)


def pickup(definition_path, location, tags=(), name=None, folder="Items", layout=None):
    """AVeloxPickup with its origin on the floor point (ADR 0004 section 1). Returns (actor, definition asset or None)."""
    da = EAL.load_asset(definition_path) if EAL.does_asset_exist(definition_path) else None
    if da is None:
        fail(f"missing {definition_path}: run create_item_content.py first")
    label = name or ("Pickup_" + definition_path.rsplit("/", 1)[-1].replace("DA_Item_", ""))
    a = spawn(unreal.VeloxPickup, label, location, None, folder, "pickup", tags, layout)
    if da is not None:
        a.set_editor_property("definition", da)
    return a, da


def jump_pad(name, location_xy, target_location, pad_label=None, target_label=None, folder=None, tags=(), layout=None,
             trigger_extent=None):
    """AVeloxJumpPad (AimAtTarget) with its trigger centre PAD_LAUNCH_Z above the floor, a TargetPoint at target_location
    and the visible green disc. Returns the pad.
    trigger_extent (task m6-16): half extents (x, y, z) of a lower/narrower trigger. The box then rests on the floor
    (actor z = extent z), so a pawn only fires the pad while its feet are within 2 x extent z of the floor (a pawn
    hopping through the old 140 cm box launched from up to 140 cm high and overshot). The caller shifts target_location
    z by (extent z - PAD_LAUNCH_Z) to keep the launch velocity (launch origin = trigger centre)."""
    ext = tuple(trigger_extent) if trigger_extent else PAD_TRIGGER_EXTENT
    launch_z = ext[2] if trigger_extent else PAD_LAUNCH_Z
    tp = spawn(unreal.TargetPoint, target_label or f"{name}_TargetPoint", target_location, None, folder, "targetpoint", tags, layout)
    a = spawn(unreal.VeloxJumpPad, pad_label or name, (location_xy[0], location_xy[1], launch_z), None, folder, "jumppad",
              tags, layout)
    a.set_editor_property("mode", unreal.VeloxJumpPadMode.AIM_AT_TARGET)
    a.set_editor_property("target", tp)
    a.set_editor_property("override_velocity", True)
    a.set_editor_property("max_launch_speed", PAD_MAX_LAUNCH_SPEED)
    a.get_editor_property("trigger").set_box_extent(unreal.Vector(*ext), False)
    # Visible pad: flat green disc on the floor, as wide as the trigger, no collision (set by the class).
    pm = a.get_editor_property("pad_mesh")
    pm.set_static_mesh(mesh(CYLINDER))
    pm.set_material(0, MATS["Pad"])
    pm.set_relative_location(unreal.Vector(0, 0, 5 - launch_z), False, False)
    pm.set_relative_scale3d(unreal.Vector(ext[0] / 50.0, ext[1] / 50.0, 0.1))
    PADS.append(a)
    return a


def check_pads(log_landing=True):
    """Launch speed validation of every pad spawned by jump_pad(); landing points are informational
    (the runtime check is velox.bots.DumpPadLandings on the server)."""
    world = unreal.EditorLevelLibrary.get_editor_world()
    for a in PADS:
        label = a.get_actor_label()
        if log_landing:
            log(f"{label}: compute_landing_point {a.compute_landing_point(world)}")
        v = a.compute_launch_velocity(VALIDATION_GRAVITY_Z)
        speed = v.length()
        log(f"{label}: launch {v} |v|={speed:.0f} cm/s")
        if not (0.0 < speed <= PAD_MAX_LAUNCH_SPEED):
            fail(f"{label}: launch speed {speed:.0f} outside (0, {PAD_MAX_LAUNCH_SPEED:.0f}]")


# ---------------------------------------------------------------- navigation
def nav_bounds(center, size, name="NavMeshBounds", tags=(), layout=None):
    """NavMeshBoundsVolume over the whole map (ADR 0005 section 3; bWholeWorldNavigable is not a config property in 5.8)."""
    v = spawn(unreal.NavMeshBoundsVolume, name, center, None, "Navigation", None, tags, layout)
    v.set_actor_scale3d(unreal.Vector(size[0] / NAV_BOUNDS_BRUSH, size[1] / NAV_BOUNDS_BRUSH, size[2] / NAV_BOUNDS_BRUSH))
    origin, extent = v.get_actor_bounds(False)
    log(f"{name}: origin {origin} extent {extent}")
    want = unreal.Vector(size[0] / 2, size[1] / 2, size[2] / 2)
    if abs(extent.x - want.x) > 1.0 or abs(extent.y - want.y) > 1.0 or abs(extent.z - want.z) > 1.0:
        fail(f"{name}: bounds extent {extent} differs from {want} (brush is not the default 200 cm cube)")
    count("navbounds")
    return v


def nav_link(name, link_type, start, end, pad=None, both_ways=False, required_speed=0.0, folder="NavLinks", tags=(), layout=None):
    """AVeloxNavLink at the start point (LinkStart = 0), LinkEnd relative to it (no rotation)."""
    a = spawn(unreal.VeloxNavLink, name, start, None, folder, "navlink", tags, layout)
    a.set_editor_property("type", link_type)
    a.set_editor_property("link_start", unreal.Vector(0.0, 0.0, 0.0))
    a.set_editor_property("link_end", unreal.Vector(end[0] - start[0], end[1] - start[1], end[2] - start[2]))
    a.set_editor_property("both_ways", both_ways)
    a.set_editor_property("required_speed_cm_s", required_speed)
    if pad is not None:
        a.set_editor_property("jump_pad", pad)
    LINKS.append(a)
    return a


def drop_links(tag, cx, cy, size, top, edges):
    """Drop links from the platform top (DROP_INSET inside the edge) to the floor (DROP_OUT outside), edge midpoints.
    `edges` is a string of N/S/E/W; the links are named <tag>_Drop<edge>."""
    h = size / 2
    for e in edges:
        d = {"N": (0.0, 1.0), "S": (0.0, -1.0), "E": (1.0, 0.0), "W": (-1.0, 0.0)}[e]
        start = (cx + d[0] * (h - DROP_INSET), cy + d[1] * (h - DROP_INSET), top)
        end = (cx + d[0] * (h + DROP_OUT), cy + d[1] * (h + DROP_OUT), 0.0)
        nav_link(f"{tag}_Drop{e}", unreal.VeloxNavLinkType.DROP, start, end)


# ---------------------------------------------------------------- map-wide settings
def clip_box(name, center, size):
    """Invisible BlockAll volume, no navigation influence (M4-07f): seals an open-top arena against knockback."""
    a = spawn(unreal.StaticMeshActor, name, center, None, "Clip", "clip")
    a.set_actor_scale3d(unreal.Vector(size[0] / 100.0, size[1] / 100.0, size[2] / 100.0))
    c = a.static_mesh_component
    c.set_editor_property("static_mesh", mesh(CUBE))
    c.set_mobility(STATIC)
    c.set_collision_profile_name("BlockAll")
    c.set_editor_property("can_ever_affect_navigation", False)
    c.set_editor_property("fill_collision_underneath_for_navmesh", False)
    a.set_actor_hidden_in_game(True)
    return a


def clip_walls(half_x, half_y, height=CLIP_H, ceiling_z=None, wall_t=100.0):
    """Four clip walls coinciding with the visible perimeter walls (inner face at +-half_x / +-half_y), `height` high,
    plus a ceiling slab (centre Z `ceiling_z`, default height + wall_t / 2, i.e. sitting on the walls). 5 actors, kind "clip"."""
    h, t = height, wall_t
    clip_box("Clip_N", (0, half_y + t / 2, h / 2), (2 * half_x + 2 * t, t, h))
    clip_box("Clip_S", (0, -half_y - t / 2, h / 2), (2 * half_x + 2 * t, t, h))
    clip_box("Clip_E", (half_x + t / 2, 0, h / 2), (t, 2 * half_y, h))
    clip_box("Clip_W", (-half_x - t / 2, 0, h / 2), (t, 2 * half_y, h))
    clip_box("Clip_Ceiling", (0, 0, h + t / 2 if ceiling_z is None else ceiling_z), (2 * half_x + 2 * t, 2 * half_y + 2 * t, t))


def set_kill_z(z):
    """WorldSettings.KillZ: below this a pawn has left the map and dies via FellOutOfWorld."""
    settings = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    settings.set_editor_property("kill_z", z)
    got = settings.get_editor_property("kill_z")
    log(f"WorldSettings.KillZ = {got}")
    if abs(got - z) > 0.01:
        fail("could not set WorldSettings.KillZ")


def static_sun_and_sky():
    """Static directional light and sky light (baked lighting, ADR 0001). 2 actors, kind "light"."""
    sun = spawn(unreal.DirectionalLight, "Sun", (0, 0, 800), unreal.Rotator(0, -45, 30), None, "light")
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_mobility(STATIC)
    sky = spawn(unreal.SkyLight, "SkyLight", (0, 0, 900), None, None, "light")
    sky.get_component_by_class(unreal.SkyLightComponent).set_mobility(STATIC)


# ---------------------------------------------------------------- art mode helpers (task M5-15)
def kit_piece(name, mesh_path, location, yaw=0.0, pitch=0.0, scale=(1.0, 1.0, 1.0), layout=None, lm_res=None, folder="Kit"):
    """Visual-only kit mesh instance: NoCollision, no navigation influence, Static, optional lightmap resolution override.
    Counted under kind "kit". Unreal's Rotator argument order is (roll, pitch, yaw)."""
    a = EAS.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location), unreal.Rotator(0.0, pitch, yaw))
    a.set_actor_label(name)
    a.set_actor_scale3d(unreal.Vector(*scale))
    c = a.static_mesh_component
    m = mesh(mesh_path)
    if m is None:
        fail("missing kit mesh " + mesh_path)
        return a
    c.set_editor_property("static_mesh", m)
    c.set_mobility(STATIC)
    # The kit meshes already default to NoCollision, so setting it again is a no-op and is not serialised: a reloaded component
    # would fall back to the actor class default (BlockAll). Force a real change first, then set the final value.
    c.set_collision_profile_name("BlockAll")
    c.set_collision_profile_name("NoCollision")
    c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    c.set_editor_property("can_ever_affect_navigation", False)
    if lm_res:
        c.set_editor_property("override_light_map_res", True)
        c.set_editor_property("overridden_light_map_res", int(lm_res))
    a.set_folder_path(folder)
    tag_actor(a, name, layout)
    count("kit")
    return a


def _srgb8(rgb):
    return unreal.Color(*[max(0, min(255, int(round(c * 255.0)))) for c in rgb], 255)


def static_point_light(name, location, lumens, rgb, radius, layout=None, folder="Lights"):
    """Static (baked) point light in lumens. Counted under kind "pointlight"."""
    a = spawn(unreal.PointLight, name, location, None, folder, "pointlight", (), layout)
    comp = a.get_component_by_class(unreal.PointLightComponent)
    comp.set_mobility(STATIC)
    comp.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
    comp.set_editor_property("intensity", float(lumens))
    comp.set_editor_property("light_color", _srgb8(rgb))
    comp.set_editor_property("attenuation_radius", float(radius))
    comp.set_editor_property("cast_shadows", True)
    return a


def static_spot_light(name, location, rotation, lumens, rgb, radius, outer_cone=44.0, layout=None, folder="Lights"):
    """Static spot light; rotation = (roll, pitch, yaw). Counted under kind "spotlight"."""
    a = spawn(unreal.SpotLight, name, location, unreal.Rotator(*rotation), folder, "spotlight", (), layout)
    comp = a.get_component_by_class(unreal.SpotLightComponent)
    comp.set_mobility(STATIC)
    comp.set_editor_property("intensity_units", unreal.LightUnits.LUMENS)
    comp.set_editor_property("intensity", float(lumens))
    comp.set_editor_property("light_color", _srgb8(rgb))
    comp.set_editor_property("attenuation_radius", float(radius))
    comp.set_editor_property("outer_cone_angle", float(outer_cone))
    return a


def sphere_reflection_capture(name, location, radius, layout=None, folder="Lights"):
    """SphereReflectionCapture (the capture resolution is the project's r.ReflectionCaptureResolution). Kind "reflection"."""
    a = spawn(unreal.SphereReflectionCapture, name, location, None, folder, "reflection", (), layout)
    comp = a.get_component_by_class(unreal.SphereReflectionCaptureComponent)
    comp.set_editor_property("influence_radius", float(radius))
    return a


def sky_light(cubemap_path, name="SkyLight", location=(0, 0, 900)):
    """Static SkyLight with a specified cubemap (TC_Sky_DM1). Kind "skylight"."""
    a = spawn(unreal.SkyLight, name, location, None, None, "skylight")
    comp = a.get_component_by_class(unreal.SkyLightComponent)
    comp.set_mobility(STATIC)
    cube = unreal.load_asset(cubemap_path)
    if cube is None:
        fail("missing sky cubemap " + cubemap_path)
    else:
        comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        comp.set_editor_property("cubemap", cube)
    return a


def lightmass_importance_volume(center, size, name="LightmassImportance"):
    """LightmassImportanceVolume over the playable space (scale = size / default 200 cm brush). Kind "lightmassvolume"."""
    v = spawn(unreal.LightmassImportanceVolume, name, center, None, "Lights", "lightmassvolume")
    v.set_actor_scale3d(unreal.Vector(size[0] / NAV_BOUNDS_BRUSH, size[1] / NAV_BOUNDS_BRUSH, size[2] / NAV_BOUNDS_BRUSH))
    origin, extent = v.get_actor_bounds(False)
    log(f"{name}: origin {origin} extent {extent}")
    return v


def lightmass_world_settings(bounces=3, quality=1.0, level_scale=1.0, compress=True, vlm_cell=200.0):
    """ADR 0006 section 5: 3 bounces, indirect quality 1, level scale 1, compressed lightmaps, volumetric lightmap detail cell 200."""
    ws = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    ls = ws.get_editor_property("lightmass_settings")
    ls.set_editor_property("num_indirect_lighting_bounces", int(bounces))
    ls.set_editor_property("indirect_lighting_quality", float(quality))
    ls.set_editor_property("static_lighting_level_scale", float(level_scale))
    ls.set_editor_property("compress_lightmaps", bool(compress))
    ls.set_editor_property("volumetric_lightmap_detail_cell_size", float(vlm_cell))
    ws.set_editor_property("lightmass_settings", ls)
    got = ws.get_editor_property("lightmass_settings")
    log("Lightmass: bounces=%s quality=%s scale=%s compress=%s vlm_cell=%s" % (
        got.get_editor_property("num_indirect_lighting_bounces"), got.get_editor_property("indirect_lighting_quality"),
        got.get_editor_property("static_lighting_level_scale"), got.get_editor_property("compress_lightmaps"),
        got.get_editor_property("volumetric_lightmap_detail_cell_size")))
    if int(got.get_editor_property("num_indirect_lighting_bounces")) != int(bounces) or abs(got.get_editor_property("volumetric_lightmap_detail_cell_size") - vlm_cell) > 0.01:
        fail("could not set the Lightmass world settings")
