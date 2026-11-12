"""Shim (ADR 0008 section 1.2, task M7 P2): the DM1 generator is now `create_map.py --map dm1` (+ mapgen.py). Same CLI as before
(--mode blockout|art, --validate-only, VELOX_DM1_MODE)."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if "--map" not in sys.argv:
    sys.argv[1:1] = ["--map", "dm1"]
__shim__ = True
import create_map  # noqa: E402  (inside the editor it runs main() itself on import)

if __name__ == "__main__":
    sys.exit(create_map.main())
