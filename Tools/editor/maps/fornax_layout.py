"""Fornax: the layout table (ADR 0008 sections 1.2, 3.4, 4; concept docs/maps/fornax.md, M7 task S4). Single source of truth for the
Fornax blockout (task B4) and, later, its art mode. Data only: no `unreal` import; the few helper functions are pure arithmetic.

Centimetres, X east, Y north, lowest walkable floor top at Z = 0, 100 cm grid, 400 cm kit module. Footprint 48 x 36 m (X -2400..2400,
Y -1800..1800, outer walls 100 cm), levels 0 / 320 (L1: Charging Deck, Flue, West Landing, East Gallery) / 640 (Crane Bridge). Lava surface
Z = -40, lava bed (collision, kind "hazard") top Z = -60: the rim is 60 cm (concept section 12 rule 1).

Top-down (north up; the rectangle tables in docs/maps/fornax.md section 3 are binding, this is a reading aid):

  y 1800 +--------------- solid -----------+ Landing --+ Casting Hall B (Mega) --------+ Gallery G (z 320)
  y 1400 | Flue F (z 320, 26 x 4 m)  -------> West Landing, Crane Bridge (z 640) above the mid strip between trenches T1 / T2
  y  900 | Crucible A (Red Armor island)  | Hearth H (furnace, RL) | mass (Y -300..300, tunnel with the RG on the gallery)
  y -900 | Slag Run S (lava gutter)   -------------------------> | Tap Room C (Yellow Armor, Pad 2, 15 deg ramp up to the gallery)
  x -2400            -900   -800     200   300                              1900   2300

Lava (8 Hazard records, each over a Box of kind "hazard"): the pool around the island and the slag bridge (5 pieces), the Slag gutter,
trenches T1 and T2. Rooms are enclosed by walls up to the higher of the two ceilings they separate; the clip ceilings sit on the room
ceilings (2, "Ceil_*"), so a rocket jump never leaves a room.
"""
import math

MAP_ID = "FORNAX"
SLUG = "fornax"
DISPLAY_NAME = "Fornax"
MAP_PATH = "/Game/Maps/Fornax/L_Fornax_Blockout"
ART_MAP_PATH = "/Game/Maps/Fornax/L_Fornax"
LOG_PREFIX = "[VeloxFornax]"
THEME = "Fornax"
FLOOR_Z = 0.0                    # E0.3 (e): level 0 is the lowest walkable floor top (the lava bed at -60 is not a floor, ADR 0008 3.5)
KILL_Z = -1000.0                 # safety net only (concept section 12 rule 6)
LEVEL_STEP = 320.0
GRID = 100.0
MODULE = 400.0
RAMP_DEG = 15.0                  # the Tap ramp; the Chute sets deg=30 itself
RISER = 20.0
TREAD = 50.0
WALL_T = 100.0
LAVA_SURFACE_Z = -40.0
LAVA_BED_Z = -60.0               # bed top = hazard z0 = rim 60 below the floor
BED_THICK = 100.0

# ---------------------------------------------------------------- records
from maps.records import Room, Box, Opening, Wall, Stairs, Ramp, Pad, Item, Spawn, Link, Light, Clip, Hazard  # noqa: E402,F401

FILL_NAV_KINDS = ("floor", "wall", "platform", "step")
FLOATING_KINDS = ("ramp", "bridge", "bridgewall", "lintel")

# ---------------------------------------------------------------- rooms (concept section 2: ceilings clear above the room floor)
ROOMS = [
    Room("A", "Crucible", -2300, -900, -900, 900, 0, 960, "atrium"),
    Room("H", "Hearth", -800, 200, -900, 900, 0, 640, "room"),
    Room("S", "Slag Run", -2300, 200, -1700, -1000, 0, 400, "corridor"),
    Room("B", "Casting Hall", 300, 2300, 300, 1700, 0, 1240, "atrium"),
    Room("C", "Tap Room", 300, 2300, -1700, -300, 0, 960, "atrium"),
    Room("F", "Flue", -2300, 300, 1000, 1400, 320, 400, "corridor"),
    Room("T", "Gallery tunnel", 1900, 2300, -300, 300, 320, 400, "corridor"),
]
RJ_CLEARANCE_MIN = 900.0

