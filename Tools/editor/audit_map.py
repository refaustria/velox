"""Static audit of a Velox map: performance budgets, collision diff, protrusion, lighting-built (ADR 0006 sections 2, 5, 8, E0.1 (d)).

Run headless with the pythonscript commandlet (arguments follow the script path inside the quoted -script value):

    UnrealEditor-Cmd Velox.uproject -run=pythonscript -script="<abs>/Tools/editor/audit_map.py /Game/Maps/DM1/L_DM1 --protrusion --lighting" \
        -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

or through the wrapper `infra/ci/audit.sh <Map> [options]`, which also turns the result into an exit code.

    audit_map.py <Map> [--diff-collision A B] [--protrusion] [--lighting] [--json out]

Output: a table on stdout (every line prefixed `[VeloxAudit]`), `Saved/Audit/<MapShortName>.json` (or --json), and a final
`[VeloxAudit] RESULT PASS|FAIL`. The process exit code of the commandlet is not reliable across engine versions, so the wrapper
parses the RESULT line. The pure helpers (budget evaluation, collision diff, protrusion, texture size estimate) do not import
`unreal` and are unit-testable with `python3 -I Tools/editor/audit_map.py --selftest`.

Figures (all static, from asset data; GPU-side numbers are measured on the Mac, ADR 0006 section 8):
  draw_calls_est     sum over visible static mesh components of their material slots (upper bound: nothing culled, nothing batched)
  triangles_total    sum of LOD0 triangles over visible static mesh components (upper bound for "in view")
  lightmap_mb        sum over lightmapped components of res^2 * 4 B * 1.3
  texture_mb         every texture referenced by the materials used on the map, once each, mips included (compressed estimate)
  lm_gt64            lightmapped components with lightmap resolution > 64
  translucent        components using a translucent/additive material (exempt: pickups, material names containing Glow or Beam)
  kit_meshes / materials  distinct static meshes / materials; per-mesh materials checked against the kit rule
"""
import json
import math
import os
import subprocess
import sys

try:
    import unreal  # noqa: F401  (absent in --selftest)
    HAVE_UNREAL = True
except ImportError:
    HAVE_UNREAL = False

LOG = "[VeloxAudit]"

# ADR 0006 section 8. L_DM1 and L_DM1_Blockout are judged against the full table; the small test maps against the same limits
# (they are far below them, so a breach there means a real regression).
BUDGET = {
    "draw_calls_est": 600,
    "triangles_total": 300000,
    "lightmap_mb": 48.0,
    "texture_mb": 256.0,
    "lm_gt64": 120,
    "kit_meshes": 25,           # distinct kit meshes (SM_Kit_*) used by the map
    "materials_per_kit_mesh": 8,
    "translucent": 0,
    "lfs_mb": 600.0,            # section 1: new LFS objects for M5; informational for the repo total, not a map breach
}
LIGHTMAP_BYTES = 4 * 1.3
PROTRUSION_LIMIT_CM = 5.0
PROTRUSION_CEILING_CM = 250.0   # above this height over a walkable surface a pawn cannot touch the mesh
COLLISION_TOL_CM = 1.0
TRANSLUCENT_EXEMPT_ACTOR_CLASSES = ("VeloxPickup", "VeloxLightningBeam")
TRANSLUCENT_EXEMPT_MATERIAL_WORDS = ("glow", "beam")
# Actor classes that carry no gameplay and are ignored by the collision diff.
VISUAL_ACTOR_CLASSES = (
    "StaticMeshActor", "TextRenderActor", "PointLight", "SpotLight", "RectLight", "DirectionalLight", "SkyLight", "SphereReflectionCapture",
    "BoxReflectionCapture", "PlanarReflectionCapture", "SkyAtmosphere", "ExponentialHeightFog", "PostProcessVolume",
    "VolumetricLightmapDensityVolume", "LightmassImportanceVolume", "LightmassCharacterIndirectDetailVolume", "WorldSettings",
    "Brush", "DefaultPhysicsVolume", "Note", "DecalActor", "AudioVolume", "AmbientSound", "SkyDome",
)


def log(msg):
    line = "%s %s" % (LOG, msg)
    if HAVE_UNREAL:
        unreal.log(line)
    else:
        print(line)


# ---------------------------------------------------------------------------------------------------------------------
# Pure helpers (no unreal import)
# ---------------------------------------------------------------------------------------------------------------------
def texture_bytes(width, height, bytes_per_pixel):
    """Mip chain included (x 4/3)."""
    return width * height * bytes_per_pixel * 4.0 / 3.0


