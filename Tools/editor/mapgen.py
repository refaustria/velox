"""Velox map generator, map independent (ADR 0008 section 1.2, task M7 P2; formerly the body of create_dm1_map.py, ADR 0006 section 2).

Every map is a layout table `maps/<slug>_layout.py` (data only, records from `maps/records.py`). The functions here take that
table as their first parameter `L`; nothing in this module names a map. Pure-Python parts (validate_layout, all_solids, art_plan,
expected_counts, art_counts, pad_arc, plan_protrusion) need no editor; the spawn/build parts need `unreal` and `veloxmap`.

`bind(L)` returns a facade whose functions take no `L` (used by `maps/golden_dump.py`). The CLI is `create_map.py`.
"""
import functools
import inspect
import math
import os
import sys
from collections import Counter, namedtuple

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

try:
    import unreal  # noqa: E402
    import veloxmap as vm  # noqa: E402
except ImportError:      # pure-Python use (validate, golden) outside the editor
    unreal = None
    vm = None

MATERIAL_KEYS = ("Floor", "Wall", "Platform", "Stairs", "RampWalkable", "Pad", "Target")
STAIR_FOOT_EXTRA = 100.0
RAMP_THICKNESS = 20.0
RAMP_FILL_STEP = 200.0
PLAYERSTART_Z = 100.0
SIGHTLINE_REACH = 30000.0        # cm; a longest sight line that reaches it is reported as "open"
GRAVITY = 2000.0
PAD_TRIGGER_Z = 70.0
TRAJECTORY_DT = 1.0 / 240.0

from maps.records import DEFAULT_RULES, LEGACY_RULES, HAZARD_TYPES, OCT_CLEAR_FACTOR, PAD_MAX_LAUNCH_SPEED  # noqa: E402,F401


def blockout_path(L):
    """The spec module's MAP_PATH (ADR 0008 1.2); DM1 has none and keeps /Game/Maps/<MAP_ID>/L_<MAP_ID>_Blockout."""
    return getattr(L, "MAP_PATH", None) or f"/Game/Maps/{L.MAP_ID}/L_{L.MAP_ID}_Blockout"


def art_path(L):
    return getattr(L, "ART_MAP_PATH", None) or f"/Game/Maps/{L.MAP_ID}/L_{L.MAP_ID}"


def sky_cubemap(L):
    return f"/Game/Environment/Lighting/TC_Sky_{L.MAP_ID}"


def log_prefix(L):
    return f"[Velox{L.MAP_ID}]"



# ---------------------------------------------------------------- expansion of the records into boxes (pure Python)
def expand_wall(w):
    """Strips of a wall with openings: (id, kind, x0, x1, y0, y1, z0, z1). Sills are walls; lintels float."""
    pieces = []

    def put(suffix, s0, s1, z0, z1, kind):
        if s1 - s0 <= 0.0 or z1 - z0 <= 0.0:
            return
        if w.run == "x":
            pieces.append((f"{w.id}_{suffix}", kind, s0, s1, w.b0, w.b1, z0, z1))
        else:
            pieces.append((f"{w.id}_{suffix}", kind, w.b0, w.b1, s0, s1, z0, z1))

    cur = w.s0
    for i, o in enumerate(sorted(w.openings)):
        put(f"seg{i}", cur, o.o0, 0.0, w.top, "wall")
        put(f"sill{i}", o.o0, o.o1, 0.0, min(o.zlo, w.top), "wall")
        put(f"lintel{i}", o.o0, o.o1, o.zhi, w.top, "lintel")
        cur = o.o1
    put("seg%d" % len(w.openings), cur, w.s1, 0.0, w.top, "wall")
    return pieces


StairsFrame = namedtuple("StairsFrame", "axis x_from d band tread riser z0")
RampFrame = namedtuple("RampFrame", "axis foot d band deg z0 run length")
_RAMP_DIRS = {"+x": ("x", 1.0), "-x": ("x", -1.0), "+y": ("y", 1.0), "-y": ("y", -1.0)}
_RAMP_YAW = {"+x": 0.0, "+y": 90.0, "-x": 180.0, "-y": 270.0}      # yaw of the slab's local +X (the run direction)


def stairs_frame(L, s):
    """Stairs record -> StairsFrame: run axis ('x'|'y'), start coordinate on it, direction sign, cross band (y0, y1), tread, riser and
    base height with the layout defaults applied. All code reads stairs through this helper (ADR 0008 section 1.3)."""
    if s.axis not in ("x", "y"):
        raise ValueError(f"{s.id}: stairs axis {s.axis!r} (x | y)")
    return StairsFrame(s.axis, s.x_from, s.x_dir, (s.y0, s.y1), L.TREAD if s.tread is None else s.tread,
                       L.RISER if s.riser is None else s.riser, L.FLOOR_Z if s.z0 is None else s.z0)


def expand_stairs(L, s):
    f = stairs_frame(L, s)
    out = []
    # The first 20 cm step is left out (the foot rises 40 cm at once, still below the 45 cm step height): Recast raises the nav
    # over a lone 20 cm step beside a wall and the lint then reports the foot strip as walk-in floor.
    for i in range(1, s.steps):
        xa = f.x_from + f.d * i * f.tread
        xb = xa + f.d * f.tread
        if i == 1:
            xa -= f.d * STAIR_FOOT_EXTRA      # lengthened foot (see above)
        lo, hi = min(xa, xb), max(xa, xb)
        top = f.z0 + (i + 1) * f.riser
        if f.axis == "x":
            out.append((f"{s.id}_{i:02d}", "step", lo, hi, f.band[0], f.band[1], f.z0, top))
        else:
            out.append((f"{s.id}_{i:02d}", "step", f.band[0], f.band[1], lo, hi, f.z0, top))
    return out


def ramp_frame(L, r):
    """Ramp record -> RampFrame: run axis, foot point (x, y, z0) at the middle of the cross band, unit direction (dx, dy), cross band
    (c0, c1) as coordinates on the other axis, angle, foot height, horizontal run and slope length. `rise` is the height gained."""
    if r.dir not in _RAMP_DIRS:
        raise ValueError(f"{r.id}: ramp dir {r.dir!r} (+x -x +y -y)")
    axis, sign = _RAMP_DIRS[r.dir]
    deg = L.RAMP_DEG if r.deg is None else r.deg
    z0 = L.FLOOR_Z if r.z0 is None else r.z0
    mid = (r.y0 + r.y1) / 2
    th = math.radians(deg)
    if axis == "x":
        foot, d = (r.x0, mid, z0), (sign, 0.0)
    else:
        foot, d = (mid, r.x0, z0), (0.0, sign)
    return RampFrame(axis, foot, d, (r.y0, r.y1), deg, z0, r.rise / math.tan(th), r.rise / math.sin(th))


def ramp_yaw(r):
    return _RAMP_YAW[r.dir]


def ramp_geometry(L, r):
    """(run, length along the slope) of the ramp."""
    f = ramp_frame(L, r)
    return f.run, f.length


def _ramp_local(f, r, x, y):
    """(distance along the run from the foot, cross coordinate) of the world point (x, y)."""
    if f.axis == "x":
        return (x - r.x0) * f.d[0], y
    return (y - r.x0) * f.d[1], x


def ramp_surface_z(L, r, x, y):
    """Walking surface height of the ramp at (x, y), or None outside its footprint (band and run, edges included)."""
    f = ramp_frame(L, r)
    s, cross = _ramp_local(f, r, x, y)
    if not (f.band[0] <= cross <= f.band[1] and 0.0 <= s <= f.run):
        return None
    return s * math.tan(math.radians(f.deg)) + f.z0


def expand_ramp_fills(L, r):
    """Stepped pawn-blocking fill boxes under the thin slab (Ramp_Fill_*, as on L_ItemTest): each box reaches the slab underside
    at its low end, so the residual clearance under the slab is < RAMP_FILL_STEP * tan(deg) (54 cm at 15 deg). `fill="none"`: no boxes
    (a free-spanning slab, see validate_layout)."""
    if r.fill == "none":
        return []
    if r.fill != "solid":
        raise ValueError(f"{r.id}: ramp fill {r.fill!r} (solid | none)")
    f = ramp_frame(L, r)
    th = math.radians(f.deg)
    sign = f.d[0] if f.axis == "x" else f.d[1]
    out = []
    sa = 0.0
    n = 0
    while sa < f.run - 1e-6:
        sb = min(sa + RAMP_FILL_STEP, f.run)
        top = sa * math.tan(th) - RAMP_THICKNESS / math.cos(th) + f.z0
        if top > f.z0 + 1.0:
            n += 1
            lo, hi = (r.x0 + sa, r.x0 + sb) if sign > 0 else (r.x0 - sb, r.x0 - sa)
            if f.axis == "x":
                out.append((f"{r.id}_Fill_{n}", "platform", lo, hi, r.y0, r.y1, f.z0, top))
            else:
                out.append((f"{r.id}_Fill_{n}", "platform", r.y0, r.y1, lo, hi, f.z0, top))
        sa = sb
    return out


def all_solids(L):
    """Every axis-aligned solid of the map as (id, kind, x0, x1, y0, y1, z0, z1): boxes, wall strips, stairs, ramp fills."""
    out = [(b.id, b.kind, b.x0, b.x1, b.y0, b.y1, b.z0, b.z1) for b in L.BOXES]
    for w in L.WALLS:
        out += expand_wall(w)
    for s in L.STAIRS:
        out += expand_stairs(L, s)
    for r in L.RAMPS:
        out += expand_ramp_fills(L, r)
    return out


def material_of(L, kind, record_id):
    if kind in ("wall", "lintel", "bridgewall"):
        return "Wall"
    if kind == "step":
        return "Stairs"
    for b in L.BOXES:
        if b.id == record_id:
            return b.mat
    return "Platform"


# ---------------------------------------------------------------- geometry helpers for the checks
def seg_hits_box(p, q, box, pad=0.0):
    """Slab test of the segment p->q against (x0, x1, y0, y1, z0, z1)."""
    lo = (box[0] - pad, box[2] - pad, box[4] - pad)
    hi = (box[1] + pad, box[3] + pad, box[5] + pad)
    t0, t1 = 0.0, 1.0
    for a in range(3):
        d = q[a] - p[a]
        if abs(d) < 1e-9:
            if p[a] < lo[a] or p[a] > hi[a]:
                return False
            continue
        ta, tb = (lo[a] - p[a]) / d, (hi[a] - p[a]) / d
        if ta > tb:
            ta, tb = tb, ta
        t0, t1 = max(t0, ta), min(t1, tb)
        if t0 > t1:
            return False
    return True


def point_in_box(p, box):
    return box[0] <= p[0] <= box[1] and box[2] <= p[1] <= box[3] and box[4] <= p[2] <= box[5]


