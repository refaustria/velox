"""DM1 "Praesidium": the layout table (ADR 0006 sections 2 and 3, amendments E0.2, E0.3 (e), E0.5). Single source of truth for
`create_dm1_map.py --mode blockout` (task M5-05) and, later, `--mode art` (task 15). Data only: no `unreal` import; the few
helper functions are pure arithmetic over the records.

Centimetres, X east, Y north, lowest walkable floor top at Z = 0 (FLOOR_Z, E0.3 e), 100 cm grid, 400 cm kit module.
Footprint 70 x 38 m (X -3500..3500, Y -2300..1500), three levels (0 / 320 / 640), level step 320.

Top-down sketch (north up; walls are 100 cm, all 100 cm):

  y 1500 +----------------------- C North Lane (y 800..1400, 4 m high, 68 m strafe lane) -------------------------+
         |            [LG ledge +80 at x 0]  YA-N                                                                |
  y  700 +--door--+----------+..........+===========+..........+----------+--door--+
         | A Mega Hall       | pocket   |  E Rail   |  pocket  |       B RA Yard    |
         | x -3400..-1600    |<walkway->|  Tower    |<walkway->|    x 1600..3400    |
         | ramp 15 deg to the| z 320    | x +-600   |  z 320   | stairs N+S to the  |
         | ledge (Mega)      | 4 m wide | y +-700   |          | balcony (RA)       |
  y -700 +--door--+----------+..........+===gate====+..........+----------+--door--+
         | D South Hall (y -2200..-800, 68 m lane, 10 m clear + balcony y -2200..-1800 at z 320, stairs at both ends)|
         | bridges x -2000..-1600 and 1600..2000 (z 320) join the balcony to the A ledge / B balcony                |
  y-2300 +----------------------------------------------------------------------------------------------------------+
         x -3500                                   0                                                       x 3500

E: ground (Quad, FFA only), z 320 docks and a mid bridge between the two walkways, z 640 top slab (RG) reached by the D pad
(through the south gate) or by a rocket jump from either dock. Upper ring: A ledge -> walkway -> E mid bridge -> walkway ->
B balcony -> bridge -> D balcony -> bridge -> A ledge. Lower ring: A -> C -> B -> D -> A (doors in the A/B north and south walls).
"""
from collections import namedtuple
import math

MAP_ID = "DM1"
DISPLAY_NAME = "Praesidium"        # ADR 0006 E0.5; shown by a text_label next to the intermission camera, no UI binding yet
FLOOR_Z = 0.0                    # E0.3 (e): level 0 is the lowest walkable floor top
KILL_Z = -1000.0                 # task m4-bots/07f
LEVEL_STEP = 320.0
GRID = 100.0
MODULE = 400.0
RAMP_DEG = 15.0
RISER = 20.0                     # stairs
TREAD = 50.0
WALL_T = 100.0

# ---------------------------------------------------------------- records
Room = namedtuple("Room", "id name x0 x1 y0 y1 floor_z ceiling cls")          # ceiling = clear height above floor_z; cls: atrium | room | corridor
Box = namedtuple("Box", "id kind mat x0 x1 y0 y1 z0 z1")                       # axis aligned; kind drives the collision / nav policy
Opening = namedtuple("Opening", "o0 o1 zlo zhi")                               # along the wall's run axis; zlo > 0: sill below, zhi < top: lintel above
Wall = namedtuple("Wall", "id run s0 s1 b0 b1 top openings")                   # run 'x': spans x s0..s1, band y b0..b1; run 'y': the converse
Stairs = namedtuple("Stairs", "id x_from x_dir y0 y1 steps")                   # step s: tread TREAD along x_dir, top (s+1)*RISER
Ramp = namedtuple("Ramp", "id x0 y0 y1 rise")                                  # rises along +X at RAMP_DEG from (x0, FLOOR_Z) to z = rise
Pad = namedtuple("Pad", "id x y target land")                                  # target = AimAtTarget apex point; land = authored landing (link end)
Item = namedtuple("Item", "id definition x y z disabled_in_modes")
Spawn = namedtuple("Spawn", "id x y yaw")
Link = namedtuple("Link", "id type start end pad")                              # type: JUMP_PAD | ROCKET_JUMP | DROP | JUMP | JUMP_FAST
Light = namedtuple("Light", "id kind x y z")                                    # placeholder for task 15 (ignored in blockout mode)
Clip = namedtuple("Clip", "id x0 x1 y0 y1 z0 z1")                               # invisible BlockAll, no nav influence