def lightmap_mb(resolutions):
    return sum(r * r * LIGHTMAP_BYTES for r in resolutions) / (1024.0 * 1024.0)


def evaluate_budgets(fig, budget=BUDGET):
    """fig: dict of figures. Returns the list of breach strings (empty = within budget)."""
    out = []
    for key in ("draw_calls_est", "triangles_total", "lightmap_mb", "texture_mb", "lm_gt64", "kit_meshes", "translucent"):
        if fig.get(key, 0) > budget[key]:
            out.append("%s %s > %s" % (key, _fmt(fig[key]), _fmt(budget[key])))
    for mesh, nmats in sorted(fig.get("materials_per_mesh", {}).items()):
        if nmats > budget["materials_per_kit_mesh"]:
            out.append("mesh %s has %d materials > %d" % (mesh, nmats, budget["materials_per_kit_mesh"]))
    return out


def _fmt(v):
    return ("%.2f" % v) if isinstance(v, float) else str(v)


def oriented_box(center, half, rot_deg):
    """(center, half extents, yaw/pitch/roll in degrees as unreal.Rotator order pitch,yaw,roll) -> dict with an inverse rotation."""
    return {"c": tuple(center), "h": tuple(half), "r": tuple(rot_deg)}


def _rot_matrix(pitch, yaw, roll):
    """Unreal rotation (pitch about Y, yaw about Z, roll about X), world <- local, as a 3x3 row-major list."""
    p, y, r = math.radians(pitch), math.radians(yaw), math.radians(roll)
    sp, cp, sy, cy, sr, cr = math.sin(p), math.cos(p), math.sin(y), math.cos(y), math.sin(r), math.cos(r)
    return [
        [cp * cy, sr * sp * cy - cr * sy, -(cr * sp * cy + sr * sy)],
        [cp * sy, sr * sp * sy + cr * cy, cy * sr - cr * sp * sy],
        [sp, -sr * cp, cr * cp],
    ]


def point_box_distance(p, box):
    """Distance from world point p to an oriented box (0 inside)."""
    m = _rot_matrix(*box["r"])
    d = [p[i] - box["c"][i] for i in range(3)]
    local = [sum(m[j][i] * d[j] for j in range(3)) for i in range(3)]  # transpose = inverse
    q = [max(abs(local[i]) - box["h"][i], 0.0) for i in range(3)]
    return math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2])


def box_corners(box):
    m = _rot_matrix(*box["r"])
    pts = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            for sz in (-1, 1):
                l = (sx * box["h"][0], sy * box["h"][1], sz * box["h"][2])
                pts.append(tuple(box["c"][i] + sum(m[i][j] * l[j] for j in range(3)) for i in range(3)))
    return pts


def sample_points(box, step=25.0, cap=2500):
    """Points on a lattice through the oriented box volume (corners and faces included)."""
    m = _rot_matrix(*box["r"])
    axes = []
    for h in box["h"]:
        n = max(1, int(math.ceil(2 * h / step)))
        n = min(n, 40)
        axes.append([-h + 2 * h * i / n for i in range(n + 1)])
    total = len(axes[0]) * len(axes[1]) * len(axes[2])
    stride = max(1, int(math.ceil(total / float(cap))))
    pts, k = [], 0
    for lx in axes[0]:
        for ly in axes[1]:
            for lz in axes[2]:
                k += 1
                if (k - 1) % stride:
                    continue
                l = (lx, ly, lz)
                pts.append(tuple(box["c"][i] + sum(m[i][j] * l[j] for j in range(3)) for i in range(3)))
    return pts


def _walkable_air_ref(p, clip_boxes):
    """Reference implementation (slow, O(boxes) per probe), kept for the selftest equivalence check.
    True if p is in free space a pawn can occupy: outside every clip solid and within PROTRUSION_CEILING_CM above a clip top
    surface that lies below it (the point's column hits a floor)."""
    for b in clip_boxes:
        if point_box_distance(p, b) <= 0.0:
            return False
    # floor beneath within the ceiling height: a downward probe at 10 cm steps
    z = 0.0
    while z <= PROTRUSION_CEILING_CM:
        q = (p[0], p[1], p[2] - z)
        for b in clip_boxes:
            if point_box_distance(q, b) <= 0.0:
                return True
        z += 10.0
    return False


