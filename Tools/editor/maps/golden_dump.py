"""DM1 golden dump (ADR 0008 section 1.5, task M7 P1). Pure Python, no editor.

Freezes what the DM1 generator `Tools/editor/create_dm1_map.py` produces from `maps/dm1_layout.py`: the sorted validator `figure`
lines, `all_solids()`, the art plan tuples, `expected_counts()`, `art_counts()`, and every pad's simulated landing. Floats are
written with `repr` (json.dumps does the same), keys sorted, 1-space indent, one trailing newline: the bytes are deterministic.

    python3 -I Tools/editor/maps/golden_dump.py [--out Tools/editor/maps/golden/dm1.json]     (default: stdout)

P2 re-runs this same function (`golden(module)`) against the split generator and compares the bytes with the committed file.
The functions are read through the module `create_dm1_map` (the generator as it exists at P1); after P2 adapt only `_api()`.
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EDITOR = os.path.dirname(HERE)
sys.path.insert(0, EDITOR)


def _api():
    import create_dm1_map as g   # noqa: E402  (safe outside the editor: main() only runs in the editor or as a script)
    return g


def golden(g=None):
    g = g or _api()
    problems, figures = g.validate_layout()
    solids = g.all_solids()
    plan = g.art_plan()
    pads = {}
    for p in g.L.PADS:
        landing, err = g.pad_arc(p, solids)
        pads[p.id] = {"landing": list(landing) if landing else None, "error": err}
    return {
        "problems": list(problems),
        "figure_lines": sorted("figure %s = %r" % (k, v) for k, v in figures.items()),
        "all_solids": [list(s) for s in solids],
        "art_plan": [list(i) for i in plan],
        "expected_counts": g.expected_counts(),
        "art_counts": g.art_counts(plan),
        "pad_landings": pads,
    }


def dumps(g=None):
    return json.dumps(golden(g), indent=1, sort_keys=True) + "\n"


def main(argv):
    text = dumps()
    if "--out" in argv:
        path = argv[argv.index("--out") + 1]
        with open(path, "w", newline="\n") as f:
            f.write(text)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
