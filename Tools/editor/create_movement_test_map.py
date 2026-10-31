"""Velox M1 greybox movement test map: /Game/Maps/L_MovementTest (ADR 0002 section 7).

Run headless:
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_movement_test_map.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: the map is deleted and rebuilt on every run; the M_Greybox_* materials are reused/re-applied.
Built on the shared library Tools/editor/veloxmap.py (ADR 0006 section 2).
All dimensions are centimetres (1 Q3 unit = 2.5 cm, ADR 0002 section 1). Pawn capsule is 37.5 x 70 cm.
Layout (X forward, floor top at Z=0):
  strafe corridor  x 0..5120,  y -500..500 (walls 250 high), markers every 512 cm
  ramp bank        y 1000..3800, lanes 10/20/30/45/50 deg, 1600 cm slope -> 400 cm platform
  stairs           y 4700/5300/5900, 12 steps, risers 20/40/60 cm
  jump pads        y -1500/-2500/-3500: AVeloxJumpPad (AimAtTarget) + ATargetPoint + landing marker discs
  bunny-hop gaps   y 7600/8400/9200, gaps 500/700/900 cm over a 100 cm drop; walled floor-height runways behind
                   the start platforms (run-up 1200/2500/4500 cm to x -1600/-2900/-4900, markers every 512 cm,
                   PlayerStart_Gap<n> at each runway start); pit-exit steps beside each start platform
  bot nav links    (M4, ADR 0005 section 4) AVeloxNavLink: JumpPad links for pads A/B/C (pad floor point -> landing
                   marker / ledge), Drop links at the free edges of the pad A platform and the pad C ledge, gap links
                   500 cm -> Jump (both ways), 700 cm -> JumpFast, none for 900 cm (beyond a bot's reliable speed);
                   one NavMeshBoundsVolume over the whole map
"""
import math
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import veloxmap as vm  # noqa: E402

MAP_PATH = "/Game/Maps/L_MovementTest"


# ---------------------------------------------------------------- sections
def build_ground():
    # Main slab, top at Z=0: x -600..6000, y -4200..7000.
    vm.box("Ground", (2700, 1400, -50), (6600, 11200, 100), "Floor", "ground", folder="Ground")


def build_corridor():
    L, W = 5120.0, 1000.0
    wall_h = 250.0
    for side, y in (("L", -550), ("R", 550)):
        vm.box(f"CorridorWall_{side}", (L / 2, y, wall_h / 2), (L + 200, 100, wall_h), "Wall", "wall", folder="Corridor")
    vm.box("CorridorWall_Back", (-50, 0, wall_h / 2), (100, W, wall_h), "Wall", "wall", folder="Corridor")
    vm.box("CorridorWall_End", (L + 50, 0, wall_h / 2), (100, W, wall_h), "Wall", "wall", folder="Corridor")
    # Distance markers every 512 cm: thin floor strips (no collision) + text labelled in Q3 units and metres.
    for i in range(11):
        x = i * 512.0
        vm.box(f"Marker_{i:02d}", (x, 0, 0.5), (20, W, 1), "MarkerA" if i % 2 == 0 else "MarkerB", "marker",
               collide=False, folder="Markers")
        vm.text_label(f"MarkerText_{i:02d}", (x, -480, 140), f"{x / 2.5:.0f} u / {x / 100.0:.2f} m")


def build_ramps():
    L, T, PLAT, WID = 1600.0, 20.0, 400.0, 600.0
    for i, deg in enumerate((10, 20, 30, 45, 50)):
        y = 1000.0 + i * 700.0
        th = math.radians(deg)
        h = L * math.sin(th)
        run = L * math.cos(th)
        mat = "RampWalkable" if deg <= 45 else "RampBlocked"
        # Ramp slab: top surface runs from (0, y, 0) to (run, y, h).
        vm.ramp(f"Ramp_{deg:02d}deg", (0.0, y, 0.0), L, WID, T, deg, mat, folder="Ramps")
        vm.box(f"RampPlatform_{deg:02d}deg", (run + PLAT / 2, y, h / 2), (PLAT, WID, h), "Platform", "platform",
               folder="Ramps")
        vm.text_label(f"RampText_{deg:02d}", (-100, y, 140), f"{deg} deg" + ("" if deg <= 45 else " (not walkable)"),
                      folder="Ramps")