def _protrusion_depth_ref(mesh_box, clip_boxes):
    """Largest distance (cm) by which sample points of the kit instance's bounds stand in walkable air away from every clip solid.
    The bounds are an upper bound for the mesh, so the check is conservative; exact flushness is verified in the editor."""
    worst = 0.0
    for p in sample_points(mesh_box):
        if _walkable_air_ref(p, clip_boxes):
            d = min(point_box_distance(p, b) for b in clip_boxes) if clip_boxes else 0.0
            if d > worst:
                worst = d
    return worst


class ClipIndex(object):
    """Prepared clip boxes with a 2D spatial hash, so the protrusion pass does not scan every box for every sample point.
    Every shortcut is a conservative bounding-sphere reject in front of the exact point_box_distance test, so the results are
    identical to the reference functions (_walkable_air_ref / _protrusion_depth_ref); the selftest checks that."""
    CELL = 256.0
    EPS = 1e-6

    def __init__(self, clip_boxes):
        self.items = []
        self.cells = {}
        for b in clip_boxes:
            m = _rot_matrix(*b["r"])
            h = b["h"]
            radius = math.sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]) + self.EPS
            it = (b["c"], h, m, radius)
            self.items.append(it)
            c = b["c"]
            for ix in range(int(math.floor((c[0] - radius) / self.CELL)), int(math.floor((c[0] + radius) / self.CELL)) + 1):
                for iy in range(int(math.floor((c[1] - radius) / self.CELL)), int(math.floor((c[1] + radius) / self.CELL)) + 1):
                    self.cells.setdefault((ix, iy), []).append(it)

    def _near(self, x, y, margin):
        """Boxes whose bounding sphere may come within `margin` of (x, y) horizontally (superset)."""
        c = self.CELL
        out = []
        for ix in range(int(math.floor((x - margin) / c)), int(math.floor((x + margin) / c)) + 1):
            for iy in range(int(math.floor((y - margin) / c)), int(math.floor((y + margin) / c)) + 1):
                out.extend(self.cells.get((ix, iy), ()))
        return out

    @staticmethod
    def _inside(p, it):
        c, h, m, radius = it
        dx, dy, dz = p[0] - c[0], p[1] - c[1], p[2] - c[2]
        if dx * dx + dy * dy + dz * dz > radius * radius:
            return False
        if abs(m[0][0] * dx + m[1][0] * dy + m[2][0] * dz) > h[0]:
            return False
        if abs(m[0][1] * dx + m[1][1] * dy + m[2][1] * dz) > h[1]:
            return False
        return abs(m[0][2] * dx + m[1][2] * dy + m[2][2] * dz) <= h[2]

    def walkable_air(self, p):
        cand = self._near(p[0], p[1], 0.0)
        for it in cand:
            if self._inside(p, it):
                return False
        z = 0.0
        while z <= PROTRUSION_CEILING_CM:
            q = (p[0], p[1], p[2] - z)
            for it in cand:
                if self._inside(q, it):
                    return True
            z += 10.0
        return False

    def min_distance(self, p):
        best = None
        for it in self.items:
            c = it[0]
            lb = math.sqrt((p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2) - it[3]
            if best is not None and lb >= best:
                continue
            d = _box_distance_m(p, it)
            if best is None or d < best:
                best = d
        return best if best is not None else 0.0

    def any_within(self, p, dist):
        """True if some box is at distance <= dist from p (exact)."""
        for it in self._near(p[0], p[1], dist + 1.0):
            c = it[0]
            if math.sqrt((p[0] - c[0]) ** 2 + (p[1] - c[1]) ** 2 + (p[2] - c[2]) ** 2) - it[3] > dist:
                continue
            if _box_distance_m(p, it) <= dist:
                return True
        return False


def _box_distance_m(p, it):
    c, h, m, _r = it
    d = [p[i] - c[i] for i in range(3)]
    local = [sum(m[j][i] * d[j] for j in range(3)) for i in range(3)]
    q = [max(abs(local[i]) - h[i], 0.0) for i in range(3)]
    return math.sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2])


def walkable_air(p, clip_boxes):
    """True if p is in free space a pawn can occupy (see ClipIndex.walkable_air). clip_boxes may be a ClipIndex."""
    idx = clip_boxes if isinstance(clip_boxes, ClipIndex) else ClipIndex(clip_boxes)
    return idx.walkable_air(p)


def protrusion_depth(mesh_box, clip_boxes):
    """Largest distance (cm) by which sample points of the kit instance's bounds stand in walkable air away from every clip solid.
    The bounds are an upper bound for the mesh, so the check is conservative; exact flushness is verified in the editor.
    clip_boxes may be a ClipIndex (build it once per map)."""
    idx = clip_boxes if isinstance(clip_boxes, ClipIndex) else ClipIndex(clip_boxes)
    worst = 0.0
    for p in sample_points(mesh_box):
        if not idx.walkable_air(p):
            continue
        if not idx.items:
            continue
        if worst > 0.0 and idx.any_within(p, worst):
            continue  # nearest solid is no farther than the current worst: cannot raise it
        d = idx.min_distance(p)
        if d > worst:
            worst = d
    return worst


