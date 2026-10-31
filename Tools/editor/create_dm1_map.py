"""Velox DM1 "Praesidium" generator (ADR 0006 section 2, task M5-05): writes /Game/Maps/DM1/L_DM1_Blockout from the layout table
Tools/editor/maps/dm1_layout.py.

Run headless (needs /Game/Items/DA_Item_* from create_item_content.py and the M_Greybox_* materials from
create_movement_test_map.py; run both first on a fresh checkout):
  UnrealEditor-Cmd /abs/path/Velox.uproject -run=pythonscript -script="/abs/path/Tools/editor/create_dm1_map.py --mode blockout" \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
(`--mode blockout` is the default; the mode can also come from the environment variable VELOX_DM1_MODE. `--mode art` belongs to
task 15 and raises.)

The layout checks (validate_layout) need no editor: `python3 -I Tools/editor/create_dm1_map.py --validate-only`.

Idempotent: the map is cleared and rebuilt on every run. Every actor is tagged Layout:<record id>; no actor carries NavLintIgnore
(E0.2 b). Collision is clip-first: the greybox boxes are the clip; the ceilings and the perimeter are invisible BlockAll volumes.
"""
import math
import os
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from maps import dm1_layout as L  # noqa: E402

try:
    import unreal  # noqa: E402
    import veloxmap as vm  # noqa: E402
except ImportError:      # --validate-only outside the editor
    unreal = None
    vm = None

MAP_PATH = "/Game/Maps/DM1/L_DM1_Blockout"
LOG_PREFIX = "[VeloxDM1]"
MATERIAL_KEYS = ("Floor", "Wall", "Platform", "Stairs", "RampWalkable", "Pad", "Target")
STAIR_FOOT_EXTRA = 100.0
RAMP_THICKNESS = 20.0
RAMP_FILL_STEP = 200.0
PLAYERSTART_Z = 100.0
GRAVITY = 2000.0
PAD_TRIGGER_Z = 70.0
TRAJECTORY_DT = 1.0 / 240.0


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


def expand_stairs(s):
    out = []
    # The first 20 cm step is left out (the foot rises 40 cm at once, still below the 45 cm step height): Recast raises the nav
    # over a lone 20 cm step beside a wall and the lint then reports the foot strip as walk-in floor.
    for i in range(1, s.steps):
        xa = s.x_from + s.x_dir * i * L.TREAD
        xb = xa + s.x_dir * L.TREAD
        if i == 1:
            xa -= s.x_dir * STAIR_FOOT_EXTRA      # lengthened foot (see above)
        out.append((f"{s.id}_{i:02d}", "step", min(xa, xb), max(xa, xb), s.y0, s.y1, 0.0, (i + 1) * L.RISER))
    return out


def ramp_geometry(r):
    """(run, length along the slope) of the ramp."""
    th = math.radians(L.RAMP_DEG)
    return r.rise / math.tan(th), r.rise / math.sin(th)


def expand_ramp_fills(r):
    """Stepped pawn-blocking fill boxes under the thin slab (Ramp_Fill_*, as on L_ItemTest): each box reaches the slab underside
    at its low-X end, so the residual clearance under the slab is < RAMP_FILL_STEP * tan(15 deg) = 54 cm (< agent height)."""
    th = math.radians(L.RAMP_DEG)
    run, _ = ramp_geometry(r)
    out = []
    xa = r.x0
    n = 0
    while xa < r.x0 + run - 1e-6:
        xb = min(xa + RAMP_FILL_STEP, r.x0 + run)
        top = (xa - r.x0) * math.tan(th) - RAMP_THICKNESS / math.cos(th)
        if top > 1.0:
            n += 1
            out.append((f"{r.id}_Fill_{n}", "platform", xa, xb, r.y0, r.y1, 0.0, top))
        xa = xb
    return out


def all_solids():
    """Every axis-aligned solid of the map as (id, kind, x0, x1, y0, y1, z0, z1): boxes, wall strips, stairs, ramp fills."""
    out = [(b.id, b.kind, b.x0, b.x1, b.y0, b.y1, b.z0, b.z1) for b in L.BOXES]
    for w in L.WALLS:
        out += expand_wall(w)
    for s in L.STAIRS:
        out += expand_stairs(s)
    for r in L.RAMPS:
        out += expand_ramp_fills(r)
    return out


def material_of(kind, record_id):
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