# ---------------------------------------------------------------- solid boxes
F = "Floor"


def _floor(i, x0, x1, y0, y1):
    return Box(i, "floor", F, x0, x1, y0, y1, -100, 0)


BOXES = [
    # L0 floor slabs: the whole interior minus the lava (pool X -2000..-1200 Y -300..500, gutter X -1700..-700 Y -1450..-1250,
    # trenches X 1000..1600 Y 700..900 and Y 1200..1400); vertical strips between the hole edges
    _floor("Ground_W", -2300, -2000, -1700, 1700),
    _floor("Ground_2a", -2000, -1700, -1700, -300), _floor("Ground_2b", -2000, -1700, 500, 1700),
    _floor("Ground_3a", -1700, -1200, -1700, -1450), _floor("Ground_3b", -1700, -1200, -1250, -300), _floor("Ground_3c", -1700, -1200, 500, 1700),
    _floor("Ground_4a", -1200, -700, -1700, -1450), _floor("Ground_4b", -1200, -700, -1250, 1700),
    _floor("Ground_5", -700, 1000, -1700, 1700),
    _floor("Ground_6a", 1000, 1600, -1700, 700), _floor("Ground_6b", 1000, 1600, 900, 1200), _floor("Ground_6c", 1000, 1600, 1400, 1700),
    _floor("Ground_7", 1600, 2300, -1700, 1700),
    _floor("RA_Island", -1750, -1450, -50, 250),                 # A.isle 300 x 300, 250 cm of lava on W, N and E
    _floor("Slag_Bridge", -1700, -1500, -300, -50),              # A.bridge 200 wide walkway from the south floor to the island
    # lava beds (kind hazard: BlockAll, no nav influence), one per Hazard piece, top at LAVA_BED_Z
    # (generated below from HAZARDS)
    # L1 platforms
    Box("Deck", "platform", "Platform", -2300, -900, 500, 900, 0, 320),               # Charging Deck, 4 m deep
    Box("Flue_Floor", "platform", "Platform", -2300, 300, 1000, 1400, 0, 320),         # Flue, 26 x 4 m
    Box("West_Landing", "platform", "Platform", 300, 900, 900, 1700, 0, 320),
    Box("Gallery", "platform", "Platform", 1900, 2300, -1700, 1700, 0, 320),           # East Gallery, 34 m long, 4 m deep
    # L2
    Box("Crane_Bridge", "bridge", "Platform", 900, 1900, 850, 1250, 580, 640),         # 10 x 4 m deck, underside 580
    # solid masses
    Box("Furnace", "wall", "Wall", -500, -100, -200, 200, 0, 640),                     # H.furnace, full height
    Box("Mass", "wall", "Wall", 200, 1900, -300, 300, 0, 1240),                        # between Casting Hall and Tap Room
    Box("Tunnel_Roof", "lintel", "Wall", 1900, 2300, -300, 300, 720, 1240),            # gallery tunnel: ceiling 400 over the gallery floor
    Box("Flue_Mouth", "lintel", "Wall", 200, 300, 1000, 1400, 720, 1240),              # Flue opens onto the landing; 400 clear
]

# ---------------------------------------------------------------- hazards (lava): pieces around the island, gutter, trenches
HAZARDS = [
    Hazard("Pool_W", "lava", -2000, -1750, -300, 500, LAVA_BED_Z, LAVA_SURFACE_Z),
    Hazard("Pool_E", "lava", -1450, -1200, -300, 500, LAVA_BED_Z, LAVA_SURFACE_Z),
    Hazard("Pool_N", "lava", -1750, -1450, 250, 500, LAVA_BED_Z, LAVA_SURFACE_Z),
    Hazard("Pool_SW", "lava", -1750, -1700, -300, -50, LAVA_BED_Z, LAVA_SURFACE_Z),
    Hazard("Pool_SE", "lava", -1500, -1450, -300, -50, LAVA_BED_Z, LAVA_SURFACE_Z),
    Hazard("Gutter", "lava", -1700, -700, -1450, -1250, LAVA_BED_Z, LAVA_SURFACE_Z),
    Hazard("Trench_T1", "lava", 1000, 1600, 700, 900, LAVA_BED_Z, LAVA_SURFACE_Z),
    Hazard("Trench_T2", "lava", 1000, 1600, 1200, 1400, LAVA_BED_Z, LAVA_SURFACE_Z),
]
BOXES += [Box("Bed_" + h.id, "hazard", "Pit", h.x0, h.x1, h.y0, h.y1, h.z0 - BED_THICK, h.z0) for h in HAZARDS]