def collision_diff(a, b, tol=COLLISION_TOL_CM):
    """a, b: dict {'blocking': [(key, center(3), half(3))...], 'gameplay': [(key, loc(3))...]}.
    Returns a list of difference strings (empty = identical within tol). Greedy nearest matching per key."""
    out = []
    for kind in ("blocking", "gameplay"):
        left, right = list(a[kind]), list(b[kind])
        by_key_r = {}
        for item in right:
            by_key_r.setdefault(item[0], []).append(item)
        for item in left:
            cand = by_key_r.get(item[0], [])
            hit = None
            for c in cand:
                if all(abs(x - y) <= tol for pa, pb in zip(item[1:], c[1:]) for x, y in zip(pa, pb)):
                    hit = c
                    break
            if hit is None:
                out.append("%s only in A: %s %s" % (kind, item[0], _round(item[1:])))
            else:
                cand.remove(hit)
        for key, rest in by_key_r.items():
            for c in rest:
                out.append("%s only in B: %s %s" % (kind, key, _round(c[1:])))
    return out


def _round(parts):
    return [[round(x, 1) for x in p] for p in parts]


def selftest():
    # budgets
    assert evaluate_budgets({"draw_calls_est": 10, "triangles_total": 100}) == []
    assert evaluate_budgets({"draw_calls_est": 601}) == ["draw_calls_est 601 > 600"]
    assert evaluate_budgets({"materials_per_mesh": {"SM_X": 9}})[0].startswith("mesh SM_X")
    assert abs(lightmap_mb([64]) - 64 * 64 * 5.2 / 1048576.0) < 1e-9
    # protrusion: floor slab and wall; a flush trim is fine, one 20 cm into the room is flagged
    floor = oriented_box((0, 0, -50), (500, 500, 50), (0, 0, 0))
    wall = oriented_box((500, 0, 300), (50, 500, 300), (0, 0, 0))
    clips = [floor, wall]
    flush = oriented_box((448, 0, 20), (2, 400, 20), (0, 0, 0))   # 4 cm skirting on the wall face x=450
    assert protrusion_depth(flush, clips) <= PROTRUSION_LIMIT_CM, protrusion_depth(flush, clips)
    inroom = oriented_box((420, 0, 20), (20, 400, 20), (0, 0, 0))  # sticks out 30 cm... centre 420 +-20 => x 400..440, wall face at 450
    assert protrusion_depth(inroom, clips) > PROTRUSION_LIMIT_CM, protrusion_depth(inroom, clips)
    high = oriented_box((100, 0, 400), (20, 20, 20), (0, 0, 0))    # 400 cm above the floor: out of reach
    assert protrusion_depth(high, clips) == 0.0
    inside = oriented_box((520, 0, 100), (20, 400, 20), (0, 0, 0))  # inside the wall solid
    assert protrusion_depth(inside, clips) == 0.0
    # the indexed implementation must agree exactly with the reference on a random scene (rotated boxes included)
    import random
    rnd = random.Random(7)
    scene = [oriented_box((rnd.uniform(-600, 600), rnd.uniform(-600, 600), rnd.uniform(-20, 300)),
                          (rnd.uniform(20, 300), rnd.uniform(20, 300), rnd.uniform(5, 120)),
                          (rnd.choice((0, 0, 30, -20)), rnd.uniform(0, 360), rnd.choice((0, 0, 15)))) for _ in range(25)]
    idx = ClipIndex(scene)
    for _ in range(10):
        kit = oriented_box((rnd.uniform(-500, 500), rnd.uniform(-500, 500), rnd.uniform(0, 250)),
                           (rnd.uniform(2, 150), rnd.uniform(2, 150), rnd.uniform(2, 80)), (0, rnd.uniform(0, 360), 0))
        assert protrusion_depth(kit, idx) == _protrusion_depth_ref(kit, scene)
        for pt in sample_points(kit, step=60.0, cap=60):
            assert idx.walkable_air(pt) == _walkable_air_ref(pt, scene)
    # rotated ramp box: its own centre is inside
    ramp = oriented_box((0, 0, 100), (200, 100, 10), (30, 0, 0))
    assert point_box_distance((0, 0, 100), ramp) == 0.0
    # collision diff
    A = {"blocking": [("Clip", (0, 0, 0), (100, 100, 10))], "gameplay": [("VeloxPickup", (5, 5, 5))]}
    B = {"blocking": [("Clip", (0.5, 0, 0), (100, 100, 10))], "gameplay": [("VeloxPickup", (5, 5, 5.9))]}
    assert collision_diff(A, B) == []
    B["gameplay"][0] = ("VeloxPickup", (5, 5, 8))
    assert len(collision_diff(A, B)) == 2
    print("%s selftest OK" % LOG)


