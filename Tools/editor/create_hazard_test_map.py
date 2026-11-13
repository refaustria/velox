"""Velox M7 hazard test map: /Game/Maps/L_HazardTest (ADR 0008 section 3.7, task H2).

Run headless:
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=<abs>/Tools/editor/create_hazard_test_map.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: the map is cleared and rebuilt on every run. Reuses the /Game/Materials/M_Greybox_* materials created by
create_movement_test_map.py. Centimetres, floor top at Z=0, X forward. Layout:
  main floor x -2400..0, y -1000..1000 (top 0), walls N, S and W (400 high), the east side open to the gap;
  one lava channel cut into the floor: x -1500..-1200, y -600..600, bed (Box kind "hazard") top -60, AVeloxHazardVolume
    Lava from -60 to the surface at -40;
  the platform: 800 x 800 at x 1000..1800, y -400..400, top 0, open edges, 1000 cm of void gap west of it;
  one jump pad on the main floor at (-300, 0) across the gap onto the platform (+ JUMP_PAD nav link to its centre);
  a void slab (AVeloxHazardVolume Void) z -1300..-500 under everything: footprint plus 4000 cm on every side, so any
    knockback lands in it (ADR 0008 3.1: KillZ -2600 stays the safety net, a KillZ death is a defect);
  one AVeloxDummySpawner on the platform (bAutoSpawn off; tests spawn it), four PlayerStarts on the main floor
    (none near the channel), static sun + sky, one NavMeshBoundsVolume over floor and platform.
FtHazard (Source/Velox/Tests/VeloxHazardFt.cpp, infra/ci/fts.sh) depends on these coordinates.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import veloxmap as vm  # noqa: E402


MAP_PATH = "/Game/Maps/L_HazardTest"

FLOOR_X0, FLOOR_X1 = -2400.0, 0.0
FLOOR_HALF_Y = 1000.0
SLAB_T = 100.0
WALL_H = 400.0
WALL_T = 100.0
LAVA_X0, LAVA_X1 = -1500.0, -1200.0
LAVA_HALF_Y = 600.0
LAVA_BED_Z = -60.0
LAVA_SURFACE_Z = -40.0
PLATFORM_X0, PLATFORM_X1 = 1000.0, 1800.0
PLATFORM_HALF = 400.0
VOID_TOP, VOID_BOTTOM = -500.0, -1300.0
VOID_MARGIN = 4000.0
KILL_Z = -2600.0
PAD_XY = (-300.0, 0.0)
PLATFORM_CENTER = ((PLATFORM_X0 + PLATFORM_X1) / 2, 0.0)
# AimAtTarget: the target is the apex. Apex 450 over x 500 (800 cm from the pad): |v| about 1790 cm/s at gravity 2000, landing
# about x 1280 on the platform (x 1000..1800). The nav link still ends at the platform centre.
PAD_APEX = (500.0, 0.0, 450.0)

STARTS = [  # (label, x, y, yaw)
    ("PlayerStart_SW", -2100, -700, 0),
    ("PlayerStart_NW", -2100, 700, 0),
    ("PlayerStart_SE", -700, -700, 0),
    ("PlayerStart_NE", -700, 700, 0),
]

EXPECTED = {"ground": 4, "wall": 3, "hazard": 1, "platform": 1, "hazardvolume": 2, "jumppad": 1, "targetpoint": 1, "navlink": 1,
            "dummyspawner": 1, "playerstart": 4, "light": 2, "navbounds": 1}


def span(a, b):
    return (a + b) / 2, b - a


def build_floor_and_lava():
    zc = -SLAB_T / 2
    for name, (x0, x1, y0, y1) in (
        ("Floor_W", (FLOOR_X0, LAVA_X0, -FLOOR_HALF_Y, FLOOR_HALF_Y)),
        ("Floor_E", (LAVA_X1, FLOOR_X1, -FLOOR_HALF_Y, FLOOR_HALF_Y)),
        ("Floor_N", (LAVA_X0, LAVA_X1, LAVA_HALF_Y, FLOOR_HALF_Y)),
        ("Floor_S", (LAVA_X0, LAVA_X1, -FLOOR_HALF_Y, -LAVA_HALF_Y)),
    ):
        (cx, sx), (cy, sy) = span(x0, x1), span(y0, y1)
        vm.box(name, (cx, cy, zc), (sx, sy, SLAB_T), "Floor", "ground", folder="Floor")
    (cx, sx), (cy, sy) = span(LAVA_X0, LAVA_X1), span(-LAVA_HALF_Y, LAVA_HALF_Y)
    (bz, bs) = span(-SLAB_T, LAVA_BED_Z)
    vm.box("LavaBed", (cx, cy, bz), (sx, sy, bs), "RampBlocked", "hazard", folder="Hazards")
    (lz, ls) = span(LAVA_BED_Z, LAVA_SURFACE_Z)
    vm.hazard("Hazard_Lava", "lava", (cx, cy, lz), (sx, sy, ls))

    (wx, wsx) = span(FLOOR_X0 - WALL_T, FLOOR_X1)
    vm.box("Wall_N", (wx, FLOOR_HALF_Y + WALL_T / 2, WALL_H / 2), (wsx, WALL_T, WALL_H), "Wall", "wall", folder="Floor")
    vm.box("Wall_S", (wx, -FLOOR_HALF_Y - WALL_T / 2, WALL_H / 2), (wsx, WALL_T, WALL_H), "Wall", "wall", folder="Floor")
    vm.box("Wall_W", (FLOOR_X0 - WALL_T / 2, 0, WALL_H / 2), (WALL_T, 2 * FLOOR_HALF_Y, WALL_H), "Wall", "wall", folder="Floor")


def build_platform_pad_void():
    (cx, sx) = span(PLATFORM_X0, PLATFORM_X1)
    vm.box("Platform", (cx, 0, -SLAB_T / 2), (sx, 2 * PLATFORM_HALF, SLAB_T), "Platform", "platform", folder="Platform")
    pad = vm.jump_pad("Pad_Gap", PAD_XY, PAD_APEX, pad_label="Pad_Gap",
                      target_label="Pad_Gap_Target", folder="Platform")
    vm.nav_link("Pad_Gap_Link", unreal.VeloxNavLinkType.JUMP_PAD, (PAD_XY[0], PAD_XY[1], 0.0), (PLATFORM_CENTER[0], PLATFORM_CENTER[1], 0.0), pad=pad)
    sp = vm.spawn(unreal.VeloxDummySpawner, "DummySpawn_Platform", (PLATFORM_CENTER[0], 200.0, 110.0), None, "Platform", "dummyspawner")
    sp.set_editor_property("auto_spawn", False)

    x0, x1 = FLOOR_X0 - WALL_T - VOID_MARGIN, PLATFORM_X1 + VOID_MARGIN
    y0, y1 = -(FLOOR_HALF_Y + WALL_T + VOID_MARGIN), FLOOR_HALF_Y + WALL_T + VOID_MARGIN
    (vx, vsx), (vy, vsy), (vz, vsz) = span(x0, x1), span(y0, y1), span(VOID_BOTTOM, VOID_TOP)
    vm.hazard("Hazard_Void", "void", (vx, vy, vz), (vsx, vsy, vsz))


def build_map():
    les = vm.rebuild_map(MAP_PATH)
    if les is None:
        return
    build_floor_and_lava()
    build_platform_pad_void()
    for label, x, y, yaw in STARTS:
        vm.player_start(label, (x, y, 100), yaw, folder="Starts")
    vm.static_sun_and_sky()
    vm.set_kill_z(KILL_Z)
    (nx, nsx) = span(FLOOR_X0 - WALL_T - 100, PLATFORM_X1 + 200)
    # top below the wall tops (WALL_H): the lint samples every pawn-blocking top inside the bounds, and the unreachable 100 cm wall tops would
    # be isolated clusters (ADR 0008 3.5 task H3); flights above the bounds do not need nav
    vm.nav_bounds((nx, 0.0, 90.0), (nsx, 2 * (FLOOR_HALF_Y + WALL_T + 100), WALL_H + 180.0 - 20.0))
    vm.check_pads()

    vm.assert_counts(EXPECTED)
    actors = vm.EAS.get_all_level_actors()
    n_hz = sum(1 for a in actors if isinstance(a, unreal.VeloxHazardVolume))
    n_ps = sum(1 for a in actors if isinstance(a, unreal.PlayerStart))
    vm.log(f"HazardVolumes={n_hz} (expected 2) PlayerStarts={n_ps} (expected 4)")
    if n_hz != 2 or n_ps != 4:
        vm.fail("level actor class counts mismatch")
    vm.save_map(les)


def main():
    vm.configure(log_prefix="[VeloxHazardMap]", fill_nav_kinds=())
    vm.greybox_materials(keys=("Floor", "Wall", "Platform", "Pad", "RampBlocked"))
    if not vm.ERRORS:
        build_map()
    vm.raise_if_errors("hazard test map")
    vm.log("done")


main()
