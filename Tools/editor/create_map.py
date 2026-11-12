"""Velox map generator CLI (ADR 0008 section 1.2, task M7 P2): one entry point for every map.

  python3 -I Tools/editor/create_map.py --map dm1 --validate-only            (pure Python: layout checks)
  python3 -I Tools/editor/create_map.py --map dm1 --golden check|write       (pure Python: compare / rewrite maps/golden/<slug>.json)
  python3 -I Tools/editor/create_map.py --map dm1 --selftest                 (pure Python: validate + golden determinism + fixtures)
  python3 -I Tools/editor/create_map.py --map <slug> --validate-only --free-edges (adds the per-box free-edge report)
  UnrealEditor-Cmd /abs/Velox.uproject -run=pythonscript -script="/abs/Tools/editor/create_map.py --map dm1 --mode blockout" \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

`--mode blockout` (default) or `art`; the mode can also come from VELOX_DM1_MODE (kept for the create_dm1_map.py shim).
The layout table is `maps/<slug>_layout.py`; the generator is `mapgen.py` (functions take the table as `L`).
"""
import importlib
import os
import sys
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mapgen  # noqa: E402

GOLDEN_DIR = os.path.join(HERE, "maps", "golden")


def _opt(argv, name, default=None):
    for i, a in enumerate(argv):
        if a == name and i + 1 < len(argv):
            return argv[i + 1]
        if a.startswith(name + "="):
            return a.split("=", 1)[1]
    return default


def load_layout(slug):
    return importlib.import_module(f"maps.{slug}_layout")


def parse_mode(argv):
    return _opt(argv, "--mode", os.environ.get("VELOX_DM1_MODE", "blockout"))


def golden_path(slug):
    return os.path.join(GOLDEN_DIR, slug + ".json")


def golden_text(slug):
    sys.path.insert(0, os.path.join(HERE, "maps"))
    import golden_dump  # noqa: E402
    return golden_dump.dumps(mapgen.bind(load_layout(slug)))


def golden_cmd(slug, action):
    text = golden_text(slug)
    path = golden_path(slug)
    if action == "write":
        with open(path, "w", newline="\n") as f:
            f.write(text)
        print(f"[golden] {slug}: wrote {path}")
        return 0
    if action != "check":
        raise RuntimeError(f"unknown --golden {action!r} (check | write)")
    if not os.path.isfile(path):
        print(f"[golden] {slug}: no golden file {path}", file=sys.stderr)
        return 1
    with open(path, newline="") as f:
        want = f.read()
    if want != text:
        print(f"[golden] {slug}: MISMATCH against {path}", file=sys.stderr)
        return 1
    print(f"[golden] {slug}: OK (byte-identical to maps/golden/{slug}.json)")
    return 0


def _fixture_layout(ramps=(), stairs=(), **extra):
    """A minimal spec-module stand-in (types.SimpleNamespace) for the P3 selftest."""
    import types
    ns = types.SimpleNamespace(FLOOR_Z=0.0, RAMP_DEG=15.0, TREAD=50.0, RISER=20.0, RAMPS=list(ramps), STAIRS=list(stairs), CLIPS=[],
                               BOXES=[], WALLS=[], PADS=[], LIGHTS=[], ROOMS=[], ITEMS=[], SPAWNS=[], LINKS=[], AMMO_FOR={}, WEAPON_IDS=[],
                               EXPECTED={})
    ns.__dict__.update(extra)
    return ns