# ---------------------------------------------------------------------------------------------------------------------
# Editor side
# ---------------------------------------------------------------------------------------------------------------------
def prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def short_name(path):
    return path.rstrip("/").split("/")[-1].split(".")[0]


def lod0_triangles(mesh):
    """Triangles of LOD0 of a StaticMesh (StaticMesh.get_num_triangles(lod), 5.8). 0 when the API is missing."""
    try:
        return int(mesh.get_num_triangles(0))
    except Exception:
        return 0


def resolve_material_chain(mat):
    """Follow MaterialInstance -> parent until a Material; returns (material, [instances])."""
    chain = []
    seen = 0
    while mat is not None and seen < 16:
        if isinstance(mat, unreal.Material):
            return mat, chain
        chain.append(mat)
        mat = prop(mat, "parent")
        seen += 1
    return None, chain


def material_textures(mat):
    """All textures a material interface references (graph textures plus instance overrides)."""
    texs = []
    base, chain = resolve_material_chain(mat)
    for inst in chain:
        for entry in prop(inst, "texture_parameter_values", []) or []:
            t = prop(entry, "parameter_value")
            if t is not None:
                texs.append(t)
    if base is not None:
        try:
            texs.extend(t for t in unreal.MaterialEditingLibrary.get_used_textures(base) if t is not None)
        except Exception:
            pass
    return texs


def texture_cost_bytes(tex):
    try:
        w = int(tex.blueprint_get_size_x())
        h = int(tex.blueprint_get_size_y())
    except Exception:
        w = h = int(prop(tex, "source_size_x", 0) or 0) or 0
        h = int(prop(tex, "source_size_y", 0) or 0) or h
    comp = str(prop(tex, "compression_settings", ""))
    if "HDR" in comp or "DISPLACEMENT" in comp:
        bpp = 8.0
    elif "MASKS" in comp or "ALPHA" in comp or "BC7" in comp or "NORMALMAP" in comp:
        bpp = 1.0
    elif "DEFAULT" in comp and bool(prop(tex, "lod_group", None) is not None):
        bpp = 1.0       # BC3 upper bound (BC1 would be 0.5)
    else:
        bpp = 1.0
    return texture_bytes(w, h, bpp)


def blend_translucent(mat):
    base, chain = resolve_material_chain(mat)
    # instance override of the blend mode
    for inst in chain:
        ovr = prop(inst, "base_property_overrides")
        if ovr is not None and prop(ovr, "override_blend_mode", False):
            bm = str(prop(ovr, "blend_mode"))
            return not ("OPAQUE" in bm or "MASKED" in bm)
    if base is None:
        return False
    bm = str(prop(base, "blend_mode"))
    return not ("OPAQUE" in bm or "MASKED" in bm)


def component_lightmap_res(comp, mesh):
    if prop(comp, "override_light_map_res", False):
        return int(prop(comp, "overridden_light_map_res", 0) or 0)
    return int(prop(mesh, "light_map_resolution", 0) or 0)


def comp_visible(comp):
    return bool(prop(comp, "visible", True)) and not bool(prop(comp, "hidden_in_game", False))


def comp_blocks_pawn(comp):
    try:
        ce = str(comp.get_collision_enabled())
        if "NO_COLLISION" in ce:
            return False
        resp = comp.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)
        return "BLOCK" in str(resp)
    except Exception:
        return False


def world_oriented_box(actor_or_comp):
    """Oriented box of a static mesh component's mesh bounds in world space."""
    comp = actor_or_comp
    mesh = comp.static_mesh
    b = mesh.get_bounding_box()
    mn, mx = b.min, b.max
    lc = ((mn.x + mx.x) / 2, (mn.y + mx.y) / 2, (mn.z + mx.z) / 2)
    lh = ((mx.x - mn.x) / 2, (mx.y - mn.y) / 2, (mx.z - mn.z) / 2)
    t = comp.get_world_transform()
    s = t.scale3d
    loc = t.translation
    r = t.rotation.rotator()
    m = _rot_matrix(r.pitch, r.yaw, r.roll)
    sl = (lc[0] * s.x, lc[1] * s.y, lc[2] * s.z)
    center = (
        loc.x + sum(m[0][j] * sl[j] for j in range(3)),
        loc.y + sum(m[1][j] * sl[j] for j in range(3)),
        loc.z + sum(m[2][j] * sl[j] for j in range(3)),
    )
    half = (abs(lh[0] * s.x), abs(lh[1] * s.y), abs(lh[2] * s.z))
    return oriented_box(center, half, (r.pitch, r.yaw, r.roll))