# ---------------------------------------------------------------- walls (strips with openings)
O = Opening
DOOR_H = 300.0
WALLS = [
    # outer walls (heights by room; the corner columns belong to the west / east walls)
    Wall("Wall_W_Slag", "y", -1800, -1000, -2400, -2300, 400, ()),
    Wall("Wall_W_A", "y", -1000, 1000, -2400, -2300, 960, ()),
    Wall("Wall_W_F", "y", 1000, 1800, -2400, -2300, 720, ()),
    Wall("Wall_E_C", "y", -1800, -300, 2300, 2400, 960, ()),
    Wall("Wall_E_T", "y", -300, 300, 2300, 2400, 1240, ()),
    Wall("Wall_E_B", "y", 300, 1800, 2300, 2400, 1240, ()),
    Wall("Wall_S_Slag", "x", -2300, 200, -1800, -1700, 400, ()),
    Wall("Wall_S_C", "x", 200, 2300, -1800, -1700, 960, ()),
    Wall("Wall_N_F", "x", -2300, 300, 1400, 1800, 720, ()),                # solid north of the Flue
    Wall("Wall_N_B", "x", 300, 2300, 1700, 1800, 1240, ()),
    # Crucible: A | Hearth (door Y -200..200), north (two Flue openings at the deck, z 320 sill), south (door to the Slag Run)
    Wall("Wall_AH", "y", -1000, 1000, -900, -800, 960, (O(-200, 200, 0, DOOR_H),)),
    Wall("Wall_N_A", "x", -2300, -900, 900, 1000, 960, (O(-2200, -1800, 320, 320 + DOOR_H), O(-1400, -1000, 320, 320 + DOOR_H))),
    Wall("Wall_S_A", "x", -2300, -900, -1000, -900, 960, (O(-2200, -1800, 0, DOOR_H),)),
    # Hearth: north wall (the Chute passes through it, 300 wide, open to the Flue ceiling), south wall (door to the Slag Run)
    Wall("Wall_N_H", "x", -800, 200, 900, 1000, 720, (O(-800, -500, 0, 720),)),
    Wall("Wall_S_H", "x", -800, 200, -1000, -900, 640, (O(-500, -100, 0, DOOR_H),)),
    # east wall column X 200..300: Slag Run | Tap Room (open end Y -1600..-1200) and Hearth | Tap Room (door Y -800..-400)
    Wall("Wall_C_S", "y", -1700, -300, 200, 300, 960, (O(-1600, -1200, 0, DOOR_H), O(-800, -400, 0, DOOR_H))),
    # Hearth | Casting Hall (door Y 400..800); the Flue mouth (Y 1000..1400) is a free opening closed above by Flue_Mouth
    Wall("Wall_C_B", "y", 300, 900, 200, 300, 1240, (O(400, 800, 0, DOOR_H),)),
    Wall("Wall_C_L1", "y", 900, 1000, 200, 300, 1240, ()),
    Wall("Wall_C_L2", "y", 1400, 1700, 200, 300, 1240, ()),
]

STAIRS = [
    Stairs("A_Stairs", -300, +1, -2300, -2000, 16, "y"),     # deck stairs: 16 risers of 20 cm, treads 50, rise north (Y -300..500), east flank open to the pool
    Stairs("B_Stairs", 1700, -1, 1400, 1700, 16),            # north stairs: descend east from the landing (X 900..1700)
]