def build_stairs():
    TREAD, WID, N = 80.0, 400.0, 12
    for i, riser in enumerate((20, 40, 60)):
        y = 4700.0 + i * 600.0
        vm.stairs(f"Stairs_{riser}cm", 100.0, y, TREAD, WID, riser, N, "Stairs", folder="Stairs")
        vm.text_label(f"StairsText_{riser}", (-100, y, 140), f"{riser} cm risers" + (" (blocks)" if riser == 60 else ""),
                      folder="Stairs")


# Jump pads (ADR 0002 sections 4 and 7). The launch point is the trigger centre, placed at the capsule centre of a
# pawn standing on the pad (capsule half height 70 cm, floor Z=0). Q3 AimAtTarget puts the apex at the target, so the
# pad A target sits 30 cm above the platform's near edge: from the pad centre the arc clears the edge by ~28 cm, lands
# at ~x 1276 and ground friction stops the pawn at ~x 1445 (platform ends at 1500). Default gravity -2000 cm/s^2 gives
# launch speeds A 1558, B 1789, C 1600 cm/s. Entering the trigger from above launches from higher up (Q3 has the same
# trigger-centre approximation, ADR 0002 section 4) and overshoots.
PAD_LAUNCH_Z = vm.PAD_LAUNCH_Z

# Bot nav links (ADR 0005 section 4; conventions in Tools/editor/README.md).
GAP_TAKEOFF_INSET = 60.0   # gap link start inside the start platform edge (x 400)
GAP_LAND_INSET = 100.0     # gap link end inside the landing platform edge
GAP_LINKS = {              # gap -> (link type name, both ways, required take-off speed cm/s); 900: no link
    500: ("JUMP", True, 740.0),
    700: ("JUMP_FAST", False, 1100.0),
}


def build_pads():
    def pad(name, x, y, target_loc):
        return vm.jump_pad(f"JumpPad{name}", (x, y), target_loc, target_label=f"JumpPad{name}_TargetPoint", folder="JumpPads")

    def target(name, x, y, z):
        vm.cylinder_marker(f"JumpPad{name}_Target", (x, y, z + 2), (120, 120, 4), "Target", folder="JumpPads")

    # A: platform 800 cm away / 320 cm up (platform x 1100..1500, top Z=320).
    pad_a = pad("A", 500, -1500, (1100, -1500, 420))
    vm.box("JumpPadA_Platform", (1300, -1500, 160), (400, 400, 320), "Platform", "platform", folder="JumpPads")
    target("A", 1300, -1500, 320)
    vm.nav_link("JumpPadA_Link", unreal.VeloxNavLinkType.JUMP_PAD, (500, -1500, 0), (1300, -1500, 320), pad=pad_a)
    vm.drop_links("JumpPadA_Platform", 1300, -1500, 400, 320, "NSEW")
    vm.text_label("PadA_Text", (200, -1500, 140), "Pad A: +800 fwd / +320 up", folder="JumpPads")
    # B: flat, 1600 cm (target level with the launch point: minimum-speed 45 degree arc).
    pad_b = pad("B", 500, -2500, (2100, -2500, PAD_LAUNCH_Z))
    target("B", 2100, -2500, 0)
    vm.nav_link("JumpPadB_Link", unreal.VeloxNavLinkType.JUMP_PAD, (500, -2500, 0), (2100, -2500, 0), pad=pad_b)
    vm.text_label("PadB_Text", (200, -2500, 140), "Pad B: flat 1600", folder="JumpPads")
    # C: vertical 640 cm (capsule centre rises 640 cm); ledge beside the pad, marker disc at the apex.
    pad_c = pad("C", 500, -3500, (500, -3500, PAD_LAUNCH_Z + 680))
    vm.box("JumpPadC_Ledge", (900, -3500, 320), (400, 400, 640), "Platform", "platform", folder="JumpPads")
    target("C", 500, -3500, 680)
    # Vertical pad: the pawn steers onto the ledge after the launch (PadRun holds forward), so the link ends on the ledge.
    vm.nav_link("JumpPadC_Link", unreal.VeloxNavLinkType.JUMP_PAD, (500, -3500, 0), (900, -3500, 640), pad=pad_c)
    # The west edge faces pad C 400 cm away: a drop there would land on the pad, so it gets no link.
    vm.drop_links("JumpPadC_Ledge", 900, -3500, 400, 640, "NSE")
    vm.text_label("PadC_Text", (200, -3500, 140), "Pad C: vertical 680", folder="JumpPads")