def collect(map_path):
    """Load the map and gather everything the audit needs."""
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if world is None:
        raise RuntimeError("cannot load map %s" % map_path)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    data = {"map": map_path, "components": [], "blocking": [], "gameplay": [], "clip_boxes": [], "kit_instances": [], "actor_count": len(actors)}
    for a in actors:
        cname = a.get_class().get_name()
        label = a.get_actor_label()
        sm_comps = a.get_components_by_class(unreal.StaticMeshComponent)
        for comp in sm_comps:
            mesh = prop(comp, "static_mesh")
            if mesh is None:
                continue
            mats = list(comp.get_materials())
            vis = comp_visible(comp)
            static = "STATIC" in str(prop(comp, "mobility", "")).upper()
            res = component_lightmap_res(comp, mesh) if static else 0
            rec = {
                "actor": label, "class": cname, "mesh": mesh.get_path_name(), "mesh_name": mesh.get_name(), "visible": vis, "static": static,
                "lm_res": res, "tris": None, "materials": [m.get_path_name() for m in mats if m is not None], "n_slots": len(mats),
            }
            data["components"].append(rec)
            if comp_blocks_pawn(comp):
                ob = world_oriented_box(comp)
                data["clip_boxes"].append(ob)
                data["blocking"].append(("%s|%s" % (cname, mesh.get_name()), ob["c"], ob["h"]))
            elif vis and mesh.get_name().startswith("SM_Kit_"):
                data["kit_instances"].append((label, world_oriented_box(comp)))
        if cname not in VISUAL_ACTOR_CLASSES and "Light" not in cname:
            loc = a.get_actor_location()
            data["gameplay"].append((cname, (loc.x, loc.y, loc.z)))
    return data


def figures(data):
    tri_cache, tex_cache = {}, {}
    meshes, mats = set(), set()
    mats_per_mesh = {}
    draw = tris = lm_gt64 = 0
    lm_res = []
    translucent = []
    textures = {}
    kit = set()
    lm_components = 0
    detail = []
    for rec in data["components"]:
        if not rec["visible"]:
            continue
        mp = rec["mesh"]
        if mp not in tri_cache:
            tri_cache[mp] = lod0_triangles(unreal.load_asset(mp))
        t = tri_cache[mp]
        rec["tris"] = t
        meshes.add(rec["mesh_name"])
        if rec["mesh_name"].startswith("SM_Kit_"):
            kit.add(rec["mesh_name"])
        mats_per_mesh.setdefault(rec["mesh_name"], set()).update(rec["materials"])
        mats.update(rec["materials"])
        draw += max(rec["n_slots"], 1)
        tris += t
        if rec["static"] and rec["lm_res"] > 0:
            lm_components += 1
            lm_res.append(rec["lm_res"])
            if rec["lm_res"] > 64:
                lm_gt64 += 1
        for mp_ in rec["materials"]:
            if mp_ not in tex_cache:
                mi = unreal.load_asset(mp_)
                tex_cache[mp_] = (blend_translucent(mi), material_textures(mi))
            is_tr, texs = tex_cache[mp_]
            if is_tr and rec["class"] not in TRANSLUCENT_EXEMPT_ACTOR_CLASSES and not any(w in mp_.lower() for w in TRANSLUCENT_EXEMPT_MATERIAL_WORDS):
                translucent.append("%s: %s" % (rec["actor"], mp_))
            for tex in texs:
                textures[tex.get_path_name()] = tex
    tex_mb = sum(texture_cost_bytes(t) for t in textures.values()) / (1024.0 * 1024.0)
    top = sorted(((v, k) for k, v in tri_cache.items()), reverse=True)[:8]
    fig = {
        "draw_calls_est": draw, "triangles_total": tris, "lightmap_mb": round(lightmap_mb(lm_res), 3), "texture_mb": round(tex_mb, 3),
        "lm_gt64": lm_gt64, "lm_components": lm_components, "kit_meshes": len(kit), "distinct_meshes": len(meshes), "distinct_materials": len(mats),
        "translucent": len(translucent), "translucent_list": translucent[:20],
        "materials_per_mesh": {k: len(v) for k, v in mats_per_mesh.items() if k.startswith("SM_Kit_")},
        "components_visible": sum(1 for r in data["components"] if r["visible"]), "components_total": len(data["components"]),
        "top_meshes_by_tris": [{"mesh": k.split(".")[-1], "tris": v} for v, k in top],
        "lightmap_resolutions": {str(r): lm_res.count(r) for r in sorted(set(lm_res))},
    }
    return fig