def selftest_p3():
    """Ramp / Stairs extensions and the yawed veloxmap arithmetic (M7 P3). Pure Python; raises AssertionError on a failure."""
    import math
    from unittest import mock
    from maps.records import Ramp, Stairs

    def near(a, b, eps=1e-6):
        return abs(a - b) < eps

    # four directions, 15 deg: surface height at the foot, top and outside; fills under the slab on the uphill side; yaw per direction
    run15 = 320.0 / math.tan(math.radians(15.0))
    for d, (ax, sg, yaw) in {"+x": ("x", 1, 0.0), "-x": ("x", -1, 180.0), "+y": ("y", 1, 90.0), "-y": ("y", -1, 270.0)}.items():
        r = Ramp("R", 1000.0, -200.0, 200.0, 320.0, dir=d)
        L = _fixture_layout([r])
        f = mapgen.ramp_frame(L, r)
        assert f.axis == ax and f.d == ((sg, 0.0) if ax == "x" else (0.0, sg)) and mapgen.ramp_yaw(r) == yaw, d
        assert near(f.run, run15) and f.z0 == 0.0 and f.foot == ((1000.0, 0.0, 0.0) if ax == "x" else (0.0, 1000.0, 0.0)), d

        def pt(s, c=0.0, sg=sg, ax=ax):
            return (1000.0 + sg * s, c) if ax == "x" else (c, 1000.0 + sg * s)
        assert near(mapgen.ramp_surface_z(L, r, *pt(0.0)), 0.0) and near(mapgen.ramp_surface_z(L, r, *pt(run15)), 320.0), d
        assert mapgen.ramp_surface_z(L, r, *pt(-1.0)) is None and mapgen.ramp_surface_z(L, r, *pt(run15 + 1.0)) is None, d
        assert mapgen.ramp_surface_z(L, r, *pt(100.0, 250.0)) is None, d      # outside the band
        fills = mapgen.expand_ramp_fills(L, r)
        ref = mapgen.expand_ramp_fills(_fixture_layout([r._replace(dir="+x")]), r._replace(dir="+x"))
        assert len(fills) == len(ref) and fills, d
        for (_i, _k, x0, x1, y0, y1, z0, z1) in fills:
            lo, hi, c0, c1 = (x0, x1, y0, y1) if ax == "x" else (y0, y1, x0, x1)
            assert (c0, c1) == (-200.0, 200.0) and z0 == 0.0, d
            s_lo, s_hi = sorted(((lo - 1000.0) * sg, (hi - 1000.0) * sg))
            assert s_lo >= -1e-9 and s_hi <= run15 + 1e-6, d
            assert z1 <= mapgen.ramp_surface_z(L, r, *pt(s_lo)) - mapgen.RAMP_THICKNESS + 1e-6, d      # the fill stays under the slab
        cover = mapgen._Cover(L)
        assert cover.covered((pt(100.0)[0], pt(100.0)[1], 10.0)) and not cover.covered((pt(100.0)[0], pt(100.0)[1], 100.0)), d

    # z0: roof ramp starting at 320 and a below-floor ramp
    for z0 in (320.0, -320.0):
        r = Ramp("R", 0.0, 0.0, 400.0, 160.0, dir="+x", z0=z0)
        L = _fixture_layout([r])
        assert near(mapgen.ramp_surface_z(L, r, 0.0, 200.0), z0)
        assert near(mapgen.ramp_surface_z(L, r, mapgen.ramp_frame(L, r).run, 200.0), z0 + 160.0)
        fills = mapgen.expand_ramp_fills(L, r)
        assert fills and all(b[6] == z0 and b[7] > z0 for b in fills), z0

    # fill: "none" leaves no boxes and the validator demands 160 clear and 170 wide; bad fill / dir names raise
    free = Ramp("F", 0.0, 0.0, 200.0, 400.0, fill="none")
    L = _fixture_layout([free])
    assert mapgen.expand_ramp_fills(L, free) == [] and not mapgen._Cover(L).covered((100.0, 100.0, 5.0))
    assert not any("free ramp" in p for p in mapgen.validate_layout(L)[0])
    for bad_r in (free._replace(rise=100.0), free._replace(y1=150.0)):
        problems, _ = mapgen.validate_layout(_fixture_layout([bad_r]))
        assert any("free ramp" in p for p in problems), bad_r
    for fn, rec in ((mapgen.expand_ramp_fills, free._replace(fill="air")), (mapgen.ramp_frame, free._replace(dir="up"))):
        try:
            fn(L, rec)
        except ValueError:
            pass
        else:
            raise AssertionError(f"{rec} was accepted")

    # 20 and 30 degrees, and the angle limit (reported per ramp when set)
    for deg in (20.0, 30.0):
        r = Ramp("R", 0.0, 0.0, 400.0, 300.0, deg=deg)
        L = _fixture_layout([r])
        f = mapgen.ramp_frame(L, r)
        assert near(f.run, 300.0 / math.tan(math.radians(deg))) and near(f.length, 300.0 / math.sin(math.radians(deg))) and f.deg == deg
        first = mapgen.expand_ramp_fills(L, r)[0]
        assert near(first[7], 200.0 * math.tan(math.radians(deg)) - mapgen.RAMP_THICKNESS / math.cos(math.radians(deg)))
        assert near(mapgen.ramp_surface_z(L, r, f.run, 100.0), 300.0)
        problems, fig = mapgen.validate_layout(L)
        assert fig["R_deg"] == deg and not any("45.5" in p for p in problems)
    problems, _ = mapgen.validate_layout(_fixture_layout([Ramp("S", 0.0, 0.0, 400.0, 300.0, deg=46.0)]))
    assert any("45.5" in p for p in problems)

    # art plan plates: yawed, inside the footprint, one column per 200 cm across the band
    for d in ("+x", "-x", "+y", "-y"):
        r = Ramp("R", 1000.0, -400.0, 400.0, 320.0, dir=d, deg=20.0, z0=100.0)
        L = _fixture_layout([r])
        f = mapgen.ramp_frame(L, r)
        th = math.radians(20.0)
        cross_seen = set()
        for i in mapgen.ramp_plan(L, None, r):
            assert i.yaw == mapgen.ramp_yaw(r) and i.pitch == 20.0 and i.z >= 100.0 - 1e-9
            ya = math.radians(i.yaw)
            ex, ey = (math.cos(ya), math.sin(ya)), (-math.sin(ya), math.cos(ya))
            horiz = 400.0 * i.sx * math.cos(th)
            for (u, v) in ((0.0, 0.0), (horiz, 0.0), (0.0, 200.0 * i.sy), (horiz, 200.0 * i.sy)):
                wx, wy = i.x + ex[0] * u + ey[0] * v, i.y + ex[1] * u + ey[1] * v
                run_c, cross_c = (wx, wy) if f.axis == "x" else (wy, wx)
                s_run = (run_c - 1000.0) * (f.d[0] if f.axis == "x" else f.d[1])
                assert -1e-6 <= s_run <= f.run + 1e-6 and -400.0 - 1e-6 <= cross_c <= 400.0 + 1e-6, (d, i)
            cross_seen.add(round(i.y if f.axis == "x" else i.x, 3))
        assert len(cross_seen) == 4, (d, cross_seen)

    # stairs: axis x keeps the DM1 foot rule; axis y swaps the roles; tread / riser / z0 override; direction -1
    sx = Stairs("SX", 100.0, 1, 0.0, 400.0, 6)
    sy = Stairs("SY", 100.0, 1, 0.0, 400.0, 6, axis="y")
    sm = Stairs("SM", 100.0, -1, 0.0, 400.0, 6, axis="y", z0=100.0, tread=40.0, riser=10.0)
    L = _fixture_layout(stairs=[sx, sy, sm])
    bx, by, bm = (mapgen.expand_stairs(L, s) for s in (sx, sy, sm))
    assert len(bx) == len(by) == len(bm) == 5
    for a, b in zip(bx, by):
        assert (a[2], a[3], a[4], a[5], a[6], a[7]) == (b[4], b[5], b[2], b[3], b[6], b[7])       # x <-> y swap
    assert bx[0][2:4] == (100.0 + 50.0 - mapgen.STAIR_FOOT_EXTRA, 200.0) and bx[0][7] == 40.0 and bx[1][2:4] == (200.0, 250.0)
    assert bm[0][2:6] == (0.0, 400.0, 20.0, 160.0) and bm[1][4:6] == (-20.0, 20.0) and bm[2][4:6] == (-60.0, -20.0)
    assert all(b[6] == 100.0 for b in bm) and [b[7] for b in bm] == [120.0, 130.0, 140.0, 150.0, 160.0]
    try:
        mapgen.stairs_frame(L, sx._replace(axis="z"))
    except ValueError:
        pass
    else:
        raise AssertionError("bad stairs axis accepted")
    problems, _ = mapgen.validate_layout(L)
    assert not problems or all("stairs" not in p for p in problems)
    assert any(i.layout == "SY" for i in mapgen.plan_art(L)), "no art plan for the y stairs"

    # veloxmap arithmetic (needs a stub for `unreal`, which is not importable outside the editor)
    with mock.patch.dict(sys.modules, {"unreal": mock.MagicMock()}):
        sys.modules.pop("veloxmap", None)
        import veloxmap as vmod
        c0 = vmod.ramp_center((10.0, 20.0, 0.0), 400.0, 20.0, 15.0)
        c90 = vmod.ramp_center((10.0, 20.0, 0.0), 400.0, 20.0, 15.0, 90.0)
        c180 = vmod.ramp_center((10.0, 20.0, 0.0), 400.0, 20.0, 15.0, 180.0)
        off = c0[0] - 10.0
        assert near(off, 200.0 * math.cos(math.radians(15.0)) + 10.0 * math.sin(math.radians(15.0))) and c0[1] == 20.0
        assert near(c90[0], 10.0) and near(c90[1], 20.0 + off) and near(c180[0], 10.0 - off) and near(c180[1], 20.0) and near(c90[2], c0[2])
        sb = vmod.stairs_boxes(100.0, 5.0, 40.0, 200.0, 20.0, 3, axis="y", direction=-1, z0=50.0)
        assert sb[0] == ((5.0, 80.0, 60.0), (200.0, 40.0, 20.0)) and sb[2] == ((5.0, 0.0, 80.0), (200.0, 40.0, 60.0)), sb
        assert vmod.stairs_boxes(0.0, 0.0, 50.0, 100.0, 20.0, 2)[1] == ((75.0, 0.0, 20.0), (50.0, 100.0, 40.0))
        for mode, n in (("box", 5), ("walls", 4), ("none", 0)):
            with mock.patch.object(vmod, "clip_box") as cb:
                vmod.clip_walls(1000.0, 800.0, mode=mode)
                assert cb.call_count == n, (mode, cb.call_count)
        with mock.patch.object(vmod, "clip_box") as cb:
            vmod.configure(clip_h=2000.0)
            vmod.clip_walls(1000.0, 800.0)
            assert cb.call_args_list[0][0][1][2] == 1000.0 and cb.call_args_list[-1][0][1][2] == 2050.0
        sys.modules.pop("veloxmap", None)
    assert mapgen.expected_counts(_fixture_layout(CLIP_MODE="walls"))["clip"] == 4
    print("[selftest] ramp / stairs / yaw / clip fixtures OK")