RAMPS = [
    Ramp("Chute", 446.0, -800, -500, 320.0, "+y", 30.0),                # H.chute: 30 deg, 554 cm run, rises north through the Hearth north wall into the Flue
    Ramp("Tap_Ramp", 705.8, -1700, -1400, 320.0, "+x"),                 # C.ramp: 15 deg, 1194 cm run, rises east to the gallery (top at X 1900)
]

# ---------------------------------------------------------------- jump pads (AimAtTarget: the target is the capsule-centre apex, ADR 0002)
PADS = [
    Pad("Bellows", 1400, 450, (1400, 900, 760), (1400, 1021, 640)),    # Pad 1: B south strip -> Crane Bridge, over trench T1
    Pad("Tap_Pad", 1400, -1000, (1900, -1000, 420), (2046, -1000, 320)),   # Pad 2: Tap Room -> East Gallery (the DM1 PadA shape)
]

# ---------------------------------------------------------------- items (concept section 5; 30 pickups)
Q = "/Game/Items/DA_Item_"
DUEL = "Velox.Mode.Duel"


def _it(i, name, x, y, z=0.0, dis=()):
    return Item(i, Q + name.replace(".", "_"), x, y, z, tuple(dis))


def ramp_z(x):
    """Tap ramp surface height at x."""
    return (x - 705.8) * math.tan(math.radians(RAMP_DEG))


ITEMS = [
    _it("RA", "Armor.Red", -1600, 100),
    _it("Mega", "Health.Mega", 1400, 1050, 640),
    _it("YA", "Armor.Yellow", 1500, -500),
    _it("RL", "Weapon.RocketLauncher", -300, -550),
    _it("LG", "Weapon.LightningGun", 900, 500),
    _it("RG", "Weapon.Railgun", 2100, 0, 320),
    _it("PG", "Weapon.PlasmaGun", 600, -1000),
    _it("SG", "Weapon.Shotgun", -2100, 1200, 320),
    _it("RocketsAmmo", "Ammo.Rockets", -900, -1125),
    _it("LightningAmmo", "Ammo.Lightning", 1750, 1000),
    _it("SlugsAmmo", "Ammo.Slugs", 2100, 900, 320),
    _it("CellsAmmo", "Ammo.Cells", 1700, -1200),
    _it("ShellsAmmo", "Ammo.Shells", -1100, 700, 320),
    _it("H50", "Health.Large", -2150, 700, 320),
    _it("H25_1", "Health.Medium", -1600, -700),
    _it("H25_2", "Health.Medium", 1300, 1050),
    _it("H25_3", "Health.Medium", 500, -450),
    _it("H25_4", "Health.Medium", 0, -1575),
    _it("H5_S1", "Health.Small", -1500, -1125),
    _it("H5_S2", "Health.Small", -1300, -1125),
    _it("H5_S3", "Health.Small", -1100, -1125),
    _it("H5_T1", "Health.Small", 1000, -1550, ramp_z(1000)),
    _it("H5_T2", "Health.Small", 1300, -1550, ramp_z(1300)),
    _it("H5_T3", "Health.Small", 1600, -1550, ramp_z(1600)),
    # shards on the stairs, at tread centres (the concept's x / y sit on tread edges): deck stairs treads 3, 8, 13; north stairs 4, 8, 12
    _it("Shard_D1", "Armor.Shard", -2150, -125, 80),
    _it("Shard_D2", "Armor.Shard", -2150, 125, 180),
    _it("Shard_D3", "Armor.Shard", -2150, 375, 280),
    _it("Shard_N1", "Armor.Shard", 1475, 1550, 100),
    _it("Shard_N2", "Armor.Shard", 1275, 1550, 180),
    _it("Shard_N3", "Armor.Shard", 1075, 1550, 260),
]
WEAPON_IDS = ("PG", "SG", "LG", "RL", "RG")
MEGA_ID, RA_ID = "Mega", "RA"
MAJOR_IDS = ("RA", "Mega")
AMMO_FOR = {"PG": "CellsAmmo", "SG": "ShellsAmmo", "LG": "LightningAmmo", "RL": "RocketsAmmo", "RG": "SlugsAmmo"}
AMMO_MIN_CM, AMMO_MAX_CM = 600.0, 1200.0