def pad_z(L, pad):
    """Floor height under a pad (Pad.z; None = the map's FLOOR_Z)."""
    return L.FLOOR_Z if getattr(pad, "z", None) is None else pad.z


def spawn_z(L, spawn):
    """Floor height under a spawn (Spawn.z; None = the map's FLOOR_Z). The PlayerStart actor sits PLAYERSTART_Z above it."""
    return L.FLOOR_Z if getattr(spawn, "z", None) is None else spawn.z


def pad_launch(L, pad):
    """AimAtTarget launch velocity (VeloxMoveSim::JumpPadLaunchVelocity, Height > 0 branch) from the trigger centre."""
    ox, oy, oz = pad.x, pad.y, pad_z(L, pad) + PAD_TRIGGER_Z
    dx, dy, h = pad.target[0] - ox, pad.target[1] - oy, pad.target[2] - oz
    dist = math.hypot(dx, dy)
    t = math.sqrt(2.0 * h / GRAVITY)
    return (dx / dist * dist / t, dy / dist * dist / t, GRAVITY * t), t


def pad_arc(L, pad, solids, interpolate=False):
    """Walk the feet arc (origin = pad floor point, VeloxJumpPad::TryComputeLandingPoint) from the apex on; returns
    (landing, error text or None). A hit before the apex is an error (the arc must be free up to the apex).
    The landing is the first sample inside the solid (up to ~8 cm below its top at 240 Hz; the DM1 golden freezes that); interpolate=True
    returns the point where the arc crosses the top instead (M7 P6: pad_arc_clearance_problem and genuine landings)."""
    v, t_apex = pad_launch(L, pad)
    cyls = [(o.cx, o.cy, oct_r_clear(o), o.z0, o.z1) for o in octs(L)]       # an Oct must be cleared: circumscribed radius
    clips = [(c.x0, c.x1, c.y0, c.y1, c.z0, c.z1) for c in L.CLIPS]
    zp = pad_z(L, pad)
    pre = [s[2:] for s in solids if s[1] != "floor" and s[7] > zp + 1.0] + clips        # the pad stands on the ground slab / its deck
    post = [s[2:] for s in solids] + clips
    o = (pad.x, pad.y, pad_z(L, pad))

    def at(t):
        return (o[0] + v[0] * t, o[1] + v[1] * t, o[2] + v[2] * t - 0.5 * GRAVITY * t * t)

    prev = at(0.0)
    steps = int(5.0 / TRAJECTORY_DT)
    for i in range(1, steps):
        t = i * TRAJECTORY_DT
        cur = at(t)
        hit = next((b for b in (pre if t < t_apex else post) if seg_hits_box(prev, cur, b)), None)
        if hit is None:
            hit = next((c for c in cyls if seg_cyl_t(prev, cur, *c) is not None), None)
        if hit is not None:
            # first contact: the landing is where the segment enters the top of the box
            if t < t_apex - 1e-6:
                return None, f"{pad.id}: the arc hits solid {hit} before its apex (t={t:.2f} s < {t_apex:.2f} s) at {cur}"
            top = hit[5] if len(hit) == 6 else hit[4]            # box (x0 x1 y0 y1 z0 z1) or Oct cylinder (cx cy r z0 z1)
            if interpolate and prev[2] > top >= cur[2]:                          # entered through the top: interpolate it (the sample lies up to ~8 cm below)
                f = (prev[2] - top) / (prev[2] - cur[2])
                return (prev[0] + f * (cur[0] - prev[0]), prev[1] + f * (cur[1] - prev[1]), top), None
            return (cur[0], cur[1], cur[2]), None
        prev = cur
    return None, f"{pad.id}: the arc never lands"


# ---------------------------------------------------------------- M7 P4: rules, Oct, Hazard, line of sight, link ends, drops, free edges
def rules(L):
    """DEFAULT_RULES merged with the spec module's RULES; a key that DEFAULT_RULES does not know is an error (a typo would silently do nothing)."""
    out = dict(DEFAULT_RULES)
    out.update(LEGACY_RULES.get(getattr(L, "MAP_ID", None), {}))
    extra = getattr(L, "RULES", None) or {}
    unknown = sorted(set(extra) - set(out))
    if unknown:
        raise ValueError(f"unknown RULES key(s) {unknown}")
    out.update(extra)
    return out


def octs(L):
    return list(getattr(L, "OCTS", None) or ())


def hazards(L):
    return list(getattr(L, "HAZARDS", None) or ())


def oct_r_block(o):
    """Radius where an Oct must block (line of sight): the inscribed circle."""
    return o.d / 2.0


def oct_r_clear(o):
    """Radius where an Oct must be cleared (pad arcs, gaps): the circumscribed circle, 0.541 d."""
    return o.d * OCT_CLEAR_FACTOR


def seg_box_t(p, q, box, pad=0.0):
    """Entry parameter t in [0, 1] of the segment p->q into (x0, x1, y0, y1, z0, z1) grown by `pad`, or None."""
    lo = (box[0] - pad, box[2] - pad, box[4] - pad)
    hi = (box[1] + pad, box[3] + pad, box[5] + pad)
    t0, t1 = 0.0, 1.0
    for a in range(3):
        d = q[a] - p[a]
        if abs(d) < 1e-9:
            if p[a] < lo[a] or p[a] > hi[a]:
                return None
            continue
        ta, tb = (lo[a] - p[a]) / d, (hi[a] - p[a]) / d
        if ta > tb:
            ta, tb = tb, ta
        t0, t1 = max(t0, ta), min(t1, tb)
        if t0 > t1:
            return None
    return t0


def seg_cyl_t(p, q, cx, cy, r, z0, z1):
    """Entry parameter of the segment p->q into the vertical cylinder (cx, cy, r, z0..z1), or None."""
    t0, t1 = 0.0, 1.0
    dz = q[2] - p[2]
    if abs(dz) < 1e-9:
        if p[2] < z0 or p[2] > z1:
            return None
    else:
        ta, tb = (z0 - p[2]) / dz, (z1 - p[2]) / dz
        if ta > tb:
            ta, tb = tb, ta
        t0, t1 = max(t0, ta), min(t1, tb)
        if t0 > t1:
            return None
    dx, dy, fx, fy = q[0] - p[0], q[1] - p[1], p[0] - cx, p[1] - cy
    a, c = dx * dx + dy * dy, fx * fx + fy * fy - r * r
    if a < 1e-12:
        return t0 if c <= 0.0 else None
    b = 2.0 * (fx * dx + fy * dy)
    disc = b * b - 4.0 * a * c
    if disc < 0.0:
        return None
    s = math.sqrt(disc)
    t0, t1 = max(t0, (-b - s) / (2.0 * a)), min(t1, (-b + s) / (2.0 * a))
    return t0 if t0 <= t1 else None


NONSTANDING_KINDS = ("wall", "bridgewall", "lintel", "hazard")       # solid kinds that are no standing surface


def _supports(L, solids, x, y, z, tol):
    """Is there a standing surface within `tol` of height z under (x, y)? Boxes, ramp slabs, Oct tops."""
    for s in solids:
        if s[1] not in NONSTANDING_KINDS and s[2] <= x <= s[3] and s[4] <= y <= s[5] and abs(s[7] - z) <= tol:
            return True
    for r in L.RAMPS:
        rz = ramp_surface_z(L, r, x, y)
        if rz is not None and abs(rz - z) <= tol:
            return True
    for o in octs(L):
        if math.hypot(x - o.cx, y - o.cy) <= oct_r_block(o) and abs(o.z1 - z) <= tol:
            return True
    return False


def _solid_at(L, solids, x, y, z, tol):
    """Is (x, y) occupied by something that reaches z (a wall or a neighbouring floor at or above it)? Used to tell a protected edge from a free one."""
    for s in solids:
        if s[1] != "hazard" and s[2] <= x <= s[3] and s[4] <= y <= s[5] and s[6] <= z + tol and s[7] >= z - tol:
            return True
    for r in L.RAMPS:
        rz = ramp_surface_z(L, r, x, y)
        if rz is not None and rz >= z - tol:
            return True
    for o in octs(L):
        if math.hypot(x - o.cx, y - o.cy) <= oct_r_block(o) and o.z0 <= z + tol and o.z1 >= z - tol:
            return True
    return False


def _obstructs(L, solids, x, y, z, head=160.0, tol=5.0):
    """Does anything stand in the column at (x, y) between z + tol and z + head (a wall, a taller step, an Oct)?"""
    for s in solids:
        if s[1] != "hazard" and s[2] <= x <= s[3] and s[4] <= y <= s[5] and s[6] < z + head and s[7] > z + tol:
            return True
    for o in octs(L):
        if math.hypot(x - o.cx, y - o.cy) <= oct_r_block(o) and o.z0 < z + head and o.z1 > z + tol:
            return True
    return False


# ---- line of sight (ADR 0008 1.4)
LOS_EYE_Z = 160.0
LOS_TARGET_Z = 60.0


def _slab_boxes(L, r):
    """Stepped thin boxes approximating the slab of a free-spanning (fill "none") ramp, for line of sight only."""
    f = ramp_frame(L, r)
    th = math.radians(f.deg)
    sign = f.d[0] if f.axis == "x" else f.d[1]
    out = []
    n = max(1, int(math.ceil(f.run / 50.0)))
    for i in range(n):
        sa, sb = f.run * i / n, f.run * (i + 1) / n
        lo, hi = (r.x0 + sa, r.x0 + sb) if sign > 0 else (r.x0 - sb, r.x0 - sa)
        z1 = sb * math.tan(th) + f.z0
        z0 = sa * math.tan(th) - RAMP_THICKNESS / math.cos(th) + f.z0
        out.append((lo, hi, r.y0, r.y1, z0, z1) if f.axis == "x" else (r.y0, r.y1, lo, hi, z0, z1))
    return out


def sight_blockers(L):
    """(boxes, cylinders) that block a line of sight: every solid but the lava bed, free ramp slabs, Octs (radius d/2)."""
    boxes = [s[2:] for s in all_solids(L) if s[1] != "hazard"]
    for r in L.RAMPS:
        if r.fill == "none":
            boxes += _slab_boxes(L, r)
    return boxes, [(o.cx, o.cy, oct_r_block(o), o.z0, o.z1) for o in octs(L)]