def _fx(boxes=(), **extra):
    """P4 fixture: a ground slab plus `boxes`, no spawns / items / links unless given."""
    from maps.records import Box
    ground = Box("G", "floor", "Floor", -3000.0, 3000.0, -3000.0, 3000.0, -100.0, 0.0)
    kw = dict(BOXES=[ground] + list(boxes), EYE_Z=160.0, MEGA_ID="RA", RA_ID="RA", MAP_ID="FX")
    kw.update(extra)
    return _fixture_layout(**kw)


def selftest_p4():
    """Oct, Hazard, line of sight, link ends, drop rule, pad clearance / speed, free ramps, free edges (M7 P4): one passing and one failing
    fixture per rule. Pure Python; raises AssertionError."""
    import ast
    import math
    from unittest import mock
    from maps.records import Box, Oct, Hazard, Item, Spawn, Link, Pad, Ramp

    def has(problems, text):
        return any(text in p for p in problems)

    def raises(fn, exc):
        try:
            fn()
        except exc:
            return True
        return False

    # rules: merge, unknown key, DM1 legacy gate
    assert mapgen.rules(_fx())["pad_arc_clearance"] == 0.0 and mapgen.rules(_fx(RULES={"pad_arc_clearance": 60.0}))["pad_arc_clearance"] == 60.0
    assert raises(lambda: mapgen.rules(_fx(RULES={"pad_arc_clearence": 60.0})), ValueError)
    assert mapgen.rules(_fx(MAP_ID="DM1"))["drop_rule"] is False and mapgen.rules(_fx(MAP_ID="DM1", RULES={"drop_rule": True}))["drop_rule"] is True
    assert mapgen.rules(_fx(MAP_ID="NEMUS"))["drop_rule"] is True

    # veloxmap.octagon_boxes: four boxes, yaw 0/45/90/135, and their union has the octagon's corners on its boundary
    with mock.patch.dict(sys.modules, {"unreal": mock.MagicMock()}):
        sys.modules.pop("veloxmap", None)
        import veloxmap as vmod
        ob = vmod.octagon_boxes(10.0, 20.0, 300.0, 0.0, 600.0)
        sys.modules.pop("veloxmap", None)
    w = 300.0 * math.tan(math.radians(22.5))
    assert [b[2] for b in ob] == [0.0, 45.0, 90.0, 135.0] and all(b[0] == (10.0, 20.0, 300.0) and b[1] == (300.0, w, 600.0) for b in ob)
    for k, a in enumerate((22.5, 67.5, 112.5)):       # an octagon vertex lies on the long edge of the box whose yaw is 22.5 deg away from it
        r = 300.0 * mapgen.OCT_CLEAR_FACTOR
        yaw = math.radians(ob[k if a < 45 else (k if a < 90 else 3)][2])
        v = (r * math.cos(math.radians(a)), r * math.sin(math.radians(a)))
        perp = abs(-math.sin(yaw) * v[0] + math.cos(yaw) * v[1])
        assert min(abs(-math.sin(math.radians(y)) * v[0] + math.cos(math.radians(y)) * v[1]) for y in (0.0, 45.0, 90.0, 135.0)) <= w / 2 + 1e-6
    assert abs(mapgen.oct_r_clear(Oct("O", "platform", "Platform", 0, 0, 300.0, 0, 1)) - 162.3) < 0.1

    # line of sight: wall / Oct block, groups, POINTS, unknown names, the longest-sightline figure
    wall = Box("W", "wall", "Wall", 400.0, 500.0, -300.0, 300.0, 0.0, 800.0)
    mk = lambda boxes=(), **kw: _fx(boxes, SPAWNS=[Spawn("S", 0.0, 0.0, 0.0)],
                                    ITEMS=[Item("RA", "/Game/Items/DA_Item_Armor_Red", 1000.0, 0.0, 0.0, ())], **kw)
    L = mk([wall], LOS_BLOCKED=[("S", "RA")])
    assert mapgen.check_los(L)[0] == [] and not mapgen.los(L, "S", "RA")                       # passing: blocked as demanded
    assert has(mapgen.check_los(mk(LOS_BLOCKED=[("S", "RA")]))[0], "must be blocked")           # failing: nothing in the way
    assert mapgen.check_los(mk(LOS_CLEAR=[("S", "RA")]))[0] == [] and has(mapgen.check_los(mk([wall], LOS_CLEAR=[("S", "RA")]))[0], "must be clear")
    assert has(mapgen.check_los(mk())[0], "must be blocked") and mapgen.check_los(mk([wall]))[0] == []        # no lists: spawns must not see the majors (DM1)
    assert mapgen.check_los(mk([wall], LOS_BLOCKED=[("spawns", "majors")], MAJOR_IDS=("RA",)))[0] == []
    assert has(mapgen.check_los(mk(LOS_BLOCKED=[("spawns", "majors")], MAJOR_IDS=("RA",)))[0], "must be blocked")
    assert mapgen.los(mk(POINTS={"Loft": (0.0, 400.0)}), "Loft", "RA")
    assert raises(lambda: mapgen.los(mk(), "S", "Nowhere"), KeyError)
    ocyl = Oct("C", "platform", "Platform", 500.0, 0.0, 200.0, 0.0, 800.0)
    assert not mapgen.los(mk(OCTS=[ocyl]), "S", "RA") and mapgen.los(mk(OCTS=[ocyl._replace(cy=300.0)]), "S", "RA")      # Oct blocks only where it stands
    fig = mapgen.check_los(mk([wall], LOS_BLOCKED=[("S", "RA")]))[1]
    assert fig["S_sightline_m"] == 300.0 and fig["longest_sightline_m"] >= fig["S_sightline_m"] and "S_sightline_m" not in mapgen.check_los(mk())[1]
    sl = mapgen.longest_sightline(mk([wall]), (0.0, 0.0, 0.0))
    assert sl == 30000.0 
    assert 1380.0 < mapgen.longest_sightline(_fx([Box("N", "wall", "Wall", -1100.0, 1100.0, 1000.0, 1100.0, 0.0, 800.0), Box("S", "wall", "Wall", -1100.0, 1100.0, -1100.0, -1000.0, 0.0, 800.0), Box("E", "wall", "Wall", 1000.0, 1100.0, -1100.0, 1100.0, 0.0, 800.0), Box("W", "wall", "Wall", -1100.0, -1000.0, -1100.0, 1100.0, 0.0, 800.0)]), (0.0, 0.0, 0.0)) < 1420.0         # corner of a 20 m room

    # LOS tolerance (max visible fraction) and point groups in POINTS
    arch = [(float(x), 400.0, 0.0) for x in range(-450, 451, 100)]                                      # 10 samples; the wall hides all but x = 450
    gate = Box("Gt", "wall", "Wall", 100.0, 200.0, -300.0, 300.0, 0.0, 800.0)
    L = mk([gate], POINTS={"Arch": arch}, LOS_BLOCKED=[("S", "Arch")])
    assert len(mapgen.los_pairs(L, "S", "Arch")) == 10
    vis = sum(1 for (_a, _b, c) in mapgen.los_pairs(L, "S", "Arch") if c)
    assert 0 < vis < 10                                                                                    # the gate hides some samples, not all
    frac = vis / 10.0
    assert has(mapgen.check_los(L)[0], "must be blocked") == (frac > 0.0)
    assert mapgen.check_los(mk([gate], POINTS={"Arch": arch}, LOS_BLOCKED=[("S", "Arch", frac)]))[0] == []
    assert has(mapgen.check_los(mk([gate], POINTS={"Arch": arch}, LOS_BLOCKED=[("S", "Arch", frac - 0.05)]))[0], "tolerance")
    pw = Box("Pw", "wall", "Wall", -1000.0, 1000.0, 200.0, 300.0, 0.0, 800.0)                              # hides every sample
    assert mapgen.check_los(mk([pw], POINTS={"Arch": arch}, LOS_BLOCKED=[("S", "Arch", 0.0)]))[0] == []
    clear_arch = mk([pw], POINTS={"Arch": arch}, LOS_CLEAR=[("S", "Arch", 0.05)])
    assert has(mapgen.check_los(clear_arch)[0], "must be clear") and mapgen.check_los(mk(POINTS={"Arch": arch}, LOS_CLEAR=[("S", "Arch", 0.0)]))[0] == []
    assert "los_visible_pct S -> Arch" in mapgen.check_los(L)[1]

    # counts a map declares itself (DM1: legacy values); blockout / art paths from the spec
    pl = Pad("P", 0.0, 0.0, (500.0, 0.0, 400.0), (1000.0, 0.0, 0.0))
    assert not has(mapgen.validate_layout(_fx([], PADS=[pl]))[0], "pads")
    assert has(mapgen.validate_layout(_fx([], PADS=[pl], RULES={"pad_count": 2}))[0], "expected 2 pads")
    assert not has(mapgen.validate_layout(_fx([], PADS=[pl], RULES={"pad_count": 1}))[0], "pads")
    assert has(mapgen.validate_layout(_fx([], RULES={"link_counts": {"JUMP_PAD": 1, "DROP": 0}}))[0], "links by type")
    assert not has(mapgen.validate_layout(_fx([], RULES={"link_counts": {"JUMP_PAD": 0}}))[0], "links by type")
    assert has(mapgen.validate_layout(_fx([], RULES={"item_mix": {"Health_Small": 1}}))[0], "item mix")
    assert mapgen.rules(_fx(MAP_ID="DM1"))["pad_count"] == 3 and mapgen.rules(_fx(MAP_ID="DM1"))["link_counts"] == {"JUMP_PAD": 3, "ROCKET_JUMP": 2}
    assert mapgen.blockout_path(_fx(MAP_ID="DM1")) == "/Game/Maps/DM1/L_DM1_Blockout" and mapgen.art_path(_fx(MAP_ID="DM1")) == "/Game/Maps/DM1/L_DM1"
    assert mapgen.blockout_path(_fx(MAP_ID="NEMUS", MAP_PATH="/Game/Maps/Nemus/L_Nemus_Blockout")) == "/Game/Maps/Nemus/L_Nemus_Blockout"
    assert mapgen.art_path(_fx(MAP_ID="NEMUS", ART_MAP_PATH="/Game/Maps/Nemus/L_Nemus")) == "/Game/Maps/Nemus/L_Nemus"

    # link ends on ground
    plat = Box("P", "platform", "Platform", 0.0, 1000.0, 0.0, 1000.0, 0.0, 320.0)
    L = _fx([plat])
    rl, solids = mapgen.rules(L), mapgen.all_solids(L)
    assert mapgen.link_end_problem(L, solids, (500.0, 500.0, 320.0), rl) is None and mapgen.link_end_problem(L, solids, (500.0, -500.0, 0.0), rl) is None
    assert "inside the edge" in mapgen.link_end_problem(L, solids, (20.0, 500.0, 320.0), rl)                # 20 cm from a free edge
    assert mapgen.link_end_problem(L, solids, (46.0, 500.0, 320.0), rl) is None
    assert "no standing surface" in mapgen.link_end_problem(L, solids, (500.0, 500.0, 340.0), rl)             # 20 cm above the top
    assert "no standing surface" in mapgen.link_end_problem(L, solids, (-500.0, 5000.0, 0.0), rl)             # off the map
    bad_l = _fx([plat], LINKS=[Link("J", "JUMP", (20.0, 500.0, 320.0), (500.0, -500.0, 0.0), None)])
    assert has(mapgen.validate_layout(bad_l)[0], "J: link start")
    assert not has(mapgen.validate_layout(_fx([plat], LINKS=[Link("J", "JUMP", (500.0, 500.0, 320.0), (500.0, -500.0, 0.0), None)]))[0], "J: link")
    assert not has(mapgen.validate_layout(_fx([plat], MAP_ID="DM1", LINKS=bad_l.LINKS))[0], "J: link")        # DM1 legacy gate

    # drop rule E3 (d)
    def drops(end, extra_boxes=(), pads=(), **kw):
        k = Link("D", "DROP", (940.0, 500.0, 320.0), end, None)
        L = _fx([plat] + list(extra_boxes), PADS=list(pads), LINKS=[k], **kw)
        return mapgen.drop_problems(L, mapgen.all_solids(L), k, mapgen.rules(L))
    far_wall = Box("FW", "wall", "Wall", 1900.0, 2000.0, 0.0, 1000.0, 0.0, 800.0)
    assert drops((1200.0, 500.0, 0.0)) == []
    assert has(drops((1100.0, 500.0, 0.0)), "300 x 300")                                                      # the platform eats the square
    assert drops((1200.0, 500.0, 0.0), [far_wall]) == [] and has(drops((1200.0, 500.0, 0.0), [far_wall._replace(x0=1500.0, x1=1600.0)]), "wall less than")
    pad = Pad("P", 1300.0, 500.0, (1500.0, 500.0, 300.0), (1800.0, 500.0, 0.0))
    assert has(drops((1200.0, 500.0, 0.0), pads=[pad]), "from pad") and drops((1200.0, 500.0, 0.0), pads=[pad._replace(x=2400.0)]) == []
    bad_l = _fx([plat], LINKS=[Link("D", "DROP", (940.0, 500.0, 320.0), (1100.0, 500.0, 0.0), None)])
    assert has(mapgen.validate_layout(bad_l)[0], "D: DROP end")

    # pad arcs: speed limit and clearance
    pa = Pad("P", 0.0, 0.0, (500.0, 0.0, 400.0), (1000.0, 0.0, 0.0))
    land = mapgen.pad_arc(_fx(), pa, mapgen.all_solids(_fx()))[0]
    assert land is not None and abs(land[2]) < 10.0
    pa = pa._replace(land=(land[0], land[1], 0.0))
    side = Box("SW", "wall", "Wall", 0.0, 1400.0, 40.0, 140.0, 0.0, 800.0)          # runs alongside the arc at 40 cm
    for clr, boxes, ok in ((60.0, [], True), (30.0, [side], True), (60.0, [side], False)):
        L = _fx(boxes, PADS=[pa], RULES={"pad_arc_clearance": clr})
        got = mapgen.pad_arc_clearance_problem(L, pa, mapgen.all_solids(L), mapgen.pad_arc(L, pa, mapgen.all_solids(L))[0], clr)
        assert (got is None) == ok, (clr, boxes, got)
        assert has(mapgen.validate_layout(L)[0], "the arc passes within") == (not ok)
    assert not has(mapgen.validate_layout(_fx([], PADS=[pa]))[0], "launch speed")
    assert has(mapgen.validate_layout(_fx([], PADS=[pa], RULES={"pad_max_launch_speed": 100.0}))[0], "launch speed")
    assert has(mapgen.validate_layout(_fx([], PADS=[pa._replace(target=(500.0, 0.0, 10.0))]))[0], "not above the trigger")
    # an Oct in the arc is hit (circumscribed radius), a distant one is not
    for cy, ok in ((0.0, False), (400.0, True)):
        L = _fx([], PADS=[pa], OCTS=[Oct("C", "platform", "Platform", 500.0, cy, 100.0, 0.0, 2000.0)])
        assert (mapgen.pad_arc(L, pa, mapgen.all_solids(L))[1] is None) == ok
    L = _fx([], PADS=[pa], OCTS=[Oct("C", "platform", "Platform", 500.0, 52.0, 100.0, 0.0, 2000.0)])      # 52 cm: outside d/2 = 50, inside 0.541 d = 54.1
    assert mapgen.pad_arc(L, pa, mapgen.all_solids(L))[1] is not None

    # Oct checks: gap rule, items inside
    o1 = Oct("O1", "platform", "Platform", 0.0, 0.0, 200.0, 0.0, 600.0)
    for dx, ok in ((250.0, False), (338.0, True), (400.0, True), (500.0, True)):       # circumradius 108.2 each: touching up to 216.5, trap (33..120 cm gap) up to 336.5
        L = _fx([], OCTS=[o1, o1._replace(id="O2", cx=dx)])
        assert (not has(mapgen.check_octs(L, mapgen.all_solids(L), mapgen.rules(L)), "gap")) == ok, dx
    L = _fx([], OCTS=[o1, o1._replace(id="O2", cx=216.0)])
    assert mapgen.check_octs(L, mapgen.all_solids(L), mapgen.rules(L)) == []
    L = _fx([wall], OCTS=[o1._replace(cx=300.0)])                                      # wall face at 400, circumradius reaches 408: touching, no trap
    assert mapgen.check_octs(L, mapgen.all_solids(L), mapgen.rules(L)) == []
    L = _fx([wall], OCTS=[o1._replace(cx=230.0)])                                      # gap 400 - (230 + 108) = 62: a trap
    assert has(mapgen.check_octs(L, mapgen.all_solids(L), mapgen.rules(L)), "gap to solid W")
    inside = Item("I", "/Game/Items/DA_Item_Health_Small", 10.0, 0.0, 0.0, ())
    assert has(mapgen.validate_layout(_fx([], OCTS=[o1], ITEMS=[inside]))[0], "inside Oct") and not has(mapgen.validate_layout(_fx([], OCTS=[o1], ITEMS=[inside._replace(x=500.0)]))[0], "Oct")
    top_item = inside._replace(z=600.0)
    assert not has(mapgen.validate_layout(_fx([], OCTS=[o1], ITEMS=[top_item]))[0], "stands on no solid")
    cnt = mapgen.expected_counts(_fx([], OCTS=[o1]))
    assert cnt["platform"] == 4 and "hazardvolume" not in cnt

    # hazards
    bed = Box("Bed", "hazard", "Pit", 0.0, 300.0, -500.0, 500.0, -160.0, -60.0)
    lip = Box("Lip", "platform", "Platform", 300.0, 1300.0, -500.0, 500.0, -100.0, 0.0)
    lava = Hazard("Lava", "lava", 0.0, 300.0, -500.0, 500.0, -60.0, -40.0)
    ground_split = Box("G2", "platform", "Platform", -1300.0, 0.0, -500.0, 500.0, -100.0, 0.0)

    def hz(hazard=lava, boxes=(bed, lip, ground_split), **kw):
        L = _fx(list(boxes), HAZARDS=[hazard], **kw)
        return mapgen.check_hazards(L, mapgen.all_solids(L), mapgen.rules(L))
    assert hz() == []                                                                    # surface 40 cm below the adjoining tops
    assert has(hz(lava._replace(z1=-5.0)), "needs 20..60") and has(hz(lava._replace(z1=-90.0, z0=-95.0)), "needs 20..60")
    assert has(hz(boxes=(lip, ground_split)), "no Box of kind 'hazard'")
    assert has(hz(boxes=(bed, ground_split._replace(x0=-1300.0, x1=-400.0), lip._replace(x0=900.0))), "no adjoining floor")
    assert has(hz(lava._replace(type="acid")), "lava | void") and has(hz(lava._replace(z1=-70.0)), "empty hazard box")
    assert hz(Hazard("Void", "void", -9000.0, 9000.0, -9000.0, 9000.0, -2000.0, -1400.0), boxes=()) == []                 # a void slab needs no bed
    it_in = Item("I", "/Game/Items/DA_Item_Health_Small", 150.0, 0.0, -50.0, ())
    assert has(hz(SPAWNS=[Spawn("S", 150.0, 0.0, 0.0)], FLOOR_Z=-150.0), "spawn S")             # the spawn point (floor + 100) is in the lava box
    assert has(hz(PADS=[Pad("P", 150.0, 0.0, (400.0, 0.0, 300.0), (600.0, 0.0, -50.0))], FLOOR_Z=-50.0), "pad P")
    assert has(hz(ITEMS=[it_in]), "item I") and not has(hz(ITEMS=[it_in._replace(x=500.0, z=0.0)]), "inside the hazard")
    assert has(hz(LINKS=[Link("K", "JUMP", (150.0, 0.0, -50.0), (900.0, 0.0, 0.0), None)]), "link K start")
    cnt = mapgen.expected_counts(_fx([bed, lip, ground_split], HAZARDS=[lava]))
    assert cnt["hazardvolume"] == 1 and cnt["hazard"] == 1

    # free ramps: the passage under the slab
    fr = Ramp("F", 0.0, 0.0, 200.0, 400.0, fill="none")
    L = _fx([], RAMPS=[fr])
    w_free, how = mapgen.ramp_passage(L, fr, mapgen.all_solids(L))
    assert w_free >= 170.0 and not has(mapgen.validate_layout(L)[0], "free ramp") and mapgen.validate_layout(L)[1]["F_under_w"] >= 170
    crate = Box("X", "platform", "Platform", 0.0, 1500.0, -50.0, 250.0, 0.0, 300.0)                  # under the whole slab: 79 cm left at the high end
    L = _fx([crate], RAMPS=[fr])
    assert mapgen.ramp_passage(L, fr, mapgen.all_solids(L))[0] < 170.0 and has(mapgen.validate_layout(L)[0], "no passage under the slab")
    pillar = Box("X", "platform", "Platform", 0.0, 1500.0, -50.0, 100.0, 0.0, 600.0)                  # blocks half the band, but pierces the slab; 100 cm remain
    L = _fx([pillar], RAMPS=[fr])
    assert has(mapgen.validate_layout(L)[0], "no passage under the slab")

    # free edges (report only)
    def edge_report(**kw):
        boxes = kw.pop("boxes", [plat])
        ex = ["G"] + list(kw.pop("FREE_EDGE_EXEMPT", []))              # the ground slab's own rim is exempt in these fixtures
        return mapgen.free_edges(_fx(boxes, FREE_EDGE_EXEMPT=ex, **kw))
    fe = edge_report()
    assert fe == {"P": 4000.0}                                                                         # all four 1000 cm edges drop 320
    assert edge_report(FREE_EDGE_EXEMPT=["P"]) == {}
    assert edge_report(boxes=[plat, plat._replace(id="Q", x0=1000.0, x1=2000.0)])["P"] == 3000.0                   # a neighbour at the same height is no edge
    walls = [Box(f"Wl{i}", "wall", "Wall", *r, 0.0, 800.0) for i, r in enumerate(((-100.0, 0.0, 0.0, 1000.0), (1000.0, 1100.0, 0.0, 1000.0),
                                                                                  (0.0, 1000.0, -100.0, 0.0), (0.0, 1000.0, 1000.0, 1100.0)))]
    assert edge_report(boxes=[plat] + walls) == {}
    from maps.records import Clip
    assert edge_report(CLIPS=[Clip("C", -20.0, 0.0, 0.0, 1000.0, -100.0, 3000.0)])["P"] == 3000.0                 # an invisible wall protects its edge
    st = Ramp("R", -1194.2, 0.0, 1000.0, 320.0, dir="+x")                                             # a ramp up one side leaves three edges (+ the ramp's 600 cm are no drop)
    got = edge_report(RAMPS=[st])["P"]
    assert 2000.0 < got < 4000.0
    lk = [Link("K", "JUMP", (500.0, 960.0, 320.0), (500.0, 1500.0, 0.0), None)]
    assert edge_report(LINKS=lk)["P"] < 4000.0
    assert "free_edge_m" not in mapgen.validate_layout(_fx([plat]))[1]
    assert mapgen.validate_layout(_fx([plat], RULES={"free_edge_report": True}, FREE_EDGE_EXEMPT=["G"]))[1]["free_edge_m"] == 40.0

    # veloxmap.drop_links is never called by the map pipeline (links come only from Link records)
    def calls_drop_links(src):
        return any(isinstance(n, ast.Call) and getattr(n.func, "attr", getattr(n.func, "id", None)) == "drop_links" for n in ast.walk(ast.parse(src)))
    for fn in (os.path.join(HERE, "create_map.py"), os.path.join(HERE, "mapgen.py")):
        with open(fn) as f:
            assert not calls_drop_links(f.read()), fn
    assert calls_drop_links("vm.drop_links('T', 0, 0, 400, 320, 'NSEW')")

    # spawning: Octs and Hazards go through veloxmap.octagon / hazard
    saved = (mapgen.vm, mapgen.unreal)
    try:
        mapgen.vm = mock.MagicMock()
        L = _fx([bed, lip, ground_split], OCTS=[o1], HAZARDS=[lava])
        mapgen.spawn_geometry(L)
        mapgen.spawn_geometry(L, clip=True)
        mapgen.spawn_hazards(L)
        assert [c[0][0] for c in mapgen.vm.octagon.call_args_list] == ["O1", "Clip_O1"] and mapgen.vm.hazard.call_count == 1
        assert mapgen.vm.hazard.call_args[0][:2] == ("Lava", "lava") and mapgen.vm.hazard.call_args[0][3] == (300.0, 1000.0, 20.0)
        mapgen.vm = mock.Mock(spec=["fail"])                                                           # no hazard() yet (before task H2)
        mapgen.spawn_hazards(L)
        assert mapgen.vm.fail.called
    finally:
        mapgen.vm, mapgen.unreal = saved
    print("[selftest] Oct / Hazard / LOS / link-end / drop / pad clearance / free-ramp / free-edge fixtures OK")