def lighting_figures(map_path):
    """registry / lightmapped / vlm. Python cannot reach ULevel::MapBuildData or FMeshMapBuildData, so this runs the C++ test
    Velox.Maps.LightingBuilt in a child editor with -VeloxLightingMap=<map> and parses its `VeloxLightingBuilt` line (the same
    figures, the same verdict as the automation test, ADR 0006 E0.1 (d))."""
    import re
    eng = str(unreal.Paths.engine_dir())
    plat = "Mac" if sys.platform == "darwin" else "Linux"
    exe = os.path.join(eng, "Binaries", plat, "UnrealEditor-Cmd")
    if not os.path.exists(exe):
        exe = os.path.join(eng, "Binaries", plat, "UnrealEditor.app", "Contents", "MacOS", "UnrealEditor-Cmd")
    proj = str(unreal.Paths.get_project_file_path())
    cmd = [exe, proj, "-ExecCmds=Automation RunTests Velox.Maps.LightingBuilt; Quit", "-TestExit=Automation Test Queue Empty",
           "-VeloxLightingMap=" + map_path, "-nullrhi", "-unattended", "-nopause", "-nosplash", "-stdout", "-FullStdOutLogOutput"]
    figs = {"registry": 0, "lightmapped": 0, "components": 0, "vlm": 0, "probe": "no VeloxLightingBuilt line in the child editor output"}
    try:
        out = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=900, errors="replace").stdout
    except Exception as e:  # noqa: BLE001
        figs["probe"] = "child editor failed: %s" % e
        return figs
    for line in out.splitlines():
        m = re.search(r"VeloxLightingBuilt (\S+) registry=(\d) lightmapped=(\d+)/(\d+) vlm=(\d)", line)
        if m and m.group(1) == map_path:
            figs = {"registry": int(m.group(2)), "lightmapped": int(m.group(3)), "components": int(m.group(4)), "vlm": int(m.group(5)), "probe": "ok"}
    return figs


def lfs_total_mb():
    try:
        out = subprocess.check_output(["git", "lfs", "ls-files", "-s"], stderr=subprocess.DEVNULL, text=True, cwd=project_dir())
    except Exception:
        return None
    total = 0.0
    for line in out.splitlines():
        # "<oid> - <path> (12 MB)"
        if line.rstrip().endswith(")") and "(" in line:
            num = line[line.rfind("(") + 1:-1].split()
            if len(num) == 2:
                unit = {"B": 1.0, "KB": 1024.0, "MB": 1048576.0, "GB": 1073741824.0}.get(num[1].upper())
                try:
                    if unit:
                        total += float(num[0]) * unit
                except ValueError:
                    pass
    return round(total / 1048576.0, 1)