def pad_launch(pad):
    """AimAtTarget launch velocity (VeloxMoveSim::JumpPadLaunchVelocity, Height > 0 branch) from the trigger centre."""
    ox, oy, oz = pad.x, pad.y, L.FLOOR_Z + PAD_TRIGGER_Z
    dx, dy, h = pad.target[0] - ox, pad.target[1] - oy, pad.target[2] - oz
    dist = math.hypot(dx, dy)
    t = math.sqrt(2.0 * h / GRAVITY)
    return (dx / dist * dist / t, dy / dist * dist / t, GRAVITY * t), t


def pad_arc(pad, solids):
    """Walk the feet arc (origin = pad floor point, VeloxJumpPad::TryComputeLandingPoint) from the apex on; returns
    (landing, error text or None). A hit before the apex is an error (the arc must be free up to the apex)."""
    v, t_apex = pad_launch(pad)
    clips = [(c.x0, c.x1, c.y0, c.y1, c.z0, c.z1) for c in L.CLIPS]
    pre = [s[2:] for s in solids if s[1] != "floor"] + clips        # the pad stands on the ground slab
    post = [s[2:] for s in solids] + clips
    o = (pad.x, pad.y, L.FLOOR_Z)

    def at(t):
        return (o[0] + v[0] * t, o[1] + v[1] * t, o[2] + v[2] * t - 0.5 * GRAVITY * t * t)

    prev = at(0.0)
    steps = int(5.0 / TRAJECTORY_DT)
    for i in range(1, steps):
        t = i * TRAJECTORY_DT
        cur = at(t)
        for b in (pre if t < t_apex else post):
            if seg_hits_box(prev, cur, b):
                # first contact: the landing is where the segment enters the top of the box
                if t < t_apex - 1e-6:
                    return None, f"{pad.id}: the arc hits solid {b} before its apex (t={t:.2f} s < {t_apex:.2f} s) at {cur}"
                lx, ly = cur[0], cur[1]
                return (lx, ly, cur[2]), None
        prev = cur
    return None, f"{pad.id}: the arc never lands"