# ---------------------------------------------------------------- spawns (5): none sees RA or Mega, >= 10 m apart, <= 8 m (horizontal) from a weapon
# Spawn has no z (the generator places every PlayerStart at FLOOR_Z + 100), so the concept's S1 (Flue, L1) and S5 (gallery, L1) are moved
# to L0 positions with the same weapon / no-sight properties (task notes). With a `Spawn.z` field they could go back.
SPAWNS = [
    Spawn("S1_Slag", -800, -1100, 0),        # Slag Run north bank, RL (7.4 m)
    Spawn("S2_Hearth", 150, -750, 180),      # Hearth, RL (4.9 m)
    Spawn("S3_Under", 500, 500, 0),          # Casting Hall south strip under the bridge line, LG (4 m)
    Spawn("S4_Tap", 1000, -1300, 0),         # Tap Room, PG (5 m)
    Spawn("S5_TapE", 1750, -600, 180),       # Tap Room east, RG (7 m)
]
SPAWN_MIN_APART = 1000.0
SPAWN_MAX_WEAPON = 800.0
EYE_Z = 160.0

LOS_BLOCKED = [("spawns", "majors"), ("FlueMid", "RA")]
LOS_CLEAR = [("DeckC", "RA"), ("FlueW", "LandingE"), ("GalS", "GalN"), ("SlagW", "SlagE")]
POINTS = {
    "FlueMid": (-1700, 1200, 320), "FlueW": (-2200, 1200, 320), "LandingE": (850, 1200, 320),     # Flue 32 m clear line to the landing edge
    "DeckC": (-1600, 700, 320), "GalS": (2100, -1600, 320), "GalN": (2100, 1600, 320),             # gallery 34 m
    "SlagW": (-2200, -1600, 0), "SlagE": (100, -1600, 0),                                          # Slag Run south bank 23 m
}

# ---------------------------------------------------------------- nav links (ADR 0005 section 4; concept section 13)
DROP_IN, DROP_OUT_TALL = 60.0, 350.0


def _drop(i, sx, sy, sz, ex, ey, ez):
    return Link(i, "DROP", (sx, sy, sz), (ex, ey, ez), None)


def _hop(i, sx, sy, ex, ey, kind="JUMP"):
    return Link(i, kind, (sx, sy, 0.0), (ex, ey, 0.0), None)


LINKS = [
    Link("PadLink_Bellows", "JUMP_PAD", (PADS[0].x, PADS[0].y, 0), PADS[0].land, "Bellows"),
    Link("PadLink_Tap", "JUMP_PAD", (PADS[1].x, PADS[1].y, 0), PADS[1].land, "Tap_Pad"),
    Link("RJ_West", "ROCKET_JUMP", (350, 1050, 320), (1000, 1050, 640), None),         # West Landing -> Crane Bridge, run-up 550 cm
    # lava hops (start 60 before the take-off edge, end 100 onto the landing): both directions; 250 cm of lava on the island sides
    _hop("Isle_In", -1140, 100, -1550, 100), _hop("Isle_Out", -1510, 100, -1100, 100),
    # the concept's second island hop (from the stairs flank) is dropped: a link start on a 50 cm tread has a lower tread inside the 45 cm agent ring
    _hop("Gutter1_N2S", -1600, -1190, -1600, -1550), _hop("Gutter1_S2N", -1600, -1510, -1600, -1150),
    _hop("Gutter2_N2S", -1200, -1190, -1200, -1550), _hop("Gutter2_S2N", -1200, -1510, -1200, -1150),
    _hop("Gutter3_N2S", -800, -1190, -800, -1550), _hop("Gutter3_S2N", -800, -1510, -800, -1150),
    _hop("T1a_N", 1100, 640, 1100, 1000), _hop("T1a_S", 1100, 960, 1100, 600),
    _hop("T1b_N", 1500, 640, 1500, 1000), _hop("T1b_S", 1500, 960, 1500, 600),
    # drops that meet ADR 0006 E3 (d); candidates dropped for not meeting it: deck east end (strip 300 wide beside a wall), landing south edge
    # (mass 250 cm ahead), bridge east end (outer wall 200 cm ahead). No drop from the deck onto the island and none from the bridge's long edges.
    _drop("Drop_Landing_E", 900 - DROP_IN, 1050, 320, 900 + DROP_OUT_TALL, 1050, 0),
    _drop("Drop_Bridge_W", 900 + DROP_IN, 1050, 640, 900 - DROP_OUT_TALL, 1050, 320),
    _drop("Drop_Gal_B", 1900 + DROP_IN, 1050, 320, 1900 - DROP_OUT_TALL, 1050, 0),
    _drop("Drop_Gal_C1", 1900 + DROP_IN, -700, 320, 1900 - DROP_OUT_TALL, -700, 0),
    _drop("Drop_Gal_C2", 1900 + DROP_IN, -1240, 320, 1900 - 300, -1240, 0),
]