# Kinds: floor/wall/platform/step stand on the ground (fill_collision_underneath_for_navmesh on); ramp, bridge, bridgewall and
# lintel float over walkable space and must not be filled (E0.2 d: every solid kind except ramp and bridge, and nothing that hangs).
FILL_NAV_KINDS = ("floor", "wall", "platform", "step")
FLOATING_KINDS = ("ramp", "bridge", "bridgewall", "lintel")

# ---------------------------------------------------------------- rooms (for the assertions of create_dm1_map.py)
ROOMS = [
    Room("A", "Mega Hall", -3400, -1600, -700, 700, 0, 800, "atrium"),
    Room("B", "RA Yard", 1600, 3400, -700, 700, 0, 800, "atrium"),
    Room("C", "North Lane", -3400, 3400, 800, 1400, 0, 400, "corridor"),
    Room("D", "South Hall", -3400, 3400, -2200, -800, 0, 900, "room"),
    Room("E", "Rail Tower", -600, 600, -700, 700, 0, 1240, "room"),
    Room("WalkW", "West walkway", -1500, -700, -200, 200, 320, 920, "corridor"),
    Room("WalkE", "East walkway", 700, 1500, -200, 200, 320, 920, "corridor"),
]
RJ_CLEARANCE_MIN = 900.0     # 07f: a rocket-jump apex is <= 880 cm above the take-off

# ---------------------------------------------------------------- solid boxes
BOXES = [
    Box("Ground", "floor", "Floor", -3500, 3500, -2300, 1500, -100, 0),
    # A ledge (Mega), B balcony (RA): 4 m deep, 14 m long, z 320
    Box("A_Ledge", "platform", "Platform", -2100, -1600, -700, 700, 0, 320),
    Box("B_Balcony", "platform", "Platform", 1600, 2100, -700, 700, 0, 320),
    # D balcony along the south wall
    Box("D_Balcony", "platform", "Platform", -2000, 2000, -2200, -1800, 0, 320),
    # walkways A/B <-> E at z 320: solid, enclosed by side walls
    Box("WalkW_Floor", "platform", "Platform", -1500, -700, -200, 200, 0, 320),
    Box("WalkE_Floor", "platform", "Platform", 700, 1500, -200, 200, 0, 320),
    # E docks at z 320, the thin mid bridge between them, the top slab at z 640
    Box("E_DockW", "platform", "Platform", -600, -300, -200, 200, 0, 320),
    Box("E_DockE", "platform", "Platform", 300, 600, -200, 200, 0, 320),
    Box("E_MidBridge", "bridge", "Platform", -300, 300, -200, 200, 300, 320),
    Box("E_TopSlab", "bridge", "Platform", -200, 200, -250, 250, 620, 640),
    # D <-> A ledge / B balcony bridges over the south hall (underside 300 cm) with hanging side walls
    Box("BridgeW_Deck", "bridge", "Platform", -2000, -1600, -1800, -800, 300, 320),
    Box("BridgeE_Deck", "bridge", "Platform", 1600, 2000, -1800, -800, 300, 320),
    Box("BridgeW_WallW", "bridgewall", "Wall", -2100, -2000, -1800, -800, 300, 900),
    Box("BridgeW_WallE", "bridgewall", "Wall", -1600, -1500, -1800, -800, 300, 900),
    Box("BridgeE_WallW", "bridgewall", "Wall", 1500, 1600, -1800, -800, 300, 900),
    Box("BridgeE_WallE", "bridgewall", "Wall", 2000, 2100, -1800, -800, 300, 900),
    # C: the LG ledge (+80 cm, 4 x 2 m) on the north side of the lane at mid
    Box("C_LGLedge", "platform", "Platform", -200, 200, 1200, 1400, 0, 80),
    # walkway side walls (stand on the ground, 12.4 m high: rocket-jump clearance)
    Box("WalkW_WallN", "wall", "Wall", -1500, -700, 200, 300, 0, 1240),
    Box("WalkW_WallS", "wall", "Wall", -1500, -700, -300, -200, 0, 1240),
    Box("WalkE_WallN", "wall", "Wall", 700, 1500, 200, 300, 0, 1240),
    Box("WalkE_WallS", "wall", "Wall", 700, 1500, -300, -200, 0, 1240),
]