# ---------------------------------------------------------------- validation (no editor needed)
def validate_layout():
    """Asserts the geometry rules of ADR 0006 section 3 on the records. Returns (list of problems, dict of figures)."""
    bad = []
    fig = {}
    solids = all_solids()
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
            if w < 120.0:
                bad.append(f"{b.id}: walkable width {w:.0f} < 120")
    for s in L.STAIRS:
        if abs(s.y1 - s.y0) < 120.0:
            bad.append(f"{s.id}: stairs width < 120")
    for w in L.WALLS:
        for o in w.openings:
            width = o.o1 - o.o0
            if not 240.0 <= width <= 400.0:
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
            if z.x1 - z.x0 < 300.0 or z.y1 - z.y0 < 300.0:
                bad.append(f"{p.id}: landing zone {z.id} is {z.x1 - z.x0:.0f} x {z.y1 - z.y0:.0f} < 300 x 300")
        landing, err = pad_arc(p, solids)
        if err:
            bad.append(err)
        else:
            d = math.hypot(landing[0] - p.land[0], landing[1] - p.land[1])
            fig[f"{p.id}_landing"] = (round(landing[0]), round(landing[1]), round(landing[2], 1))
            if d > 60.0 or abs(landing[2] - p.land[2]) > 30.0:
                bad.append(f"{p.id}: simulated landing {tuple(round(c) for c in landing)} is {d:.0f} cm from the authored {p.land}")
    # rocket-jump clearance
    for k in L.LINKS:
        if k.type != "ROCKET_JUMP":
            continue
        ceil = [c.z0 for c in L.CLIPS if c.x0 <= k.start[0] <= c.x1 and c.y0 <= k.start[1] <= c.y1 and c.z0 > k.start[2] and not c.id.startswith("Clip_")]
        clear = min(ceil) - k.start[2] if ceil else None
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
        for tid in (L.MEGA_ID, L.RA_ID):
            tgt = by_id[tid]
            p = (s.x, s.y, L.FLOOR_Z + L.EYE_Z)
            q = (tgt.x, tgt.y, tgt.z + L.EYE_Z)
            if not any(seg_hits_box(p, q, sd[2:]) for sd in solids if sd[1] != "floor"):
                bad.append(f"spawn {s.id} has line of sight to {tid}")
    # nav links and pads cross-check
    pad_ids = {p.id for p in L.PADS}
    for k in L.LINKS:
        if k.type == "JUMP_PAD":
            p = next((p for p in L.PADS if p.id == k.pad), None)
            if p is None:
                bad.append(f"{k.id}: unknown pad {k.pad}")
            elif math.hypot(k.end[0] - p.land[0], k.end[1] - p.land[1]) > 60.0 or abs(k.end[2] - p.land[2]) > 30.0:
                bad.append(f"{k.id}: link end {k.end} is more than 60 cm from the pad landing {p.land}")
    jp = sum(1 for k in L.LINKS if k.type == "JUMP_PAD")
    rj = sum(1 for k in L.LINKS if k.type == "ROCKET_JUMP")
    if (jp, rj) != (3, 2):
        bad.append(f"links: JUMP_PAD={jp} ROCKET_JUMP={rj}, expected 3 and 2")
    if len(pad_ids) != 3:
        bad.append("expected 3 pads")
    # ADR item counts
    kinds = Counter(i.definition.rsplit("DA_Item_", 1)[1] for i in L.ITEMS)
    want = {"Health_Mega": 1, "Armor_Red": 1, "Armor_Yellow": 2, "Armor_Shard": 6, "Health_Large": 1, "Health_Medium": 5,
            "Health_Small": 8, "Weapon_RocketLauncher": 1, "Weapon_Railgun": 1, "Weapon_LightningGun": 1, "Weapon_Shotgun": 1,
            "Weapon_PlasmaGun": 1, "Ammo_Rockets": 1, "Ammo_Slugs": 1, "Ammo_Lightning": 1, "Ammo_Shells": 1, "Ammo_Cells": 1,
            "Powerup_Quad": 1}
    if dict(kinds) != want:
        bad.append(f"item mix differs from ADR 0006 section 3: {dict(kinds)}")
    quad = [i for i in L.ITEMS if i.definition.endswith("Powerup_Quad")]
    if not quad or quad[0].disabled_in_modes != (L.DUEL,):
        bad.append("the Quad must be disabled in Duel (FFA only)")
    # items stand on something: the floor point is within 1 cm of a solid top whose footprint contains it (or the ramp)
    tops = [s for s in solids]
    for it in L.ITEMS:
        on_ramp = any(abs(it.z - L.ramp_z(r, it.x)) < 1.0 and r.y0 <= it.y <= r.y1 and r.x0 <= it.x <= r.x0 + ramp_geometry(r)[0] for r in L.RAMPS)
        on_top = any(s[2] <= it.x <= s[3] and s[4] <= it.y <= s[5] and abs(s[7] - it.z) < 1.0 and s[1] != "lintel" for s in tops)
        if not (on_ramp or on_top):
            bad.append(f"item {it.id} at ({it.x}, {it.y}, {it.z}) stands on no solid top")
        inside = [s[0] for s in tops if s[2] < it.x < s[3] and s[4] < it.y < s[5] and s[6] < it.z + 20.0 < s[7]]
        if inside:
            bad.append(f"item {it.id} is inside solid {inside}")
    return bad, fig


# ---------------------------------------------------------------- editor part
def spawn_geometry():
    for (sid, kind, x0, x1, y0, y1, z0, z1) in all_solids():
        rec = sid
        # layout tag = the record id (before the first piece suffix)
        for suffix in ("_seg", "_sill", "_lintel"):
            if suffix in sid:
                rec = sid.split(suffix)[0]
        if sid.endswith(tuple(f"_{n:02d}" for n in range(32))) and any(sid.startswith(s.id) for s in L.STAIRS):
            rec = sid.rsplit("_", 1)[0]
        if "_Fill_" in sid:
            rec = sid.split("_Fill_")[0]
        vm.box(sid, ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2), (x1 - x0, y1 - y0, z1 - z0), material_of(kind, sid), kind,
               folder=("Rooms" if kind in ("floor", "wall", "lintel") else "Levels"), layout=rec)
    for r in L.RAMPS:
        run, length = ramp_geometry(r)
        vm.ramp(r.id, (r.x0, (r.y0 + r.y1) / 2, L.FLOOR_Z), length, r.y1 - r.y0, RAMP_THICKNESS, L.RAMP_DEG, "RampWalkable",
                kind="ramp", folder="Levels")


def spawn_clips():
    for c in L.CLIPS:
        a = vm.clip_box(c.id, ((c.x0 + c.x1) / 2, (c.y0 + c.y1) / 2, (c.z0 + c.z1) / 2), (c.x1 - c.x0, c.y1 - c.y0, c.z1 - c.z0))
        vm.tag_actor(a, c.id)


