"""Velox M7 gap-jump test map: /Game/Maps/L_GapTest (task M7 H11, FT_BotGapJump).

Run headless:
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=<abs>/Tools/editor/create_gap_test_map.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: the map is cleared and rebuilt on every run. Reuses the /Game/Materials/M_Greybox_* materials created by
create_movement_test_map.py. Centimetres, platform tops at Z=0, X forward. Two lanes, each a 1200 cm wide strip (bots
come in along the link or from the side, as onto the Nemus log from Log_Land) cut by one gap:
  lane 300 at y 0, lane 400 at y 1700; per lane a west platform x -1400..0 and an east platform x G..G+1400 (solid
    blocks, top 0, bottom -300), the gap x 0..G between them;
  in each gap a lava bed (Box kind "hazard", top -300) with an AVeloxHazardVolume Lava -300..-280: a bot that falls short
    is in the lava (Fornax lava hops) 300 cm below the take-off (Nemus log breaks: 320); no nav in the gap;
  per lane one JUMP nav link, both ways (Nemus), from 60 cm before the west edge to 100 cm onto the east platform (the Fornax
    / Nemus authoring rule; the way back takes off 100 cm before the east edge and lands 60 cm onto the west platform);
  one PlayerStart per lane on the west platform, static sun + sky, KillZ -1500, one NavMeshBoundsVolume over both lanes.
FT_BotGapJump (Source/Velox/Tests/VeloxBotGapFt.cpp, infra/ci/fts.sh) depends on these coordinates.
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import veloxmap as vm  # noqa: E402


MAP_PATH = "/Game/Maps/L_GapTest"

LANES = {300: 0.0, 400: 1700.0}   # gap (cm) -> lane centre y
LANE_HALF_Y = 600.0               # 1200 wide: side approaches up to ~70 degrees (Nemus Log_Land, the Templum podium)
RUN_LEN = 1400.0                  # platform length on each side of a gap
BLOCK_BOTTOM = -300.0
BED_TOP = -300.0
BED_T = 100.0
LAVA_T = 20.0
TAKEOFF_INSET = 60.0              # link start inside the west edge (x 0)
LAND_INSET = 100.0                # link end inside the east edge (x G)
KILL_Z = -1500.0

EXPECTED = {"ground": 4, "hazard": 2, "hazardvolume": 2, "navlink": 2, "playerstart": 2, "light": 2, "navbounds": 1}


def span(a, b):
    return (a + b) / 2, b - a


def build_lane(gap, y):
    (cy, sy) = span(y - LANE_HALF_Y, y + LANE_HALF_Y)
    (bz, bs) = span(BLOCK_BOTTOM, 0.0)
    for name, (x0, x1) in ((f"Gap{gap}_West", (-RUN_LEN, 0.0)), (f"Gap{gap}_East", (gap, gap + RUN_LEN))):
        (cx, sx) = span(x0, x1)
        vm.box(name, (cx, cy, bz), (sx, sy, bs), "Floor", "ground", folder=f"Lane{gap}")
    (cx, sx) = span(0.0, gap)
    (hz, hs) = span(BED_TOP - BED_T, BED_TOP)
    vm.box(f"Gap{gap}_LavaBed", (cx, cy, hz), (sx, sy, hs), "RampBlocked", "hazard", folder=f"Lane{gap}")
    (lz, ls) = span(BED_TOP, BED_TOP + LAVA_T)
    vm.hazard(f"Gap{gap}_Lava", "lava", (cx, cy, lz), (sx, sy, ls), folder=f"Lane{gap}")
    vm.nav_link(f"Gap{gap}_Link", unreal.VeloxNavLinkType.JUMP, (-TAKEOFF_INSET, y, 0.0), (gap + LAND_INSET, y, 0.0), both_ways=True)
    vm.player_start(f"PlayerStart_Gap{gap}", (-1000.0, y, 100.0), 0.0, folder="Starts")


def build_map():
    les = vm.rebuild_map(MAP_PATH)
    if les is None:
        return
    for gap, y in LANES.items():
        build_lane(gap, y)
    vm.static_sun_and_sky()
    vm.set_kill_z(KILL_Z)
    x0, x1 = -RUN_LEN - 100.0, max(LANES) + RUN_LEN + 100.0
    y0, y1 = min(LANES.values()) - LANE_HALF_Y - 100.0, max(LANES.values()) + LANE_HALF_Y + 100.0
    (nx, nsx), (ny, nsy), (nz, nsz) = span(x0, x1), span(y0, y1), span(BED_TOP - 150.0, 300.0)
    vm.nav_bounds((nx, ny, nz), (nsx, nsy, nsz))

    vm.assert_counts(EXPECTED)
    actors = vm.EAS.get_all_level_actors()
    n_hz = sum(1 for a in actors if isinstance(a, unreal.VeloxHazardVolume))
    n_links = sum(1 for a in actors if isinstance(a, unreal.VeloxNavLink))
    vm.log(f"HazardVolumes={n_hz} (expected 2) NavLinks={n_links} (expected 2)")
    if n_hz != 2 or n_links != 2:
        vm.fail("level actor class counts mismatch")
    vm.save_map(les)


def main():
    vm.configure(log_prefix="[VeloxGapMap]", fill_nav_kinds=("ground",))
    vm.greybox_materials(keys=("Floor", "RampBlocked"))
    if not vm.ERRORS:
        build_map()
    vm.raise_if_errors("gap test map")
    vm.log("done")


main()
