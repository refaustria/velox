"""Velox map generator CLI (ADR 0008 section 1.2, task M7 P2): one entry point for every map.

  python3 -I Tools/editor/create_map.py --map dm1 --validate-only            (pure Python: layout checks)
  python3 -I Tools/editor/create_map.py --map dm1 --golden check|write       (pure Python: compare / rewrite maps/golden/<slug>.json)
  python3 -I Tools/editor/create_map.py --map dm1 --selftest                 (pure Python: validate + golden determinism)
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