def project_dir():
    return str(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


def main(argv):
    if "--selftest" in argv:
        selftest()
        return 0
    if not HAVE_UNREAL:
        print("%s must run inside the editor (pythonscript commandlet); use --selftest for the pure helpers" % LOG)
        return 2
    import argparse
    ap = argparse.ArgumentParser(prog="audit_map.py")
    ap.add_argument("map")
    ap.add_argument("--diff-collision", nargs=2, metavar=("A", "B"))
    ap.add_argument("--protrusion", action="store_true")
    ap.add_argument("--lighting", action="store_true")
    ap.add_argument("--json")
    args = ap.parse_args(argv)

    name = short_name(args.map)
    data = collect(args.map)
    fig = figures(data)
    breaches = evaluate_budgets(fig)
    result = {"map": args.map, "short": name, "budget": BUDGET, "figures": fig, "breaches": breaches}

    log("map %s: %d actors, %d static mesh components (%d visible)" % (args.map, data["actor_count"], fig["components_total"], fig["components_visible"]))
    log("draw_calls_est   %6d / %d" % (fig["draw_calls_est"], BUDGET["draw_calls_est"]))
    log("triangles_total  %6d / %d" % (fig["triangles_total"], BUDGET["triangles_total"]))
    log("lightmap_mb      %6.2f / %.0f   (%d lightmapped components, resolutions %s)" % (fig["lightmap_mb"], BUDGET["lightmap_mb"], fig["lm_components"], fig["lightmap_resolutions"]))
    log("texture_mb       %6.2f / %.0f" % (fig["texture_mb"], BUDGET["texture_mb"]))
    log("lm_res_gt_64     %6d / %d" % (fig["lm_gt64"], BUDGET["lm_gt64"]))
    log("kit_meshes       %6d / %d   (distinct meshes %d, distinct materials %d)" % (fig["kit_meshes"], BUDGET["kit_meshes"], fig["distinct_meshes"], fig["distinct_materials"]))
    log("translucent      %6d / %d" % (fig["translucent"], BUDGET["translucent"]))
    for t in fig["translucent_list"]:
        log("  translucent: %s" % t)
    for m in fig["top_meshes_by_tris"]:
        log("  mesh %-32s %6d tris" % (m["mesh"], m["tris"]))

    lfs = lfs_total_mb()
    result["lfs_mb"] = lfs
    log("lfs_total_mb     %s (informational; M5 new-object budget %.0f MB, section 1)" % (lfs, BUDGET["lfs_mb"]))

    if args.protrusion:
        flagged = []
        clips = ClipIndex(data["clip_boxes"])
        for label, box in data["kit_instances"]:
            d = protrusion_depth(box, clips)
            if d > PROTRUSION_LIMIT_CM:
                flagged.append({"actor": label, "depth_cm": round(d, 1)})
        result["protrusion"] = {"kit_instances": len(data["kit_instances"]), "flagged": flagged}
        log("protrusion       %d kit instances checked, %d intrude > %.0f cm" % (len(data["kit_instances"]), len(flagged), PROTRUSION_LIMIT_CM))
        for f in flagged[:20]:
            log("  protrudes: %s %.1f cm" % (f["actor"], f["depth_cm"]))
        if flagged:
            breaches.append("protrusion: %d kit instances" % len(flagged))

    if args.diff_collision:
        pa, pb = args.diff_collision
        da = data if pa == args.map else collect(pa)
        db = data if pb == args.map else collect(pb)
        diffs = collision_diff(da, db)
        result["collision_diff"] = {"a": pa, "b": pb, "blocking_a": len(da["blocking"]), "blocking_b": len(db["blocking"]),
                                    "gameplay_a": len(da["gameplay"]), "gameplay_b": len(db["gameplay"]), "differences": diffs[:200], "count": len(diffs)}
        log("collision diff   %s vs %s: %d differences (blocking %d/%d, gameplay actors %d/%d)" % (
            short_name(pa), short_name(pb), len(diffs), len(da["blocking"]), len(db["blocking"]), len(da["gameplay"]), len(db["gameplay"])))
        for d in diffs[:20]:
            log("  %s" % d)
        if diffs:
            breaches.append("collision diff: %d differences" % len(diffs))
        if not (args.map == pa or args.map == pb):
            pass
        # collect() reloads a different map; reload the audited one so later steps see it
        if da is not data or db is not data:
            data = collect(args.map)

    if args.lighting:
        lf = lighting_figures(args.map)
        result["lighting"] = lf
        log("lighting         registry=%d lightmapped=%d/%d vlm=%d  (probe: %s)" % (lf["registry"], lf["lightmapped"], lf["components"], lf["vlm"], lf["probe"]))
        ok = lf["registry"] == 1 and lf["lightmapped"] >= 1 and lf["vlm"] == 1
        result["lighting"]["pass"] = ok
        if not ok:
            log("lighting         NOT BAKED (E0.1 d needs registry=1, lightmapped>=1, vlm=1)")
        # Lighting never counts as a budget breach (budgets do not depend on the bake); it only sets its own verdict line.

    out_path = args.json or os.path.join(project_dir(), "Saved", "Audit", "%s.json" % name)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w") as f:
        json.dump(result, f, indent=1, sort_keys=True)
    log("json             %s" % out_path)
    for b in breaches:
        log("BREACH %s" % b)
    verdict = "PASS" if not breaches else "FAIL"
    if args.lighting:
        log("LIGHTING %s" % ("PASS" if result["lighting"]["pass"] else "FAIL"))
    log("RESULT %s" % verdict)
    return 0 if not breaches else 1


if __name__ == "__main__":
    rc = main(sys.argv[1:])
    if rc and HAVE_UNREAL:
        raise SystemExit(rc)
    elif rc:
        sys.exit(rc)