def spawn_items():
    by_label = {}
    for it in L.ITEMS:
        a, da = vm.pickup(it.definition, (it.x, it.y, it.z), name="Pickup_" + it.id, folder="Items", layout=it.id)
        by_label[it.id] = a
        if da is not None and it.disabled_in_modes:
            exp = da.get_editor_property("disabled_in_modes")
            text = str(exp.export_text()) if hasattr(exp, "export_text") else str(exp)
            for tag in it.disabled_in_modes:
                if tag not in text:
                    vm.fail(f"{it.id}: definition {it.definition} does not list {tag} in DisabledInModes (run create_item_content.py)")
        if da is not None and not it.disabled_in_modes:
            exp = da.get_editor_property("disabled_in_modes")
            text = str(exp.export_text()) if hasattr(exp, "export_text") else str(exp)
            if "TagName" in text and "Velox.Mode" in text:
                vm.fail(f"{it.id}: definition lists modes in DisabledInModes but the layout does not")


def spawn_pads_and_links():
    pads = {}
    for p in L.PADS:
        pads[p.id] = vm.jump_pad(p.id, (p.x, p.y), p.target, pad_label=p.id, target_label=p.id + "_Target", folder="Pads", layout=p.id)
        vm.cylinder_marker(p.id + "_LandingMarker", (p.land[0], p.land[1], p.land[2] + 2.0), (120, 120, 4), "Target", folder="Pads")
    for k in L.LINKS:
        vm.nav_link(k.id, getattr(unreal.VeloxNavLinkType, k.type), k.start, k.end, pad=pads.get(k.pad), folder="NavLinks", layout=k.id)


def spawn_starts_and_camera():
    for s in L.SPAWNS:
        vm.player_start(s.id, (s.x, s.y, L.FLOOR_Z + PLAYERSTART_Z), s.yaw, folder="Starts", layout=s.id)
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


def expected_counts():
    exp = dict(L.EXPECTED)
    exp["clip"] = len(L.CLIPS)
    exp["ramp"] = len(L.RAMPS)
    for (_, kind, *_rest) in all_solids():
        exp[kind] = exp.get(kind, 0) + 1
    exp["target"] = len(L.PADS)       # cylinder landing markers
    return exp


def build_map():
    les = vm.rebuild_map(MAP_PATH)
    if les is None:
        return
    spawn_geometry()
    spawn_clips()
    spawn_items()
    spawn_pads_and_links()
    spawn_starts_and_camera()
    vm.static_sun_and_sky()
    vm.nav_bounds(L.NAV_CENTER, L.NAV_SIZE)
    vm.set_kill_z(L.KILL_Z)
    set_game_mode()
    vm.check_pads()
    expected = expected_counts()
    # the clip_box helper counts under "clip" itself; the label counts under "text"; cylinder markers under "marker"
    vm.assert_counts(expected, exact=True)
    actors = vm.EAS.get_all_level_actors()
    ignored = [a.get_actor_label() for a in actors if "NavLintIgnore" in [str(t) for t in a.get_editor_property("tags")]]
    if ignored:
        vm.fail(f"actors carry NavLintIgnore (forbidden on DM1, E0.2 b): {ignored}")
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


def parse_mode():
    mode = os.environ.get("VELOX_DM1_MODE", "blockout")
    argv = sys.argv[1:]
    for i, a in enumerate(argv):
        if a == "--mode" and i + 1 < len(argv):
            mode = argv[i + 1]
        elif a.startswith("--mode="):
            mode = a.split("=", 1)[1]
    return mode


def main():
    validate_only = "--validate-only" in sys.argv[1:]
    mode = parse_mode()
    if mode == "art":
        raise RuntimeError("create_dm1_map.py --mode art is task 15; only --mode blockout exists in task 05")
    if mode != "blockout":
        raise RuntimeError(f"unknown --mode {mode!r} (blockout)")
    problems, figures = validate_layout()
    for k, v in sorted(figures.items()):
        print(f"{LOG_PREFIX} figure {k} = {v}")
    if problems:
        for p in problems:
            print(f"{LOG_PREFIX} LAYOUT PROBLEM: {p}")
        raise RuntimeError("layout validation failed: " + "; ".join(problems))
    print(f"{LOG_PREFIX} layout valid: {len(all_solids())} solids, {len(L.ITEMS)} items, {len(L.SPAWNS)} spawns, {len(L.LINKS)} links")
    if validate_only:
        return
    vm.configure(log_prefix=LOG_PREFIX, fill_nav_kinds=L.FILL_NAV_KINDS)
    vm.greybox_materials(keys=MATERIAL_KEYS)
    if not vm.ERRORS:
        build_map()
    vm.raise_if_errors("DM1 blockout")
    vm.log("done")


if __name__ == "__main__" or unreal is not None:
    main()
