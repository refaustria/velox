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
        print(f"{prefix} selftest OK")
    if validate_only:
        return 0
    if mapgen.vm is None:
        raise RuntimeError("the editor is required to build a map (unreal not importable); use --validate-only / --golden outside it")
    vm = mapgen.vm
    vm.configure(log_prefix=prefix, fill_nav_kinds=L.FILL_NAV_KINDS)
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