# ---------------------------------------------------------------- walls (strips with openings)
O = Opening
WALLS = [
    # outer perimeter, split by room height
    Wall("Wall_W_D", "y", -2300, -700, -3500, -3400, 900, ()),
    Wall("Wall_W_A", "y", -700, 800, -3500, -3400, 800, ()),
    Wall("Wall_W_C", "y", 800, 1500, -3500, -3400, 400, ()),
    Wall("Wall_E_D", "y", -2300, -700, 3400, 3500, 900, ()),
    Wall("Wall_E_B", "y", -700, 800, 3400, 3500, 800, ()),
    Wall("Wall_E_C", "y", 800, 1500, 3400, 3500, 400, ()),
    Wall("Wall_S", "x", -3400, 3400, -2300, -2200, 900, ()),
    Wall("Wall_N", "x", -3400, 3400, 1400, 1500, 400, ()),
    # north line between C and the halls / E (y 700..800)
    Wall("WallNL_A", "x", -3400, -1500, 700, 800, 800, (O(-3000, -2600, 0, 320),)),
    Wall("WallNL_Pocket1", "x", -1500, -700, 700, 800, 400, ()),
    Wall("WallNL_E", "x", -700, 700, 700, 800, 1240, (O(-500, -200, 0, 320), O(200, 500, 0, 320))),
    Wall("WallNL_Pocket2", "x", 700, 1500, 700, 800, 400, ()),
    Wall("WallNL_B", "x", 1500, 3400, 700, 800, 800, (O(3000, 3400, 0, 320),)),
    # south line between the halls / E and D (y -800..-700, E: -800..-600)
    Wall("WallSL_A", "x", -3400, -1500, -800, -700, 900, (O(-3000, -2600, 0, 320), O(-2000, -1600, 320, 700))),
    Wall("WallSL_Pocket1", "x", -1500, -700, -800, -700, 900, ()),
    Wall("WallSL_E", "x", -700, 700, -800, -700, 1240, (O(-200, 200, 0, 1000),)),
    Wall("WallSL_Pocket2", "x", 700, 1500, -800, -700, 900, ()),
    Wall("WallSL_B", "x", 1500, 3400, -800, -700, 900, (O(3000, 3400, 0, 320), O(1600, 2000, 320, 700))),
    # east wall of A, west wall of B (walkway openings above the z 320 threshold), west / east walls of E
    Wall("WallA_E", "y", -700, 700, -1600, -1500, 800, (O(-200, 200, 320, 800),)),
    Wall("WallB_W", "y", -700, 700, 1500, 1600, 800, (O(-200, 200, 320, 800),)),
    Wall("WallE_W", "y", -700, 700, -700, -600, 1240, (O(-200, 200, 320, 1240),)),
    Wall("WallE_E", "y", -700, 700, 600, 700, 1240, (O(-200, 200, 320, 1240),)),
]