def selftest_spawn_smoke(slug):
    """Runs build_map / build_art_map against a mocked editor: catches undefined names and missing L arguments in the spawn code."""
    from unittest import mock
    L = load_layout(slug)
    saved = (mapgen.vm, mapgen.unreal)
    mapgen.vm, mapgen.unreal = mock.MagicMock(unsafe=True), mock.MagicMock()
    mapgen.vm.pickup.return_value = (mock.MagicMock(), None)
    mapgen.vm.EAS.get_all_level_actors.return_value = []
    try:
        for fn in (mapgen.build_map, mapgen.build_art_map):
            mapgen.vm.reset_mock(return_value=False)
            fn(L)
            assert mapgen.vm.save_map.called, fn.__name__
            n_ramp = sum(1 for c in mapgen.vm.method_calls if c[0] == "ramp")
            assert n_ramp == len(L.RAMPS), (fn.__name__, n_ramp, len(L.RAMPS))
    finally:
        mapgen.vm, mapgen.unreal = saved
    print(f"[selftest] {slug}: build_map / build_art_map spawn code runs against a mocked editor")


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    slug = _opt(argv, "--map")
    if not slug:
        raise RuntimeError("--map <slug> is required")
    golden = _opt(argv, "--golden")
    if golden:
        return golden_cmd(slug, golden)
    L = load_layout(slug)
    prefix = mapgen.log_prefix(L)
    mode = parse_mode(argv)
    if mode not in ("blockout", "art"):
        raise RuntimeError(f"unknown --mode {mode!r} (blockout | art)")
    validate_only = "--validate-only" in argv or "--selftest" in argv
    problems, figures = mapgen.validate_layout(L)
    for k, v in sorted(figures.items()):
        print(f"{prefix} figure {k} = {v}")
    if problems:
        for p in problems:
            print(f"{prefix} LAYOUT PROBLEM: {p}")
        raise RuntimeError("layout validation failed: " + "; ".join(problems))
    if "--free-edges" in argv:
        for bid, length in sorted(mapgen.free_edges(L).items()):
            print(f"{prefix} free edge {bid}: {length / 100.0:.1f} m")
        print(f"{prefix} free edges total {sum(mapgen.free_edges(L).values()) / 100.0:.1f} m (report only)")
    print(f"{prefix} layout valid: {len(mapgen.all_solids(L))} solids, {len(L.ITEMS)} items, {len(L.SPAWNS)} spawns, {len(L.LINKS)} links")
    if mode == "art":
        plan = mapgen.art_plan(L)
        bad = mapgen.plan_protrusion(L, plan)
        print(f"{prefix} art plan: {len(plan)} kit instances {dict(Counter(i.mesh for i in plan))}; {len(bad)} protrude > 5 cm")
        for i, d in bad[:20]:
            print(f"{prefix} PROTRUSION {d:.1f} cm: {i}")
        if bad:
            raise RuntimeError("the kit plan protrudes into walkable space")
    if "--selftest" in argv:
        if golden_text(slug) != golden_text(slug):
            raise RuntimeError("golden dump is not deterministic")
        selftest_p3()
        selftest_p4()
        if mapgen.vm is None:
            selftest_spawn_smoke(slug)
        print(f"{prefix} selftest OK")
    if validate_only:
        return 0
    if mapgen.vm is None:
        raise RuntimeError("the editor is required to build a map (unreal not importable); use --validate-only / --golden outside it")
    vm = mapgen.vm
    vm.configure(log_prefix=prefix, fill_nav_kinds=L.FILL_NAV_KINDS, clip_h=getattr(L, "CLIP_H", None))
    vm.greybox_materials(keys=mapgen.MATERIAL_KEYS)
    if not vm.ERRORS:
        if mode == "art":
            mapgen.build_art_map(L)
        else:
            mapgen.build_map(L)
    vm.raise_if_errors(f"{L.MAP_ID} {mode}")
    vm.log("done")
    return 0


if __name__ == "__main__":
    sys.exit(main())
elif mapgen.unreal is not None and "__shim__" not in globals():
    main()