def _los_points(L, name):
    """name -> [(label, (x, y, floor_z))]: a spawn id, an item id, a name in POINTS (x, y[, floor z]), or the groups `spawns` / `majors`."""
    items = {i.id: i for i in L.ITEMS}
    if name == "spawns":
        return [(s.id, (s.x, s.y, spawn_z(L, s))) for s in L.SPAWNS]
    if name == "majors":
        ids = getattr(L, "MAJOR_IDS", None) or (L.MEGA_ID, L.RA_ID)
        return [(i, (items[i].x, items[i].y, items[i].z)) for i in ids]
    pts = getattr(L, "POINTS", None) or {}
    if name in pts:
        v = pts[name]
        group = isinstance(v[0], (tuple, list))                 # a list of points is a group (labels name[0], name[1], ...)
        out = []
        for i, p in enumerate(v if group else [v]):
            out.append((f"{name}[{i}]" if group else name, (p[0], p[1], p[2] if len(p) > 2 else L.FLOOR_Z)))
        return out
    for s in L.SPAWNS:
        if s.id == name:
            return [(name, (s.x, s.y, spawn_z(L, s)))]
    if name in items:
        return [(name, (items[name].x, items[name].y, items[name].z))]
    raise KeyError(f"line of sight: unknown name {name!r} (spawn id, item id, POINTS entry, spawns, majors)")


def los_pairs(L, a, b):
    """[(label_a, label_b, clear)] for every pair of the two names: from the eye (LOS_EYE_Z above the source floor) to LOS_TARGET_Z above the target."""
    boxes, cyls = sight_blockers(L)
    eye = getattr(L, "EYE_Z", LOS_EYE_Z)
    out = []
    for la, pa in _los_points(L, a):
        for lb, pb in _los_points(L, b):
            if la == lb:
                continue
            p, q = (pa[0], pa[1], pa[2] + eye), (pb[0], pb[1], pb[2] + LOS_TARGET_Z)
            hit = any(seg_hits_box(p, q, bx) for bx in boxes) or any(seg_cyl_t(p, q, *c) is not None for c in cyls)
            out.append((la, lb, not hit))
    return out


def los(L, a, b):
    """True when EVERY pair of a and b has a clear line of sight (so `not los(...)` is not 'blocked'; use los_pairs for the pairs)."""
    return all(c for (_a, _b, c) in los_pairs(L, a, b))


def longest_sightline(L, floor_pt, rays=72, reach=SIGHTLINE_REACH):
    """Longest horizontal sight distance from the eye above `floor_pt` over `rays` directions (solids and Octs; the clip is not counted)."""
    boxes, cyls = sight_blockers(L)
    eye = getattr(L, "EYE_Z", LOS_EYE_Z)
    p = (floor_pt[0], floor_pt[1], floor_pt[2] + eye)
    best = 0.0
    for k in range(rays):
        a = 2.0 * math.pi * k / rays
        q = (p[0] + reach * math.cos(a), p[1] + reach * math.sin(a), p[2])
        t = min([x for x in (seg_box_t(p, q, bx) for bx in boxes) if x is not None]
                + [x for x in (seg_cyl_t(p, q, *c) for c in cyls) if x is not None] + [1.0])
        best = max(best, t * reach)
    return best


def _sightline_fig(d, reach=SIGHTLINE_REACH):
    """Figure for a sight distance: metres, or "open" when a ray left the map unobstructed (the cap says nothing about the map)."""
    return "open" if d >= reach - 1e-6 else round(d / 100.0, 1)


def check_los(L):
    """LOS_BLOCKED must be blocked, LOS_CLEAR clear. DM1 (no lists) keeps its old rule: no spawn sees Mega or RA. Returns (problems, figures)."""
    bad, fig = [], {}
    blocked = getattr(L, "LOS_BLOCKED", None)
    clear = getattr(L, "LOS_CLEAR", None)
    explicit = blocked is not None or clear is not None
    if blocked is None and clear is None:
        blocked = [("spawns", "majors")]
    # an entry is (a, b) or (a, b, tol): tol = the fraction of pairs allowed to break the rule (default 0); explicit entries print their figure
    for entry in blocked or ():
        a, b, tol = entry[0], entry[1], (entry[2] if len(entry) > 2 else 0.0)
        pairs = los_pairs(L, a, b)
        seen = [(la, lb) for (la, lb, c) in pairs if c]
        frac = len(seen) / len(pairs) if pairs else 0.0
        if explicit:
            fig[f"los_visible_pct {a} -> {b}"] = round(100.0 * frac, 2)
        if frac > tol + 1e-12:
            bad.append(f"line of sight {a} -> {b} must be blocked: {len(seen)} of {len(pairs)} pairs ({100.0 * frac:.1f} %) see each other, "
                       f"tolerance {100.0 * tol:.1f} %; first {seen[0][0]} -> {seen[0][1]}")
    for entry in clear or ():
        a, b, tol = entry[0], entry[1], (entry[2] if len(entry) > 2 else 0.0)
        pairs = los_pairs(L, a, b)
        hidden = [(la, lb) for (la, lb, c) in pairs if not c]
        frac = len(hidden) / len(pairs) if pairs else 0.0
        if explicit:
            fig[f"los_blocked_pct {a} -> {b}"] = round(100.0 * frac, 2)
        if frac > tol + 1e-12:
            bad.append(f"line of sight {a} -> {b} must be clear: {len(hidden)} of {len(pairs)} pairs ({100.0 * frac:.1f} %) are blocked, "
                       f"tolerance {100.0 * tol:.1f} %; first {hidden[0][0]} -> {hidden[0][1]}")
    if explicit:
        longest = 0.0
        for s in L.SPAWNS:
            d = longest_sightline(L, (s.x, s.y, spawn_z(L, s)))
            fig[f"{s.id}_sightline_m"] = _sightline_fig(d)
            longest = max(longest, d)
        fig["longest_sightline_m"] = _sightline_fig(longest)
    return bad, fig


# ---- link ends and drops
def link_end_problem(L, solids, pt, rl, inset_ring=16):
    """None, or why `pt` is not a ground point: no standing surface within link_end_z_tol, or a point link_end_inset away that is below it
    (the end lies less than the agent radius inside an edge). A wall or a higher neighbour at that distance is fine."""
    tol, inset = rl["link_end_z_tol"], rl["link_end_inset"]
    if not _supports(L, solids, pt[0], pt[1], pt[2], tol):
        return "on no standing surface (within %g cm in Z)" % tol
    for k in range(inset_ring):
        a = 2.0 * math.pi * k / inset_ring
        x, y = pt[0] + inset * math.cos(a), pt[1] + inset * math.sin(a)
        if not _solid_at(L, solids, x, y, pt[2], tol):
            return "less than %g cm inside the edge (open at %.0f deg)" % (inset, math.degrees(a))
    return None