STAIRS = [
    Stairs("D_StairsW", -2800, +1, -2200, -1800, 16),     # rises east to the balcony (x -2000)
    Stairs("D_StairsE", 2800, -1, -2200, -1800, 16),      # rises west to the balcony (x 2000)
    Stairs("B_StairsN", 2900, -1, 300, 700, 16),
    Stairs("B_StairsS", 2900, -1, -700, -300, 16),
    Stairs("C_LGSteps", -400, +1, 1200, 1400, 4),         # 4 x 20 cm up to the LG ledge
]

RAMPS = [
    Ramp("A_Ramp", -3294.0, -200, 200, 320.0),            # top meets the ledge face (x -2000) flush at z 320
]

# ---------------------------------------------------------------- jump pads (AimAtTarget: the target is the apex, ADR 0002)
PADS = [
    Pad("PadA", -2600, -450, (-2100, -450, 420), (-1954, -450, 320)),   # lower A -> ledge
    Pad("PadB", 2600, 0, (2100, 0, 420), (1954, 0, 320)),                # lower B -> balcony
    Pad("PadD", -150, -950, (0, -250, 740), (32, -98, 640)),              # lower D -> E top (the RG pad), over the south gate; off the x = 0 RL line and the lane centre (task 06)
]

# ---------------------------------------------------------------- items
Q = "/Game/Items/DA_Item_"
DUEL = "Velox.Mode.Duel"


def _it(i, name, x, y, z=0.0, dis=()):
    return Item(i, Q + name.replace(".", "_"), x, y, z, tuple(dis))


def stair_z(x_from, x_dir, x):
    """Top of the stair step whose tread contains x."""
    s = int(math.floor(abs(x - x_from) / TREAD))
    return (s + 1) * RISER


def ramp_z(r, x):
    return (x - r.x0) * math.tan(math.radians(RAMP_DEG))


_R = RAMPS[0]
ITEMS = [
    # A Mega Hall
    _it("Mega", "Health.Mega", -2050, 660, 320),
    _it("PG", "Weapon.PlasmaGun", -2800, 500),
    _it("CellsAmmo", "Ammo.Cells", -2900, -550),
    _it("A_H25_1", "Health.Medium", -3300, -300),
    _it("A_H25_2", "Health.Medium", -2500, 450),
    _it("A_Shard_1", "Armor.Shard", -2800, 0, ramp_z(_R, -2800)),
    _it("A_Shard_2", "Armor.Shard", -2500, 0, ramp_z(_R, -2500)),
    _it("A_Shard_3", "Armor.Shard", -2200, 0, ramp_z(_R, -2200)),
    # B RA Yard
    _it("RA", "Armor.Red", 2050, -660, 320),
    _it("SG", "Weapon.Shotgun", 3100, 500),
    _it("ShellsAmmo", "Ammo.Shells", 3250, -500),
    _it("B_H25_1", "Health.Medium", 2900, -150),
    _it("B_H25_2", "Health.Medium", 3250, -150),
    _it("B_Shard_1", "Armor.Shard", 2775, 500, stair_z(2900, -1, 2775)),
    _it("B_Shard_2", "Armor.Shard", 2575, 500, stair_z(2900, -1, 2575)),
    _it("B_Shard_3", "Armor.Shard", 2375, 500, stair_z(2900, -1, 2375)),
    # C North Lane
    _it("LG", "Weapon.LightningGun", 0, 1300, 80),
    _it("YA_N", "Armor.Yellow", 800, 1000),
    _it("LightningAmmo", "Ammo.Lightning", 1100, 1300),
    _it("C_H5_1", "Health.Small", -3300, 900),
    _it("C_H5_2", "Health.Small", -3300, 1300),
    _it("C_H5_3", "Health.Small", -3100, 900),
    _it("C_H5_4", "Health.Small", -3100, 1300),
    # D South Hall
    _it("RL", "Weapon.RocketLauncher", 0, -1500),
    _it("YA_S", "Armor.Yellow", 0, -2000, 320),
    _it("RocketsAmmo", "Ammo.Rockets", -900, -1500),
    _it("D_H50", "Health.Large", 900, -1500),
    _it("D_H5_1", "Health.Small", -900, -2000, 320),
    _it("D_H5_2", "Health.Small", -450, -2000, 320),
    _it("D_H5_3", "Health.Small", 450, -2000, 320),
    _it("D_H5_4", "Health.Small", 900, -2000, 320),
    # E Rail Tower
    _it("RG", "Weapon.Railgun", 0, 0, 640),
    _it("E_H25", "Health.Medium", 0, 100, 320),
    _it("SlugsAmmo", "Ammo.Slugs", 450, -450),
    _it("Quad", "Powerup.Quad", 0, 0, 0, (DUEL,)),
]
WEAPON_IDS = ("PG", "SG", "LG", "RL", "RG")
MEGA_ID, RA_ID = "Mega", "RA"
AMMO_FOR = {"PG": "CellsAmmo", "SG": "ShellsAmmo", "LG": "LightningAmmo", "RL": "RocketsAmmo", "RG": "SlugsAmmo"}
AMMO_MIN_CM, AMMO_MAX_CM = 600.0, 1200.0