# Runway length before each gap (cm, behind the 800 cm start platform): ADR 0002 strafe gain reaches 1.4x (1120 cm/s)
# after ~2048 cm and 1.79x by 4096 cm; required take-off speeds are ~740 / 1040 / 1330 cm/s.
GAP_RUNWAYS = {500: 1200.0, 700: 2500.0, 900: 4500.0}
GAP_LANE_Y = {500: 7600.0, 700: 8400.0, 900: 9200.0}
# Pit-exit steps: (y of the start platform edge they climb to, +1/-1 = direction they extend away from that edge).
PIT_STEPS = {500: (7800.0, 1), 700: (8600.0, 1), 900: (9400.0, 1)}
RUNWAY_WALL_H = 250.0


def build_gaps():
    # Pit floor 100 cm below the platforms; start platform x -400..400, landing platform after the gap.
    vm.box("GapPit", (1600, 8400, -150), (4400, 3000, 100), "Pit", "pit", folder="Gaps")
    for gap in (500, 700, 900):
        y = GAP_LANE_Y[gap]
        vm.box(f"GapStart_{gap}", (0, y, -50), (800, 400, 100), "Gap", "gapplat", folder="Gaps")
        land_start = 400.0 + gap
        vm.box(f"GapLand_{gap}", (land_start + 400, y, -50), (800, 400, 100), "Platform", "gapplat", folder="Gaps")
        vm.text_label(f"GapText_{gap}", (-300, y, 140), f"gap {gap} cm", folder="Gaps")
        if gap in GAP_LINKS:
            kind, both, speed = GAP_LINKS[gap]
            vm.nav_link(f"Gap{gap}_Link", getattr(unreal.VeloxNavLinkType, kind), (400.0 - GAP_TAKEOFF_INSET, y, 0.0),
                        (land_start + GAP_LAND_INSET, y, 0.0), both_ways=both, required_speed=speed)

        # Straight runway at floor height, ending where the start platform begins (x=-400), walled on both sides.
        L = GAP_RUNWAYS[gap]
        x0 = -400.0 - L
        vm.box(f"GapRunway_{gap}", (x0 + L / 2, y, -50), (L, 400, 100), "Floor", "runway", folder="Gaps")
        for side, dy in (("L", -210.0), ("R", 210.0)):
            vm.box(f"GapRunwayWall_{gap}_{side}", (x0 + L / 2, y + dy, RUNWAY_WALL_H / 2),
                   (L, 20, RUNWAY_WALL_H), "Wall", "gapwall", folder="Gaps")
        vm.box(f"GapRunwayWall_{gap}_Back", (x0 - 10, y, RUNWAY_WALL_H / 2), (20, 440, RUNWAY_WALL_H),
               "Wall", "gapwall", folder="Gaps")
        vm.text_label(f"GapRunwayText_{gap}", (x0 + 100, y - 150, 140), f"gap {gap} cm: run-up {L:.0f} cm",
                      size=30.0, folder="Gaps")
        i = 0
        while x0 + i * 512.0 <= -400.0:
            x = x0 + i * 512.0
            vm.box(f"GapMarker_{gap}_{i:02d}", (x, y, 0.5), (20, 400, 1), "MarkerA" if i % 2 == 0 else "MarkerB", "gapmarker",
                   collide=False, folder="Markers")
            vm.text_label(f"GapMarkerText_{gap}_{i:02d}", (x, y - 150, 140), f"{i * 512} cm", size=30.0, folder="Markers")
            i += 1
        vm.player_start(f"PlayerStart_Gap{gap}", (x0 + 120.0, y, 100))

        # Pit exit: four 20 cm steps (100 cm treads) in the free pit strip beside the start platform, rising from the
        # pit floor (Z=-100) to the platform edge, so a player who fell can walk back up onto the start platform.
        edge_y, d = PIT_STEPS[gap]
        for k in range(4):
            top = -20.0 - 20.0 * k
            h = top + 100.0
            vm.box(f"GapExit_{gap}_{k}", (0, edge_y + d * (50.0 + 100.0 * k), -100.0 + h / 2), (800, 100, h),
                   "Stairs", "pitstep", folder="Gaps")