# ---------------------------------------------------------------- lights (art mode; a later art task fills them from the concept section 10)
LIGHTS = []
CAPTURES = ()
LIGHTMASS_VOLUME = ((0.0, 0.0, 650.0), (5000.0, 3800.0, 1500.0))

# ---------------------------------------------------------------- clip: perimeter flush outside the walls, global roof, a ceiling per room
CLIPS = [
    Clip("Clip_W", -2500, -2400, -1900, 1900, 0, 1800),
    Clip("Clip_E", 2400, 2500, -1900, 1900, 0, 1800),
    Clip("Clip_S", -2500, 2500, -1900, -1800, 0, 1800),
    Clip("Clip_N", -2500, 2500, 1800, 1900, 0, 1800),
    Clip("Clip_Roof", -2500, 2500, -1900, 1900, 1800, 1900),
    Clip("Ceil_A", -2400, -800, -1000, 1000, 960, 1060),
    Clip("Ceil_H", -800, 200, -1000, 1000, 640, 740),
    Clip("Ceil_Slag", -2400, 200, -1800, -1000, 400, 500),
    Clip("Ceil_F", -2400, 300, 1000, 1800, 720, 820),
    Clip("Ceil_B", 200, 2400, 300, 1800, 1240, 1340),
    Clip("Ceil_Mass", 200, 2400, -300, 300, 1240, 1340),
    Clip("Ceil_C", 200, 2400, -1800, -300, 960, 1060),
]

NAV_CENTER = (0.0, 0.0, 600.0)
NAV_SIZE = (5000.0, 3800.0, 1800.0)      # X -2500..2500, Y -1900..1900, Z -300..1500

INTERMISSION_CAMERA = ((0.0, 0.0, 2600.0), (0.0, -60.0, 90.0))   # location, (roll, pitch, yaw)
INTERMISSION_LABEL = (0.0, 300.0, 2250.0)

# ---------------------------------------------------------------- rules (docs/maps/fornax.md; every override cites its section)
RULES = {
    "pad_count": 2,                                  # section 7: Pad 1 (Bellows), Pad 2 (Tap)
    "link_counts": {"JUMP_PAD": 2, "ROCKET_JUMP": 1, "JUMP": 12, "DROP": 5},      # sections 7 and 13 (see the notes for the deviations)
    "item_mix": {"Armor_Red": 1, "Health_Mega": 1, "Armor_Yellow": 1, "Weapon_RocketLauncher": 1, "Weapon_LightningGun": 1,
                 "Weapon_Railgun": 1, "Weapon_PlasmaGun": 1, "Weapon_Shotgun": 1, "Ammo_Rockets": 1, "Ammo_Lightning": 1,
                 "Ammo_Slugs": 1, "Ammo_Cells": 1, "Ammo_Shells": 1, "Health_Large": 1, "Health_Medium": 4, "Health_Small": 6,
                 "Armor_Shard": 6},                  # section 5: 30 pickups, no powerup
}

# Per-kind actor counts that do not depend on the geometry expansion (the geometry kinds are counted by the generator).
EXPECTED = {
    "pickup": 30, "playerstart": 5, "jumppad": 2, "targetpoint": 2, "navbounds": 1, "navlink": 20,
    "camera": 1, "text": 1, "light": 2,
}