# ---------------------------------------------------------------- spawns (8): none sees Mega or RA, >= 10 m apart, <= 8 m (horizontal) from a weapon
SPAWNS = [
    Spawn("S1_CW", -2800, 1100, 0),      # PG, north door of A
    Spawn("S2_LGW", -750, 1350, 0),      # LG
    Spawn("S3_LGE", 750, 1350, 180),     # LG
    Spawn("S4_CE", 3000, 1100, 180),     # SG, north door of B
    Spawn("S5_RLW", -700, -1500, 0),     # RL
    Spawn("S6_RLE", 700, -1500, 180),    # RL
    Spawn("S7_E1", -400, -400, 45),      # RG (ground of E)
    Spawn("S8_E2", 400, 400, 225),       # RG
]
SPAWN_MIN_APART = 1000.0
SPAWN_MAX_WEAPON = 800.0
EYE_Z = 160.0

# ---------------------------------------------------------------- nav links (ADR 0005 section 4; Tools/editor/README.md)
DROP_IN, DROP_OUT = 60.0, 150.0


def _drop(i, sx, sy, sz, ex, ey, ez):
    return Link(i, "DROP", (sx, sy, sz), (ex, ey, ez), None)


LINKS = [
    Link("PadLink_A", "JUMP_PAD", (-2600, -450, 0), PADS[0].land, "PadA"),
    Link("PadLink_B", "JUMP_PAD", (2600, 0, 0), PADS[1].land, "PadB"),
    Link("PadLink_D", "JUMP_PAD", (PADS[2].x, PADS[2].y, 0), PADS[2].land, "PadD"),
    Link("RJ_West", "ROCKET_JUMP", (-650, 0, 320), (0, 0, 640), None),   # run-up 450 cm from the top slab edge (x -200)
    Link("RJ_East", "ROCKET_JUMP", (650, 0, 320), (0, 0, 640), None),
    # drops: 60 cm inside the free edge, 150 cm outside it
    _drop("Drop_ALedge_N", -2100 + DROP_IN, 450, 320, -2100 - DROP_OUT, 450, 0),
    _drop("Drop_ALedge_S", -2100 + DROP_IN, -450, 320, -2100 - DROP_OUT, -450, 0),
    _drop("Drop_BBalcony", 2100 - DROP_IN, 0, 320, 2100 + DROP_OUT, 0, 0),
    _drop("Drop_DBalcony_1", -1200, -1800 - DROP_IN, 320, -1200, -1800 + DROP_OUT, 0),
    _drop("Drop_DBalcony_2", -400, -1800 - DROP_IN, 320, -400, -1800 + DROP_OUT, 0),
    _drop("Drop_DBalcony_3", 400, -1800 - DROP_IN, 320, 400, -1800 + DROP_OUT, 0),
    _drop("Drop_DBalcony_4", 1200, -1800 - DROP_IN, 320, 1200, -1800 + DROP_OUT, 0),
    _drop("Drop_DockW_N", -450, 200 - DROP_IN, 320, -450, 200 + DROP_OUT, 0),
    _drop("Drop_DockW_S", -450, -200 + DROP_IN, 320, -450, -200 - DROP_OUT, 0),
    _drop("Drop_DockE_N", 450, 200 - DROP_IN, 320, 450, 200 + DROP_OUT, 0),
    _drop("Drop_DockE_S", 450, -200 + DROP_IN, 320, 450, -200 - DROP_OUT, 0),
    _drop("Drop_MidBridge_N", 0, 200 - DROP_IN, 320, 0, 200 + DROP_OUT, 0),
    _drop("Drop_MidBridge_S", 0, -200 + DROP_IN, 320, 0, -200 - DROP_OUT, 0),
    _drop("Drop_Top_W", -200 + DROP_IN, 0, 640, -200 - DROP_OUT, 0, 320),
    _drop("Drop_Top_E", 200 - DROP_IN, 0, 640, 200 + DROP_OUT, 0, 320),
    _drop("Drop_Top_N", 0, 250 - DROP_IN, 640, 0, 250 + DROP_OUT, 0),
    _drop("Drop_Top_S", 0, -250 + DROP_IN, 640, 0, -250 - DROP_OUT, 0),
]