def build_lighting_and_starts():
    vm.player_start("PlayerStart_Corridor", (120, 0, 100))
    vm.player_start("PlayerStart_Pads", (-300, -2500, 100))
    vm.static_sun_and_sky()


EXPECTED = {
    "ground": 1, "wall": 4, "marker": 11, "text": 11 + 5 + 3 + 3 + 3 + 3 + 17,
    "ramp": 5, "platform": 5 + 2,  # 5 ramp tops + pad A platform + pad C ledge
    "step": 36, "jumppad": 3, "targetpoint": 3, "target": 3, "pit": 1, "gapplat": 6,
    "runway": 3, "gapwall": 9, "gapmarker": 17, "pitstep": 12,
    "playerstart": 5, "light": 2,
    "navbounds": 1,
    "navlink": 3 + 4 + 3 + 2,  # pad links A/B/C, drops (pad A platform 4, pad C ledge 3), gaps 500/700
}


KILL_Z = -1000.0       # lowest floor is the gap pit at Z=-200 (pit slab centre -150, 100 thick, top -100); below this the pawn has left the map


def build_map():
    les = vm.rebuild_map(MAP_PATH)
    if les is None:
        return
    build_ground()
    build_corridor()
    build_ramps()
    build_stairs()
    build_pads()
    build_gaps()
    build_lighting_and_starts()
    # Everything: ground slab, gap runways (x to -4910), pit and gap lanes (y to 9900), ramp tops (z to ~1230).
    vm.nav_bounds((500.0, 2850.0, 600.0), (11200.0, 14500.0, 1800.0))
    vm.set_kill_z(KILL_Z)
    vm.check_pads()

    vm.assert_counts(EXPECTED)
    level_actors = vm.EAS.get_all_level_actors()
    n_level = len(level_actors)
    n_spawned = sum(vm.COUNTS.values())
    vm.log(f"level actors in world: {n_level} (spawned by script: {n_spawned})")
    if n_level < n_spawned:
        vm.fail(f"level has fewer actors ({n_level}) than spawned ({n_spawned})")
    n_sm = sum(1 for a in level_actors if isinstance(a, unreal.StaticMeshActor))
    n_ps = sum(1 for a in level_actors if isinstance(a, unreal.PlayerStart))
    exp_sm = sum(v for k, v in EXPECTED.items() if k not in ("text", "playerstart", "light", "jumppad", "targetpoint", "navlink", "navbounds"))
    n_pads = sum(1 for a in level_actors if isinstance(a, unreal.VeloxJumpPad))
    n_tp = sum(1 for a in level_actors if isinstance(a, unreal.TargetPoint))
    n_links = sum(1 for a in level_actors if isinstance(a, unreal.VeloxNavLink))
    vm.log(f"VeloxNavLinks={n_links} (expected {EXPECTED['navlink']})")
    if n_links != EXPECTED["navlink"]:
        vm.fail(f"nav link count {n_links} differs from {EXPECTED['navlink']}")
    vm.log(f"StaticMeshActors={n_sm} (expected {exp_sm}) PlayerStarts={n_ps} (expected 5) "
           f"VeloxJumpPads={n_pads} (expected 3) TargetPoints={n_tp} (expected 3)")
    if n_sm != exp_sm or n_ps != 5 or n_pads != 3 or n_tp != 3:
        vm.fail("level actor class counts mismatch")
    # Map check runs AVeloxJumpPad::CheckForErrors (launch speed validation); the log shows its summary line.
    vm.save_map(les)


def main():
    vm.configure(log_prefix="[VeloxMoveMap]", fill_nav_kinds=("platform",))
    vm.greybox_materials(create=True)
    if not vm.ERRORS:
        build_map()
    vm.raise_if_errors("movement test map")
    vm.log("done")


main()