def drop_problems(L, solids, k, rl):
    """ADR 0006 E3 (d) for a DROP end: drop_clear x drop_clear of clear top, drop_wall_ahead to the wall ahead, drop_pad_dist from any pad."""
    bad = []
    ex, ey, ez = k.end
    tol = rl["link_end_z_tol"]
    h = rl["drop_clear"] / 2.0
    n = int(h // 25.0)
    for i in range(-n, n + 1):
        for j in range(-n, n + 1):
            x, y = ex + i * 25.0, ey + j * 25.0
            if not _supports(L, solids, x, y, ez, tol) or _obstructs(L, solids, x, y, ez, tol=tol):
                bad.append(f"{k.id}: DROP end has no {rl['drop_clear']:.0f} x {rl['drop_clear']:.0f} clear top (blocked at {x:.0f}, {y:.0f})")
                break
        else:
            continue
        break
    dx, dy = ex - k.start[0], ey - k.start[1]
    dist = math.hypot(dx, dy)
    if dist > 1e-6:
        boxes, cyls = sight_blockers(L)
        ahead = (ex + dx / dist * rl["drop_wall_ahead"], ey + dy / dist * rl["drop_wall_ahead"])
        for zz in (ez + 40.0, ez + 140.0):
            p, q = (ex, ey, zz), (ahead[0], ahead[1], zz)
            if any(seg_hits_box(p, q, bx) for bx in boxes) or any(seg_cyl_t(p, q, *c) is not None for c in cyls):
                bad.append(f"{k.id}: DROP end has a wall less than {rl['drop_wall_ahead']:.0f} cm ahead")
                break
    for p in L.PADS:
        if math.hypot(p.x - ex, p.y - ey) < rl["drop_pad_dist"]:
            bad.append(f"{k.id}: DROP end is {math.hypot(p.x - ex, p.y - ey):.0f} cm from pad {p.id} (< {rl['drop_pad_dist']:.0f})")
    return bad


# ---- pad arcs
def pad_arc_clearance_problem(L, pad, solids, landing, clr):
    """None, or the first solid / Oct the feet arc passes closer than `clr` cm to between the launch and the landing. Ground at the pad
    (tops <= the pad floor) is ignored; a solid whose top is at or below the landing is only grown sideways (the arc has to come down onto it)."""
    v, _t_apex = pad_launch(L, pad)
    o = (pad.x, pad.y, pad_z(L, pad))

    def at(t):
        return (o[0] + v[0] * t, o[1] + v[1] * t, o[2] + v[2] * t - 0.5 * GRAVITY * t * t)

    lz = landing[2]
    prev = at(0.0)
    for i in range(1, int(5.0 / TRAJECTORY_DT)):
        cur = at(i * TRAJECTORY_DT)
        if i * TRAJECTORY_DT > _t_apex and cur[2] <= lz + 1.0:
            break
        for s in solids:
            if s[7] <= o[2] + 1.0:
                continue
            if s[2] <= landing[0] <= s[3] and s[4] <= landing[1] <= s[5] and 0.0 <= s[7] - lz <= 8.0:
                continue                                         # the landing deck itself (its top is the landing, +- the sample step)
            b = s[2:]
            grow_z = 0.0 if s[7] <= lz + 1.0 else clr
            box = (b[0] - clr, b[1] + clr, b[2] - clr, b[3] + clr, b[4] - grow_z, b[5] + grow_z)
            if seg_box_t(prev, cur, box) is not None:
                return f"{pad.id}: the arc passes within {clr:.0f} cm of solid {s[0]}"
        for oc in octs(L):
            if oc.z1 <= o[2] + 1.0:
                continue
            gz = 0.0 if oc.z1 <= lz + 1.0 else clr
            if seg_cyl_t(prev, cur, oc.cx, oc.cy, oct_r_clear(oc) + clr, oc.z0 - gz, oc.z1 + gz) is not None:
                return f"{pad.id}: the arc passes within {clr:.0f} cm of Oct {oc.id}"
        prev = cur
    return None


# ---- free ramps: the passage under the slab (ADR 0008 D41)
def ramp_passage(L, r, solids):
    """(width, how) of the best passage under a free-spanning ramp slab: the contiguous width (>= 160 cm clear) at the high end for a walk in
    along the run ('along'), or the clear stretch along the run across the whole band for a walk through crosswise ('across'). 0 if none."""
    f = ramp_frame(L, r)
    th = math.radians(f.deg)
    sign = f.d[0] if f.axis == "x" else f.d[1]

    def clearance(s, c):
        under = s * math.tan(th) + f.z0 - RAMP_THICKNESS / math.cos(th)
        x, y = (r.x0 + sign * s, c) if f.axis == "x" else (c, r.x0 + sign * s)
        g = L.FLOOR_Z
        for sd in solids:
            if sd[1] != "hazard" and sd[2] <= x <= sd[3] and sd[4] <= y <= sd[5]:
                if sd[6] < under - 1e-6 < sd[7]:
                    return 0.0
                if sd[7] <= under + 1e-6:
                    g = max(g, sd[7])
        return under - g

    cs = [r.y0 + i * 5.0 for i in range(int((r.y1 - r.y0) // 5.0) + 1)]
    best_w, run = 0.0, 0.0
    for c in cs:
        run = run + 5.0 if clearance(f.run - 5.0, c) >= 160.0 else 0.0
        best_w = max(best_w, run)
    best_l, run = 0.0, 0.0
    for i in range(int(f.run // 5.0) + 1):
        s = i * 5.0
        run = run + 5.0 if all(clearance(s, c) >= 160.0 for c in cs[::5] + [cs[-1]]) else 0.0
        best_l = max(best_l, run)
    return (best_w, "along") if best_w >= best_l else (best_l, "across")


# ---- hazards and Octs
def _in_hazard(h, p):
    return h.x0 <= p[0] <= h.x1 and h.y0 <= p[1] <= h.y1 and h.z0 <= p[2] <= h.z1


def check_hazards(L, solids, rl):
    bad = []
    hz = hazards(L)
    ids = [h.id for h in hz]
    if len(set(ids)) != len(ids):
        bad.append("duplicate hazard ids")
    for h in hz:
        if h.type not in HAZARD_TYPES:
            bad.append(f"{h.id}: hazard type {h.type!r} (lava | void)")
            continue
        if not (h.x0 < h.x1 and h.y0 < h.y1 and h.z0 < h.z1):
            bad.append(f"{h.id}: empty hazard box")
            continue
        pts = [(f"item {i.id}", (i.x, i.y, i.z)) for i in L.ITEMS]
        pts += [(f"spawn {s.id}", (s.x, s.y, spawn_z(L, s) + PLAYERSTART_Z)) for s in L.SPAWNS]
        pts += [(f"pad {p.id}", (p.x, p.y, pad_z(L, p))) for p in L.PADS] + [(f"pad {p.id} landing", tuple(p.land)) for p in L.PADS]
        for k in L.LINKS:
            pts += [(f"link {k.id} start", tuple(k.start)), (f"link {k.id} end", tuple(k.end))]
        for what, p in pts:
            if _in_hazard(h, p):
                bad.append(f"{h.id}: {what} {tuple(p)} lies inside the hazard")
        if h.type == "lava":
            if not any(s[1] == "hazard" and s[2] <= h.x0 and s[3] >= h.x1 and s[4] <= h.y0 and s[5] >= h.y1 and abs(s[7] - h.z0) <= 1.0
                       for s in solids):
                bad.append(f"{h.id}: lava has no Box of kind 'hazard' (the bed) under it, covering its footprint with its top at z0 {h.z0:g}")
            adj = [s[7] for s in solids if s[1] in ("floor", "platform", "bridge")
                   and s[2] <= h.x1 + 1.0 and s[3] >= h.x0 - 1.0 and s[4] <= h.y1 + 1.0 and s[5] >= h.y0 - 1.0
                   and not (s[2] < h.x1 - 1.0 and s[3] > h.x0 + 1.0 and s[4] < h.y1 - 1.0 and s[5] > h.y0 + 1.0)       # touching, not overlapping
                   and h.z1 < s[7] <= h.z1 + 200.0]
            if not adj:
                bad.append(f"{h.id}: lava has no adjoining floor top (within 200 cm above its surface)")
            for t in adj:
                if not 20.0 <= t - h.z1 <= 60.0:
                    bad.append(f"{h.id}: lava surface {h.z1:g} is {t - h.z1:.0f} cm below the adjoining floor top {t:g} (needs 20..60)")
                    break
    return bad


def check_octs(L, solids, rl):
    bad = []
    os_ = octs(L)
    ids = [o.id for o in os_] + [s[0] for s in solids]
    dup = [k for k, n in Counter(ids).items() if n > 1]
    if dup:
        bad.append(f"duplicate ids between Octs and solids {dup}")
    for o in os_:
        if not (o.d > 0.0 and o.z1 > o.z0):
            bad.append(f"{o.id}: empty Oct")
    gmin = rl["oct_gap_min"]
    for i, o in enumerate(os_):
        rc = oct_r_clear(o)
        for it in L.ITEMS:
            if math.hypot(it.x - o.cx, it.y - o.cy) < rc and o.z0 < it.z + 20.0 < o.z1:
                bad.append(f"item {it.id} is inside Oct {o.id}")
        for s in L.SPAWNS:
            if math.hypot(s.x - o.cx, s.y - o.cy) < rc + 34.0 and o.z0 < spawn_z(L, s) + PLAYERSTART_Z < o.z1:
                bad.append(f"spawn {s.id} is inside Oct {o.id}")
        for p in os_[i + 1:]:
            if o.z0 < p.z1 and p.z0 < o.z1:
                gap = math.hypot(o.cx - p.cx, o.cy - p.cy) - rc - oct_r_clear(p)
                if 0.0 < gap < gmin:
                    bad.append(f"Octs {o.id} and {p.id} leave a {gap:.0f} cm gap (touch, or leave >= {gmin:.0f})")
        for s in solids:
            if s[1] in ("floor", "lintel", "hazard") or not (s[6] < o.z1 - 1.0 and s[7] > o.z0 + 1.0):
                continue
            dx = max(s[2] - o.cx, 0.0, o.cx - s[3])
            dy = max(s[4] - o.cy, 0.0, o.cy - s[5])
            gap = math.hypot(dx, dy) - rc
            if 0.0 < gap < gmin:
                bad.append(f"Oct {o.id} leaves a {gap:.0f} cm gap to solid {s[0]} (touch, or leave >= {gmin:.0f})")
    return bad


# ---- free edges (report only)
def free_edges(L, step=50.0):
    """Walkable tops (kinds floor, platform, bridge) whose edge has a drop > free_edge_drop and neither a link start within 100 cm, a wall /
    higher neighbour / clip, nor a listed exemption (spec FREE_EDGE_EXEMPT = box ids). Returns {box id: free edge length in cm}."""
    rl = rules(L)
    solids = all_solids(L)
    exempt = set(getattr(L, "FREE_EDGE_EXEMPT", None) or ())
    clips = [(c.x0, c.x1, c.y0, c.y1, c.z0, c.z1) for c in L.CLIPS]
    starts = [k.start for k in L.LINKS]
    out = {}
    for b in L.BOXES:
        if b.kind not in ("floor", "platform", "bridge") or b.id in exempt:
            continue
        top = b.z1
        edges = [("x", b.x0, b.y0, b.y1, -1.0), ("x", b.x1, b.y0, b.y1, 1.0), ("y", b.y0, b.x0, b.x1, -1.0), ("y", b.y1, b.x0, b.x1, 1.0)]
        n_free = 0
        for (axis, fixed, lo, hi, sg) in edges:
            for i in range(max(1, int((hi - lo) // step))):
                along = (lo + hi) / 2 if hi - lo < step else lo + (i + 0.5) * step
                px, py = (fixed, along) if axis == "x" else (along, fixed)
                nrm = (sg, 0.0) if axis == "x" else (0.0, sg)
                ox, oy = px + nrm[0] * 10.0, py + nrm[1] * 10.0
                if any(_in_box((ox, oy, top + 60.0), (c[0], c[1], c[2], c[3], c[4], c[5])) for c in clips):
                    continue
                if any(math.hypot(sx - px, sy - py) <= 100.0 and abs(sz - top) <= 20.0 for (sx, sy, sz) in starts):
                    continue
                # a wall or a higher neighbour beside the edge protects it; otherwise find the ground below
                if any(s[1] != "hazard" and s[2] <= ox <= s[3] and s[4] <= oy <= s[5] and s[7] > top + 5.0 and s[6] < top + 180.0 for s in solids):
                    continue
                drop = None
                for s in solids:
                    if s[1] not in NONSTANDING_KINDS and s[2] <= ox <= s[3] and s[4] <= oy <= s[5] and s[7] <= top + 5.0:
                        drop = top - s[7] if drop is None else min(drop, top - s[7])
                for r in L.RAMPS:
                    rz = ramp_surface_z(L, r, ox, oy)
                    if rz is not None and rz <= top + 5.0:
                        drop = top - rz if drop is None else min(drop, top - rz)
                if drop is None or drop > rl["free_edge_drop"]:
                    n_free += 1
        if n_free:
            out[b.id] = n_free * step
    return out


def free_edge_figures(L):
    fe = free_edges(L)
    return {"free_edge_m": round(sum(fe.values()) / 100.0, 1), "free_edge_boxes": len(fe)}


# ---------------------------------------------------------------- validation (no editor needed)
def validate_layout(L):
    """Asserts the geometry rules of ADR 0006 section 3 on the records. Returns (list of problems, dict of figures)."""
    bad = []
    fig = {}
    rl = rules(L)
    solids = all_solids(L)
    ids = [s[0] for s in solids]
    dup = [k for k, n in Counter(ids).items() if n > 1]
    if dup:
        bad.append(f"duplicate solid ids {dup}")
    ids = [i.id for i in L.ITEMS]
    if len(set(ids)) != len(ids):
        bad.append("duplicate item ids")

    # walkable surfaces: width >= 120 (platforms, bridges, ledges), doors 240..400
    for b in L.BOXES:
        if b.kind in ("platform", "bridge") and b.z1 > 0.0:
            w = min(b.x1 - b.x0, b.y1 - b.y0)
            if w < rl["walkable_width"]:
                bad.append(f"{b.id}: walkable width {w:.0f} < {rl['walkable_width']:g}")
    for s in L.STAIRS:
        if abs(s.y1 - s.y0) < rl["walkable_width"]:
            bad.append(f"{s.id}: stairs width < 120")
    for r in L.RAMPS:
        f = ramp_frame(L, r)
        if f.deg >= rl["ramp_max_deg"]:
            bad.append(f"{r.id}: ramp angle {f.deg:g} deg >= 45.5 (walkable floor angle)")
        if r.deg is not None:                     # DM1 ramps use the global RAMP_DEG: no figure, so its golden stays identical
            fig[f"{r.id}_deg"] = f.deg
        if r.fill == "none":
            # free-spanning slab (ADR 0008 D41): the high end must leave >= 160 cm clear below the slab and the band must be >= 170 wide
            under = r.rise - RAMP_THICKNESS / math.cos(math.radians(f.deg))
            if under < 160.0:
                bad.append(f"{r.id}: free ramp has {under:.0f} cm clear under its high end (< 160)")
            if abs(r.y1 - r.y0) < 170.0:
                bad.append(f"{r.id}: free ramp is {abs(r.y1 - r.y0):.0f} cm wide (< 170)")
            width, how = ramp_passage(L, r, solids)       # the full passage test (P3 left it to P4)
            fig[f"{r.id}_under_w"] = round(width)
            if width < 170.0:
                bad.append(f"{r.id}: free ramp has no passage under the slab (>= 160 clear and >= 170 wide; best {width:.0f} cm {how})")
    for w in L.WALLS:
        for o in w.openings:
            width = o.o1 - o.o0
            if not rl["door_min"] <= width <= rl["door_max"]:
                bad.append(f"{w.id}: opening {o.o0}..{o.o1} is {width:.0f} wide, outside 240..400")
    # ledge depth: the A ledge, B balcony, D balcony are >= 120 deep by the width rule above
    # rooms: heights
    for r in L.ROOMS:
        need = {"atrium": 800.0, "room": 480.0, "corridor": 320.0}[r.cls]
        if r.ceiling < need:
            bad.append(f"room {r.id}: ceiling {r.ceiling:.0f} < {need:.0f} ({r.cls})")
    # pad landing zones >= 300 x 300 and the arc
    for p in L.PADS:
        zone = [b for b in L.BOXES if b.x0 <= p.land[0] <= b.x1 and b.y0 <= p.land[1] <= b.y1 and abs(b.z1 - p.land[2]) < 1.0]
        if not zone:
            bad.append(f"{p.id}: no walkable box under the landing point {p.land}")
        else:
            z = zone[0]
            if z.x1 - z.x0 < rl["pad_zone"] or z.y1 - z.y0 < rl["pad_zone"]:
                bad.append(f"{p.id}: landing zone {z.id} is {z.x1 - z.x0:.0f} x {z.y1 - z.y0:.0f} < 300 x 300")
        if p.target[2] - (pad_z(L, p) + PAD_TRIGGER_Z) <= 0.0:
            bad.append(f"{p.id}: target {tuple(p.target)} is not above the trigger centre")
            continue
        speed = math.sqrt(sum(c * c for c in pad_launch(L, p)[0]))
        if speed > rl["pad_max_launch_speed"]:
            bad.append(f"{p.id}: launch speed {speed:.0f} cm/s > PAD_MAX_LAUNCH_SPEED {rl['pad_max_launch_speed']:.0f}")
        landing, err = pad_arc(L, p, solids)
        if err:
            bad.append(err)
        else:
            d = math.hypot(landing[0] - p.land[0], landing[1] - p.land[1])
            fig[f"{p.id}_landing"] = (round(landing[0]), round(landing[1]), round(landing[2], 1))
            if d > rl["pad_land_tol"] or abs(landing[2] - p.land[2]) > rl["pad_land_tol_z"]:
                bad.append(f"{p.id}: simulated landing {tuple(round(c) for c in landing)} is {d:.0f} cm from the authored {p.land}")
            if rl["pad_arc_clearance"] > 0.0:
                why = pad_arc_clearance_problem(L, p, solids, pad_arc(L, p, solids, interpolate=True)[0], rl["pad_arc_clearance"])
                if why:
                    bad.append(why)
    # rocket-jump clearance
    for k in L.LINKS:
        if k.type != "ROCKET_JUMP":
            continue
        ceil = [c.z0 for c in L.CLIPS if c.x0 <= k.start[0] <= c.x1 and c.y0 <= k.start[1] <= c.y1 and c.z0 > k.start[2] and not c.id.startswith("Clip_")]
        ceil += [s[6] for s in solids if s[1] != "hazard" and s[2] <= k.start[0] <= s[3] and s[4] <= k.start[1] <= s[5] and s[6] > k.start[2]]
        clear = min(ceil) - k.start[2] if ceil else None
        if clear is None and rl["rj_open_sky"]:
            fig[f"{k.id}_clearance"] = "open"                 # nothing above the take-off: open sky is the best clearance there is
            continue
        fig[f"{k.id}_clearance"] = clear
        if clear is None or clear < L.RJ_CLEARANCE_MIN:
            bad.append(f"{k.id}: clearance above the take-off {clear} < {L.RJ_CLEARANCE_MIN:.0f}")
    # items: weapon ammo distance, spawns
    by_id = {i.id: i for i in L.ITEMS}
    for wid, aid in L.AMMO_FOR.items():
        a, w = by_id[aid], by_id[wid]
        d = math.hypot(a.x - w.x, a.y - w.y)
        fig[f"ammo_{wid}_m"] = round(d / 100.0, 1)
        if not L.AMMO_MIN_CM <= d <= L.AMMO_MAX_CM:
            bad.append(f"ammo {aid} is {d / 100:.1f} m from {wid}, outside 6..12 m")
    for i, s in enumerate(L.SPAWNS):
        for t in L.SPAWNS[i + 1:]:
            d = math.hypot(s.x - t.x, s.y - t.y)
            if d < L.SPAWN_MIN_APART:
                bad.append(f"spawns {s.id} and {t.id} are {d / 100:.1f} m apart (< 10 m)")
        dw = min(math.hypot(s.x - by_id[w].x, s.y - by_id[w].y) for w in L.WEAPON_IDS)
        fig[f"{s.id}_weapon_m"] = round(dw / 100.0, 1)
        if dw > L.SPAWN_MAX_WEAPON:
            bad.append(f"spawn {s.id} is {dw / 100:.1f} m from the nearest weapon (> 8 m)")
    los_bad, los_fig = check_los(L)         # LOS_BLOCKED / LOS_CLEAR; without them: no spawn sees Mega or RA (DM1, as before)
    bad += los_bad
    fig.update(los_fig)
    # nav links and pads cross-check
    pad_ids = {p.id for p in L.PADS}
    for k in L.LINKS:
        if k.type == "JUMP_PAD":
            p = next((p for p in L.PADS if p.id == k.pad), None)
            if p is None:
                bad.append(f"{k.id}: unknown pad {k.pad}")
            elif math.hypot(k.end[0] - p.land[0], k.end[1] - p.land[1]) > 60.0 or abs(k.end[2] - p.land[2]) > 30.0:
                bad.append(f"{k.id}: link end {k.end} is more than 60 cm from the pad landing {p.land}")
    # link ends on ground (hard on every map) and drop rule E3 (d)
    for k in L.LINKS:
        for what, pt in (("start", k.start), ("end", k.end)):
            why = link_end_problem(L, solids, pt, rl) if rl["link_end_ground"] else None
            if why:
                bad.append(f"{k.id}: link {what} {tuple(pt)} is {why}")
        if k.type == "DROP" and rl["drop_rule"]:
            bad += drop_problems(L, solids, k, rl)
    # counts a map declares in RULES (DM1: LEGACY_RULES, its ADR 0006 section 3 values); None = not checked
    if rl["link_counts"] is not None:
        got = {t: sum(1 for k in L.LINKS if k.type == t) for t in rl["link_counts"]}
        if got != dict(rl["link_counts"]):
            bad.append(f"links by type {got}, expected {dict(rl['link_counts'])}")
    if rl["pad_count"] is not None and len(pad_ids) != rl["pad_count"]:
        bad.append(f"expected {rl['pad_count']} pads, found {len(pad_ids)}")
    if rl["item_mix"] is not None:
        kinds = Counter(i.definition.rsplit("DA_Item_", 1)[1] for i in L.ITEMS)
        if dict(kinds) != dict(rl["item_mix"]):
            bad.append(f"item mix differs from the map's item_mix rule: {dict(kinds)}")
    quad = [i for i in L.ITEMS if i.definition.endswith("Powerup_Quad")]
    if quad and any(q.disabled_in_modes != (L.DUEL,) for q in quad):         # a map without a Quad has nothing to disable
        bad.append("the Quad must be disabled in Duel (FFA only)")
    # items stand on something: the floor point is within 1 cm of a solid top whose footprint contains it (or the ramp)
    tops = [s for s in solids]
    for it in L.ITEMS:
        on_ramp = any(ramp_surface_z(L, r, it.x, it.y) is not None and abs(it.z - ramp_surface_z(L, r, it.x, it.y)) < 1.0 for r in L.RAMPS)
        on_top = any(s[2] <= it.x <= s[3] and s[4] <= it.y <= s[5] and abs(s[7] - it.z) < 1.0 and s[1] not in ("lintel", "hazard") for s in tops)
        on_top = on_top or any(math.hypot(it.x - o.cx, it.y - o.cy) <= oct_r_block(o) and abs(o.z1 - it.z) < 1.0 for o in octs(L))
        if not (on_ramp or on_top):
            bad.append(f"item {it.id} at ({it.x}, {it.y}, {it.z}) stands on no solid top")
        inside = [s[0] for s in tops if s[2] < it.x < s[3] and s[4] < it.y < s[5] and s[6] < it.z + 20.0 < s[7]]
        if inside:
            bad.append(f"item {it.id} is inside solid {inside}")
    bad += check_octs(L, solids, rl)
    bad += check_hazards(L, solids, rl)
    if rl["free_edge_report"]:
        fig.update(free_edge_figures(L))
    return bad, fig


# ---------------------------------------------------------------- art mode: kit placement plan (pure Python, no editor)
KIT_MESH_DIR = "/Game/Environment/Kit/Meshes/SM_Kit_"
# piece -> (size X, Y, Z) of the unscaled mesh, from docs/art/kit-spec.md
KIT = {
    "Floor_400": (400.0, 400.0, 20.0), "Ceiling_400": (400.0, 400.0, 20.0), "Wall_400": (400.0, 20.0, 400.0),
    "Wall_Trim": (400.0, 25.0, 400.0), "Wall_Door300": (400.0, 30.0, 400.0), "Wall_Door400": (400.0, 30.0, 100.0),
    "Ramp_15": (400.0, 400.0, 107.2), "Stairs_5x20": (400.0, 400.0, 100.0), "Bridge_400": (400.0, 200.0, 60.0),
    "Light_Fixture": (200.0, 30.0, 20.0), "Ledge_Edge": (400.0, 15.0, 40.0), "Pad_Ring": (300.0, 300.0, 5.0),
}
WALL_T_KIT = 20.0            # the wall slab of Wall_400 (its room face is at local Y = 20)
KIT_TILE = 400.0
WALL_TILE_MAX = 1000.0        # largest wall tile edge (scaled Wall_400; ADR 0006 section 8 caps the component count, not the texel density)
FLOOR_GRID = 800.0           # world grid of floor tiles (scaled Floor_400)
CEIL_GRID = 1200.0           # ceilings are 4-12 m overhead: coarser
FLOOR_SLAB = 20.0            # Floor_400 / Ceiling_400 thickness
BRIDGE_HANG = 40.0           # Bridge_400 scaled in Z to this thickness: 20 cm below the 20 cm clip deck
TRIM_RECESS = 0.3            # Wall_Trim is 5 cm proud of its face; 0.3 cm in keeps the audit depth strictly under 5 cm
JAMB_RECESS = 1.0            # jamb tiles sit 1 cm inside the clip so a door frame's inner face never z-fights with them
KIT_TREAD = 80.0             # Stairs_5x20 tread; the layout's TREAD is 50, so the piece is scaled 0.625 in X
SAMPLE_OFF = 5.0
LM_FLOOR_OVERRIDE = 64       # lightmap resolution override for floor/ceiling tiles beyond the first LM_FLOOR_FULL_RES tiles
Inst = namedtuple("Inst", "mesh x y z yaw sx sy sz layout lm pitch", defaults=(None, 0.0))


def _in_box(p, b):
    return b[0] < p[0] < b[1] and b[2] < p[1] < b[3] and b[4] < p[2] < b[5]


def _in_ramp_body(L, p):
    for r in L.RAMPS:
        if r.fill == "none":
            continue
        f = ramp_frame(L, r)
        s_run, cross = _ramp_local(f, r, p[0], p[1])
        z = ramp_surface_z(L, r, p[0], p[1])
        if z is not None and f.band[0] < cross < f.band[1] and 0.0 < s_run < f.run and f.z0 < p[2] < z:
            return True
    return False


class _Cover:
    """Point-in-solid test over every collision solid (boxes, wall strips, stair steps, ramp fills and body, clip ceilings)."""

    def __init__(self, L):
        self.L = L
        self.boxes = [s[2:] for s in all_solids(L)] + [(c.x0, c.x1, c.y0, c.y1, c.z0, c.z1) for c in L.CLIPS]

    def covered(self, p):
        return _in_ramp_body(self.L, p) or any(_in_box(p, b) for b in self.boxes)


def _split(lo, hi, size=KIT_TILE, grid=None):
    """Cells covering lo..hi. grid=None: equal split into ceil(len/size) cells. grid=g: cells aligned to multiples of g (texture phase),
    slivers under 100 cm merged into the neighbouring cell."""
    if hi - lo <= 1e-6:
        return []
    if grid is None:
        n = max(1, int(math.ceil((hi - lo) / size - 1e-9)))
        step = (hi - lo) / n
        return [(lo + i * step, lo + (i + 1) * step) for i in range(n)]
    cuts = [lo]
    k = math.floor(lo / grid + 1e-9) + 1
    while k * grid < hi - 1e-6:
        if k * grid > lo + 1e-6:
            cuts.append(k * grid)
        k += 1
    cuts.append(hi)
    cells = [(cuts[i], cuts[i + 1]) for i in range(len(cuts) - 1)]
    if len(cells) > 1 and cells[0][1] - cells[0][0] < 100.0:
        cells[1] = (cells[0][0], cells[1][1])
        cells.pop(0)
    if len(cells) > 1 and cells[-1][1] - cells[-1][0] < 100.0:
        cells[-2] = (cells[-2][0], cells[-1][1])
        cells.pop()
    return cells


def _vface_origin(nrm, c, a0, a1, z, thick=WALL_T_KIT):
    """Origin and yaw of a Wall_400-type piece (local +Y = room side, room face at local Y = thick) whose room face lies on the plane
    through `c` with outward normal `nrm` and spans a0..a1 along the face; the piece sits behind the face (inside the solid).
    yaw 0: local +Y = +Y; 90: -X; 180: -Y; 270: +X (Unreal yaw turns +X toward +Y)."""
    if nrm == (0, 1):
        return (a0, c - thick, z), 0.0
    if nrm == (0, -1):
        return (a1, c + thick, z), 180.0
    if nrm == (1, 0):
        return (c - thick, a1, z), 270.0
    return (c + thick, a0, z), 90.0                      # (-1, 0)


def _exposed(cover, nrm, c, a0, a1, z0, z1):
    pts = []
    for u in (a0 + SAMPLE_OFF, (a0 + a1) / 2, a1 - SAMPLE_OFF):
        for v in (z0 + SAMPLE_OFF, (z0 + z1) / 2, z1 - SAMPLE_OFF):
            if nrm[0]:
                pts.append((c + nrm[0] * SAMPLE_OFF, u, v))
            else:
                pts.append((u, c + nrm[1] * SAMPLE_OFF, v))
    return not all(cover.covered(p) for p in pts)


def vertical_tiles(cover, layout, nrm, c, a0, a1, z0, z1, base_trim=False, recess=0.0, check=True):
    """Wall_400 (Wall_Trim on the bottom row when base_trim) tiles over one vertical face; fully covered tiles are skipped."""
    out = []
    for ri, (rz0, rz1) in enumerate(_split(z0, z1, WALL_TILE_MAX)):
        for (ta, tb) in _split(a0, a1, WALL_TILE_MAX):
            if check and not _exposed(cover, nrm, c, ta, tb, rz0, rz1):
                continue
            mesh = "Wall_Trim" if (base_trim and ri == 0 and abs(rz0) < 1e-6) else "Wall_400"
            rc = recess + (TRIM_RECESS if mesh == "Wall_Trim" else 0.0)       # the trim stands 5 cm proud: sink it so it stays under the limit
            (ox, oy, oz), yaw = _vface_origin(nrm, c - rc * (nrm[0] + nrm[1]), ta, tb, rz0)
            out.append(Inst(mesh, ox, oy, oz, yaw, (tb - ta) / KIT_TILE, 1.0, (rz1 - rz0) / KIT_TILE, layout, None))
    return out


def horizontal_tiles(cover, layout, mesh, x0, x1, y0, y1, z, up=True, lm=None, grid=FLOOR_GRID):
    """Floor_400 (up: top face at z, slab below) or Ceiling_400 (underside at z, slab above) tiles over a rectangle on the 400 cm world
    grid; cells whose sample points are all inside solids are skipped."""
    out = []
    for (xa, xb) in _split(x0, x1, grid=grid):
        for (ya, yb) in _split(y0, y1, grid=grid):
            off = SAMPLE_OFF if up else -SAMPLE_OFF
            pts = [(px, py, z + off) for px in (xa + 5, (xa + xb) / 2, xb - 5) for py in (ya + 5, (ya + yb) / 2, yb - 5)]
            if all(cover.covered(p) for p in pts):
                continue
            out.append(Inst(mesh, xa, ya, z, 0.0, (xb - xa) / KIT_TILE, (yb - ya) / KIT_TILE, 1.0, layout, lm))
    return out


def bridge_tiles(layout, x0, x1, y0, y1, ztop):
    """Bridge_400 decks (400 x 200, runs along its local +X) over a bridge box; the long side of the box is the run."""
    out = []
    along_x = (x1 - x0) >= (y1 - y0)
    la, lb = ((x0, x1), (y0, y1)) if along_x else ((y0, y1), (x0, x1))
    for (a0, a1) in _split(la[0], la[1], 400.0):
        for (b0, b1) in _split(lb[0], lb[1], 200.0):
            sx, sy, sz = (a1 - a0) / 400.0, (b1 - b0) / 200.0, BRIDGE_HANG / 60.0
            if along_x:
                out.append(Inst("Bridge_400", a0, b0, ztop, 0.0, sx, sy, sz, layout, None))
            else:       # yaw 90: local +X -> +Y, local +Y -> -X; origin at the min Y / max X corner of the cell
                out.append(Inst("Bridge_400", b1, a0, ztop, 90.0, sx, sy, sz, layout, None))
    return out


def record_of(L, sid):
    for suffix in ("_seg", "_sill", "_lintel"):
        if suffix in sid:
            return sid.split(suffix)[0]
    if "_Fill_" in sid:
        return sid.split("_Fill_")[0]
    for s in L.STAIRS:
        if sid.startswith(s.id + "_"):
            return s.id
    return sid


def stairs_plan(L, cover, s):
    """Stairs from Floor_400 slabs, one per clip step (top at the step top; the 20 cm slab edge is the riser) plus Wall_400 fill tiles on
    the exposed sides in groups of five steps. Stairs_5x20 is not used: its bounds contain the air above the steps, which the audit's
    protrusion rule (bounds against clip) must flag by construction."""
    out = []
    steps = [t for t in expand_stairs(L, s)]
    f = stairs_frame(L, s)
    for (sid, _k, x0, x1, y0, y1, z0, z1) in steps:
        out.append(Inst("Floor_400", x0, y0, z1, 0.0, (x1 - x0) / 400.0, (y1 - y0) / 400.0, 1.0, s.id, None))
    for g in range(0, len(steps), 5):
        grp = steps[g:g + 5]
        ia, ib = (2, 3) if f.axis == "x" else (4, 5)
        xa = min(t[ia] for t in grp)
        xb = max(t[ib] for t in grp)
        low = min(t[7] for t in grp) - FLOOR_SLAB
        if low > f.z0 + 1.0:
            sides = (((0, 1), s.y1), ((0, -1), s.y0)) if f.axis == "x" else (((1, 0), s.y1), ((-1, 0), s.y0))
            for nrm, c in sides:
                out += vertical_tiles(cover, s.id, nrm, c, xa, xb, f.z0, low)
    return out


def ramp_plan(L, cover, r):
    """The ramp surface from Bridge_400 plates (Plate material) pitched `deg` and squashed to the 20 cm clip slab; the plates lie exactly
    on the slab's oriented box. Ramp_15 is not used (bounds contain the air above the slope, see stairs_plan). The plates are yawed
    to the run direction; a plate's local +Y runs to the left of the run direction, so the cross offset starts at the band's low
    end when that is +axis and at its high end otherwise."""
    f = ramp_frame(L, r)
    th = math.radians(f.deg)
    yaw = ramp_yaw(r)
    out = []
    n = max(1, int(round(f.length / 400.0)))
    seg = f.length / n
    nw = max(1, int(round((r.y1 - r.y0) / 200.0)))
    wseg = (r.y1 - r.y0) / nw
    run_d = f.d[0] if f.axis == "x" else f.d[1]
    cross_up = (run_d > 0) if f.axis == "x" else (run_d < 0)
    for k in range(n):
        for j in range(nw):
            run_c = r.x0 + run_d * (k * seg * math.cos(th))
            cross_c = r.y0 + j * wseg if cross_up else r.y1 - j * wseg
            x, y = (run_c, cross_c) if f.axis == "x" else (cross_c, run_c)
            out.append(Inst("Bridge_400", x, y, k * seg * math.sin(th) + f.z0, yaw,
                            seg / 400.0, wseg / 200.0, RAMP_THICKNESS / 60.0, r.id, None, f.deg))
    return out


def opening_plan(cover, w):
    """Jamb tiles for every opening of one wall record and a Door400 lintel cap (both faces) over the door-sized ones (<= 400 high)."""
    out = []
    for o in sorted(w.openings):
        width = o.o1 - o.o0
        door = (o.zhi - o.zlo) <= 400.0 and o.zhi < w.top and width <= 400.0
        top = min(o.zhi, w.top)
        for nrm, c in jamb_faces(w, o):
            out += vertical_tiles(cover, w.id, nrm, c, w.b0, w.b1, o.zlo, top, recess=JAMB_RECESS, check=False)
        if door:
            mid = (o.o0 + o.o1) / 2.0
            faces = [((0, 1), w.b1), ((0, -1), w.b0)] if w.run == "x" else [((1, 0), w.b1), ((-1, 0), w.b0)]
            for nrm, c in faces:
                (ox, oy, oz), yaw = _vface_origin(nrm, c, mid - 200.0, mid + 200.0, 0.0)
                out.append(Inst("Wall_Door400", ox, oy, o.zhi - 320.0, yaw, 1.0, 1.0, 1.0, w.id, None))
    return out


def jamb_faces(w, o):
    """Cut faces of an opening: (normal pointing into the opening, plane coordinate along the run axis)."""
    if w.run == "x":
        return [((1, 0), o.o0), ((-1, 0), o.o1)]
    return [((0, 1), o.o0), ((0, -1), o.o1)]


def plan_art(L):
    """The kit placement plan: a list of Inst. Pure arithmetic over the layout table (testable outside the editor)."""
    cover = _Cover(L)
    plan = []
    for (sid, kind, x0, x1, y0, y1, z0, z1) in all_solids(L):
        if kind == "step":
            continue
        rec = record_of(L, sid)
        top = []
        if kind == "bridge":
            top = bridge_tiles(rec, x0, x1, y0, y1, z1)
        elif kind in ("floor", "platform", "wall") and "_Fill_" not in sid:
            top = horizontal_tiles(cover, rec, "Floor_400", x0, x1, y0, y1, z1, up=True)
        plan += top
        if kind in ("floor", "bridge"):
            continue
        zt = z1 - FLOOR_SLAB if top else z1
        for nrm, c, a0, a1 in (((1, 0), x1, y0, y1), ((-1, 0), x0, y0, y1), ((0, 1), y1, x0, x1), ((0, -1), y0, x0, x1)):
            if "_Fill_" in sid and nrm[0]:
                continue
            plan += vertical_tiles(cover, rec, nrm, c, a0, a1, z0, zt, base_trim=(kind == "wall"))
    for w in L.WALLS:
        plan += opening_plan(cover, w)
    for s in L.STAIRS:
        plan += stairs_plan(L, cover, s)
    for r in L.RAMPS:
        plan += ramp_plan(L, cover, r)
    for c in L.CLIPS:
        if c.id.startswith("Ceil_"):
            plan += horizontal_tiles(cover, c.id, "Ceiling_400", c.x0, c.x1, c.y0, c.y1, c.z0, up=False, grid=CEIL_GRID)
    for lt in L.LIGHTS:
        if lt.ceiling is not None:         # fixture top flush with the ceiling underside
            plan.append(Inst("Light_Fixture", lt.x - 100.0, lt.y - 15.0, lt.ceiling - 20.0, 0.0, 1.0, 1.0, 1.0, lt.id, None))
    for p in L.PADS:                       # ring sunk 1 cm so it stays within the 5 cm protrusion limit
        plan.append(Inst("Pad_Ring", p.x - 150.0, p.y - 150.0, pad_z(L, p) - 1.0, 0.0, 1.0, 1.0, 1.0, p.id, None))
    return plan


# local bounding box of each piece (x0, x1, y0, y1, z0, z1) before scale and rotation: docs/art/kit-spec.md pivots
LOCAL_BOX = {
    "Floor_400": (0, 400, 0, 400, -20, 0), "Ceiling_400": (0, 400, 0, 400, 0, 20), "Wall_400": (0, 400, 0, 20, 0, 400),
    "Wall_Trim": (0, 400, 0, 25, 0, 400), "Wall_Door400": (0, 400, -5, 25, 300, 400), "Bridge_400": (0, 400, 0, 200, -60, 0),
    "Light_Fixture": (0, 200, 0, 30, 0, 20), "Pad_Ring": (0, 300, 0, 300, 0, 5),
}


def inst_box(i, audit):
    """The audit's oriented box (mesh bounding box in world space) of one instance."""
    lb = LOCAL_BOX[i.mesh]
    c = ((lb[0] + lb[1]) / 2 * i.sx, (lb[2] + lb[3]) / 2 * i.sy, (lb[4] + lb[5]) / 2 * i.sz)
    h = ((lb[1] - lb[0]) / 2 * i.sx, (lb[3] - lb[2]) / 2 * i.sy, (lb[5] - lb[4]) / 2 * i.sz)
    m = audit._rot_matrix(i.pitch, i.yaw, 0.0)
    w = tuple((i.x, i.y, i.z)[k] + sum(m[k][j] * c[j] for j in range(3)) for k in range(3))
    return audit.oriented_box(w, h, (i.pitch, i.yaw, 0.0))


def clip_oriented_boxes(L, audit):
    """Every pawn-blocking box of the map as the audit sees it: solids, clips, the ramp slabs."""
    out = [audit.oriented_box(((s[2] + s[3]) / 2, (s[4] + s[5]) / 2, (s[6] + s[7]) / 2), ((s[3] - s[2]) / 2, (s[5] - s[4]) / 2, (s[7] - s[6]) / 2), (0, 0, 0))
           for s in all_solids(L)]
    out += [audit.oriented_box(((c.x0 + c.x1) / 2, (c.y0 + c.y1) / 2, (c.z0 + c.z1) / 2), ((c.x1 - c.x0) / 2, (c.y1 - c.y0) / 2, (c.z1 - c.z0) / 2), (0, 0, 0))
            for c in L.CLIPS]
    for r in L.RAMPS:
        f = ramp_frame(L, r)
        th = math.radians(f.deg)
        off = f.length / 2 * math.cos(th) + RAMP_THICKNESS / 2 * math.sin(th)
        cz = f.z0 + f.length / 2 * math.sin(th) - RAMP_THICKNESS / 2 * math.cos(th)
        out.append(audit.oriented_box((f.foot[0] + f.d[0] * off, f.foot[1] + f.d[1] * off, cz),
                                      (f.length / 2, (r.y1 - r.y0) / 2, RAMP_THICKNESS / 2), (f.deg, ramp_yaw(r), 0)))
    return out


def plan_protrusion(L, plan, limit=4.99):
    """Offline mirror of `audit_map.py --protrusion` (same sample lattice, same walkable-air rule, boxes prefiltered by distance):
    returns [(Inst, depth)] for every instance deeper than `limit`."""
    import audit_map as audit
    clips = clip_oriented_boxes(L, audit)
    aabbs = []
    for b in clips:
        pts = audit.box_corners(b)
        aabbs.append((min(p[0] for p in pts), max(p[0] for p in pts), min(p[1] for p in pts), max(p[1] for p in pts),
                      min(p[2] for p in pts), max(p[2] for p in pts)))
    bad = []
    for i in plan:
        mb = inst_box(i, audit)
        pts = audit.box_corners(mb)
        lo = [min(p[k] for p in pts) for k in range(3)]
        hi = [max(p[k] for p in pts) for k in range(3)]
        near = [b for b, a in zip(clips, aabbs) if a[1] >= lo[0] - 8 and a[0] <= hi[0] + 8 and a[3] >= lo[1] - 8 and a[2] <= hi[1] + 8
                and a[5] >= lo[2] - 260 and a[4] <= hi[2] + 8]
        worst = 0.0
        for p in audit.sample_points(mb):
            if audit.walkable_air(p, near):
                d = min([audit.point_box_distance(p, b) for b in near] or [0.0])
                if d > worst:
                    worst = d
        if worst > limit:
            bad.append((i, worst))
    return bad


# ---------------------------------------------------------------- editor part
def spawn_geometry(L, clip=False):
    for (sid, kind, x0, x1, y0, y1, z0, z1) in all_solids(L):
        rec = sid
        # layout tag = the record id (before the first piece suffix)
        for suffix in ("_seg", "_sill", "_lintel"):
            if suffix in sid:
                rec = sid.split(suffix)[0]
        if sid.endswith(tuple(f"_{n:02d}" for n in range(32))) and any(sid.startswith(s.id) for s in L.STAIRS):
            rec = sid.rsplit("_", 1)[0]
        if "_Fill_" in sid:
            rec = sid.split("_Fill_")[0]
        vm.box(("Clip_" + sid) if clip else sid, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0),
               material_of(L, kind, sid), kind, folder=("Rooms" if kind in ("floor", "wall", "lintel") else "Levels"), layout=rec, clip=clip)
    for o in octs(L):
        vm.octagon(("Clip_" + o.id) if clip else o.id, (o.cx, o.cy), o.d, o.z0, o.z1, o.mat, o.kind, folder="Levels", layout=o.id, clip=clip)
    for r in L.RAMPS:
        f = ramp_frame(L, r)
        vm.ramp(("Clip_" + r.id) if clip else r.id, f.foot, f.length, r.y1 - r.y0, RAMP_THICKNESS, f.deg,
                "RampWalkable", kind="ramp", folder="Levels", layout=r.id, clip=clip, yaw=ramp_yaw(r))


def spawn_hazards(L):
    """AVeloxHazardVolume per Hazard record (veloxmap.hazard(), task H2: it needs the C++ class). The lava bed is an ordinary 'hazard' Box."""
    for h in hazards(L):
        if not hasattr(vm, "hazard"):
            vm.fail(f"{h.id}: veloxmap.hazard() does not exist yet (task H2); a layout with Hazard records cannot be built before it")
            return
        vm.hazard(h.id, h.type, ((h.x0 + h.x1) / 2, (h.y0 + h.y1) / 2, (h.z0 + h.z1) / 2), (h.x1 - h.x0, h.y1 - h.y0, h.z1 - h.z0), layout=h.id)


def spawn_clips(L):
    for c in L.CLIPS:
        a = vm.clip_box(c.id, ((c.x0 + c.x1) / 2, (c.y0 + c.y1) / 2, (c.z0 + c.z1) / 2), (c.x1 - c.x0, c.y1 - c.y0, c.z1 - c.z0))
        vm.tag_actor(a, c.id)


CLIP_MODE_ACTORS = {"box": 5, "walls": 4, "none": 0}


def clip_mode(L):
    """The spec module's CLIP_MODE: None (default: the layout's own CLIPS records only, as DM1) | "box" | "walls" | "none"."""
    mode = getattr(L, "CLIP_MODE", None)
    if mode is not None and mode not in CLIP_MODE_ACTORS:
        raise ValueError(f"CLIP_MODE {mode!r} (box | walls | none)")
    return mode


def spawn_perimeter_clip(L):
    """Perimeter clip walls (and ceiling for "box") from CLIP_MODE; needs CLIP_HALF = (half_x, half_y) in the spec module."""
    mode = clip_mode(L)
    if mode in (None, "none"):
        return
    hx, hy = L.CLIP_HALF
    vm.clip_walls(hx, hy, mode=mode, wall_t=getattr(L, "CLIP_WALL_T", 100.0))


INSTANCE_GATE_TAG = "DisabledInMode:"      # actor tag prefix: a per-instance mode gate (the definition is per type; C++ reads the tag, task B-level)


def item_instance_gates(it, definition_text):
    """The modes of `it.disabled_in_modes` that its definition asset does not already list: those become per-instance actor tags
    (YA_N FFA-only next to a Yellow Armor that is on in every mode). `definition_text` is the DA's DisabledInModes export text ("" if no DA)."""
    return tuple(INSTANCE_GATE_TAG + tag for tag in it.disabled_in_modes if tag not in definition_text)


def spawn_items(L):
    by_label = {}
    for it in L.ITEMS:
        da = vm.EAL.load_asset(it.definition) if vm.EAL.does_asset_exist(it.definition) else None
        text = ""
        if da is not None:
            exp = da.get_editor_property("disabled_in_modes")
            text = str(exp.export_text()) if hasattr(exp, "export_text") else str(exp)
        a, da = vm.pickup(it.definition, (it.x, it.y, it.z), tags=item_instance_gates(it, text), name="Pickup_" + it.id, folder="Items",
                          layout=it.id)
        by_label[it.id] = a
        if da is not None and not it.disabled_in_modes:
            if "TagName" in text and "Velox.Mode" in text:
                vm.fail(f"{it.id}: definition lists modes in DisabledInModes but the layout does not")


def spawn_pads_and_links(L, markers=True):
    pads = {}
    for p in L.PADS:
        pads[p.id] = vm.jump_pad(p.id, (p.x, p.y), p.target, pad_label=p.id, target_label=p.id + "_Target", folder="Pads", layout=p.id,
                                floor_z=pad_z(L, p))
        if markers:
            vm.cylinder_marker(p.id + "_LandingMarker", (p.land[0], p.land[1], p.land[2] + 2.0), (120, 120, 4), "Target", folder="Pads")
    for k in L.LINKS:
        vm.nav_link(k.id, getattr(unreal.VeloxNavLinkType, k.type), k.start, k.end, pad=pads.get(k.pad),
                    both_ways=k.both_ways, required_speed=k.required_speed, folder="NavLinks", layout=k.id)


def spawn_starts_and_camera(L):
    for s in L.SPAWNS:
        vm.player_start(s.id, (s.x, s.y, spawn_z(L, s) + PLAYERSTART_Z), s.yaw, folder="Starts", layout=s.id)
    loc, rot = L.INTERMISSION_CAMERA
    vm.spawn(unreal.CameraActor, "IntermissionCamera", loc, unreal.Rotator(*rot), "Starts", "camera", tags=("Intermission",))
    label = vm.text_label("Label_MapName", L.INTERMISSION_LABEL, L.DISPLAY_NAME, yaw=90.0, size=300.0, folder="Markers")
    label.set_actor_rotation(unreal.Rotator(0, 90, 90), False)


def set_game_mode():
    settings = unreal.EditorLevelLibrary.get_editor_world().get_world_settings()
    cls = getattr(unreal, "VeloxGameMode_FFA", None) or unreal.VeloxGameMode
    settings.set_editor_property("default_game_mode", cls)
    got = settings.get_editor_property("default_game_mode")
    vm.log(f"WorldSettings.DefaultGameMode = {got.get_name() if got else None}")
    if got is None:
        vm.fail("could not set WorldSettings.DefaultGameMode")


def expected_counts(L):
    exp = dict(L.EXPECTED)
    exp["clip"] = len(L.CLIPS) + CLIP_MODE_ACTORS.get(clip_mode(L), 0)
    if not exp["clip"]:
        del exp["clip"]          # M7 B5: a map without any clip (Orbita, CLIP_MODE "none") spawns none; veloxmap.COUNTS has no key then
    exp["ramp"] = len(L.RAMPS)
    for (_, kind, *_rest) in all_solids(L):
        exp[kind] = exp.get(kind, 0) + 1
    exp["target"] = len(L.PADS)       # cylinder landing markers
    for o in octs(L):                 # four boxes per Oct, counted under the Oct's own kind
        exp[o.kind] = exp.get(o.kind, 0) + 4
    if hazards(L):
        exp["hazardvolume"] = len(hazards(L))      # AVeloxHazardVolume actors (veloxmap.hazard(), task H2)
    return exp


def build_map(L):
    les = vm.rebuild_map(blockout_path(L))
    if les is None:
        return
    spawn_geometry(L)
    spawn_hazards(L)
    spawn_clips(L)
    spawn_perimeter_clip(L)
    spawn_items(L)
    spawn_pads_and_links(L)
    spawn_starts_and_camera(L)
    vm.static_sun_and_sky()
    vm.nav_bounds(L.NAV_CENTER, L.NAV_SIZE)
    vm.set_kill_z(L.KILL_Z)
    set_game_mode()
    vm.check_pads()
    expected = expected_counts(L)
    # the clip_box helper counts under "clip" itself; the label counts under "text"; cylinder markers under "marker"
    vm.assert_counts(expected, exact=True)
    actors = vm.EAS.get_all_level_actors()
    ignored = [a.get_actor_label() for a in actors if "NavLintIgnore" in [str(t) for t in a.get_editor_property("tags")]]
    if ignored:
        vm.fail(f"actors carry NavLintIgnore (forbidden on {L.MAP_ID}, E0.2 b): {ignored}")
    untagged = [a.get_actor_label() for a in actors
                if not any(str(t).startswith("Layout:") for t in a.get_editor_property("tags"))
                and not isinstance(a, (unreal.WorldSettings,))]
    # engine-owned actors (brush, world settings) are not ours; report, do not fail
    if untagged:
        vm.log("actors without a Layout tag: " + ", ".join(sorted(untagged)))
    pickups = [a for a in actors if isinstance(a, unreal.VeloxPickup)]
    links = [a for a in actors if isinstance(a, unreal.VeloxNavLink)]
    kinds = Counter(str(a.get_editor_property("type")) for a in links)
    vm.log(f"VeloxPickups={len(pickups)} VeloxNavLinks={len(links)} by type {dict(kinds)}")
    vm.save_map(les)


def art_plan(L):
    """plan_art() with the lightmap resolution policy applied (ADR 0006 section 8: at most 120 components above 64): the ground tiles keep
    the mesh's 128, every other floor tile 64, stair step slabs 32."""
    stair_ids = {s.id for s in L.STAIRS}
    out = []
    for i in plan_art(L):
        if i.mesh == "Floor_400":
            i = i._replace(lm=32 if i.layout in stair_ids else (None if i.layout == "Ground" else 64))
        out.append(i)
    return out


def art_counts(L, plan):
    exp = expected_counts(L)
    exp.pop("light", None)             # the blockout's static sun + sky; the art map has a cubemap sky light and point lights
    exp.pop("target", None)            # no landing marker discs in the art map
    exp["kit"] = len(plan)
    exp["pointlight"] = len(L.LIGHTS)
    exp["reflection"] = len(L.CAPTURES)
    exp["skylight"] = 1
    exp["lightmassvolume"] = 1
    return exp


def spawn_kit(plan):
    seq = Counter()
    for i in plan:
        seq[i.mesh] += 1
        vm.kit_piece(f"Kit_{i.mesh}_{seq[i.mesh]:03d}", KIT_MESH_DIR + i.mesh, (i.x, i.y, i.z), yaw=i.yaw, pitch=i.pitch,
                     scale=(i.sx, i.sy, i.sz), layout=i.layout, lm_res=i.lm, folder="Kit")


def spawn_lighting(L):
    for lt in L.LIGHTS:
        vm.static_point_light(lt.id, (lt.x, lt.y, lt.z), lt.lumens, lt.rgb, lt.radius, layout=lt.id)
    for (cid, x, y, z, radius) in L.CAPTURES:
        vm.sphere_reflection_capture(cid, (x, y, z), radius, layout=cid)
    vm.sky_light(sky_cubemap(L))
    vm.lightmass_importance_volume(*L.LIGHTMASS_VOLUME)
    vm.lightmass_world_settings()


def build_art_map(L):
    """L_<MAP>: the blockout boxes as invisible clip (same transforms, collision and nav influence), the kit as visual-only instances,
    the same pads / items / spawns / links / nav bounds / KillZ, static lights, reflection captures, cubemap sky light."""
    plan = art_plan(L)
    les = vm.rebuild_map(art_path(L))
    if les is None:
        return
    spawn_geometry(L, clip=True)
    spawn_hazards(L)
    spawn_clips(L)
    spawn_perimeter_clip(L)
    spawn_items(L)
    spawn_pads_and_links(L, markers=False)
    spawn_starts_and_camera(L)
    spawn_kit(plan)
    spawn_lighting(L)
    vm.nav_bounds(L.NAV_CENTER, L.NAV_SIZE)
    vm.set_kill_z(L.KILL_Z)
    set_game_mode()
    vm.check_pads()
    vm.assert_counts(art_counts(L, plan), exact=True)
    actors = vm.EAS.get_all_level_actors()
    ignored = [a.get_actor_label() for a in actors if "NavLintIgnore" in [str(t) for t in a.get_editor_property("tags")]]
    if ignored:
        vm.fail(f"actors carry NavLintIgnore (forbidden on {L.MAP_ID}, E0.2 b): {ignored}")
    vm.log(f"kit instances={len(plan)} by mesh {dict(Counter(i.mesh for i in plan))}")
    vm.save_map(les)


class Bound:
    """The generator functions with the layout table already applied: `Bound(L).validate_layout()`, `.L` is the table."""

    def __init__(self, L):
        self.L = L

    def __getattr__(self, name):
        f = globals().get(name)
        if inspect.isfunction(f) and list(inspect.signature(f).parameters)[:1] == ["L"]:
            return functools.partial(f, self.L)
        if f is None:
            raise AttributeError(name)
        return f


def bind(L):
    return Bound(L)