# ---------------------------------------------------------------- lights (task 15 placeholders; not spawned in blockout mode)
LIGHTS = [
    Light("L_A", "point", -2600, 0, 700),
    Light("L_B", "point", 2600, 0, 700),
    Light("L_E", "point", 0, 0, 1100),
    Light("L_C", "rect", 0, 1100, 380),
    Light("L_D", "rect", 0, -1500, 880),
]

# ---------------------------------------------------------------- clip: perimeter flush outside the walls, global roof, a ceiling per room
# (a room ceiling covers the walls' full thickness so no wall top is exposed)
CLIPS = [
    Clip("Clip_W", -3600, -3500, -2400, 1600, 0, 1800),
    Clip("Clip_E", 3500, 3600, -2400, 1600, 0, 1800),
    Clip("Clip_S", -3600, 3600, -2400, -2300, 0, 1800),
    Clip("Clip_N", -3600, 3600, 1500, 1600, 0, 1800),
    Clip("Clip_Roof", -3600, 3600, -2400, 1600, 1800, 1900),
    Clip("Ceil_A", -3500, -1500, -800, 800, 800, 900),
    Clip("Ceil_B", 1500, 3500, -800, 800, 800, 900),
    Clip("Ceil_C", -3500, 3500, 700, 1500, 400, 500),
    Clip("Ceil_D", -3500, 3500, -2300, -700, 900, 1000),
    Clip("Ceil_E", -700, 700, -800, 800, 1240, 1340),
    Clip("Ceil_WalkW", -1500, -700, -300, 300, 1240, 1340),
    Clip("Ceil_WalkE", 700, 1500, -300, 300, 1240, 1340),
]

NAV_CENTER = (0.0, -400.0, 600.0)
NAV_SIZE = (7400.0, 4200.0, 1800.0)      # X -3700..3700, Y -2500..1700, Z -300..1500

INTERMISSION_CAMERA = ((0.0, -400.0, 2600.0), (0.0, -60.0, 90.0))   # location, (roll, pitch, yaw)
INTERMISSION_LABEL = (0.0, -150.0, 2250.0)

UPPER_RING_MIN_M, UPPER_RING_MAX_M = 45.0, 50.0       # Mega -> RA, measured with velox.bots.DumpNav (task notes)

# Per-kind actor counts that do not depend on the geometry expansion (the geometry kinds are counted by the generator).
EXPECTED = {
    "pickup": 35, "playerstart": 8, "jumppad": 3, "targetpoint": 3, "navbounds": 1, "navlink": 22,
    "camera": 1, "text": 1, "light": 2,
}
