"""Culmen: the layout table (ADR 0008 sections 1 and 4, concept docs/maps/culmen.md, task M7 S3). Data only: no `unreal` import.

Centimetres, X east, Y north, lowest walkable floor top at Z = 0, 100 cm grid. Footprint 76 x 48 m (X -3800..3800, Y -2400..2400; the
concept's north edge at Y 2200 is a solid skyline wall Y 2200..2400 so the footprint is symmetric about the origin and the perimeter clip
is flush). Three layers: L0 street 0, L1 decks 320, L2 roofs 640. No void, no hazard. Point symmetric (180 deg about the origin) except the
Porticus pair, the Car and the Viaduct (concept section 4).

Top-down sketch (north up):

  y 2400 +=================== Wall_N (skyline, 1000) ====================================================+
         |  Trench L0 (y 1400..2200, 7040 x 400 strike lane south strip y 1400..1800)   Car L1 (RG) x +-600  |
  y 1400 +--RampBaseNW---tow--Porticus N (2 gates)---------------------------------------------tow-EndTerrNE-+
         | RoofRampNW  TerraceW  Clivus  Forum  Spire(deck 320, core)  Forum  Clivus  TerraceE  RoofRampSE   |
         | West Roof (RA)  640    BrW ... Spire ... BrE                      East Roof (Mega) 640            |
  y-1400 +--EndTerrSW---------Porticus S (gate x +-200)---------------------------------RampBaseSE----------+
         |  Concourse L0 (y -1900..-1400, kiosks, BrSW / BrSE)                                              |
  y-1900 +--Viaduct L1 (x +-3000, y -2400..-1900, LG / Haste / YA)--------------------------------------+
  y-2400

Pads: eleven. The five on raised floors (P3 P4 P5 P6 P8) carry their floor in `Pad.z` (M7 P6; `PAD_FLOOR_Z` below). Spawns: ten as in the
concept, S8 S9 S10 on L1 (`Spawn.z`).
"""
import math

MAP_ID = "CULMEN"
SLUG = "culmen"
DISPLAY_NAME = "Culmen"
LOG_PREFIX = "[VeloxCulmen]"
MAP_PATH = "/Game/Maps/Culmen/L_Culmen_Blockout"
ART_MAP_PATH = "/Game/Maps/Culmen/L_Culmen"
THEME = "culmen"
FLOOR_Z = 0.0
KILL_Z = -1000.0                 # safety net only: nothing in the arena is lethal (concept section 16)
LEVEL_STEP = 320.0
GRID = 100.0
MODULE = 400.0
RAMP_DEG = 20.0                  # concept section 5: the four 20 deg ramps (every Ramp record also states deg=20)
RISER = 20.0
TREAD = 50.0
WALL_T = 100.0
CLIP_H = 1800

from maps.records import Room, Box, Opening, Wall, Stairs, Ramp, Pad, Item, Spawn, Link, Light, Clip  # noqa: E402,F401

FILL_NAV_KINDS = ("floor", "wall", "platform", "step")
FLOATING_KINDS = ("ramp", "bridge", "bridgewall", "lintel")

# Rules this map overrides or declares (docs/maps/culmen.md section references)
RULES = {
    "pad_arc_clearance": 60.0,                                        # section 17.1: every pad arc clears every solid by >= 60 cm
    "pad_count": 11,                                                  # six ground pads + five raised (PADS_RAISED)
    "link_counts": {"JUMP_PAD": 11, "ROCKET_JUMP": 4, "JUMP_FAST": 6, "DROP": 21},  # sections 8, 15 (JUMP_FAST: G1 both ways, three spots)
    "item_mix": {"Health_Mega": 1, "Armor_Red": 1, "Armor_Yellow": 2, "Armor_Shard": 8, "Health_Large": 2, "Health_Medium": 7,
                 "Health_Small": 8, "Weapon_RocketLauncher": 1, "Weapon_Railgun": 1, "Weapon_LightningGun": 1, "Weapon_Shotgun": 2,
                 "Weapon_PlasmaGun": 2, "Ammo_Rockets": 1, "Ammo_Slugs": 1, "Ammo_Lightning": 1, "Ammo_Shells": 2, "Ammo_Cells": 2,
                 "Ammo_Bullets": 1, "Powerup_Quad": 1, "Powerup_Haste": 1},                  # section 6: 46 pickups
    "free_edge_report": True,                                         # ADR 0008 section 1.4: Culmen reads the free-edge figure
}

ROOMS = []           # open sky: no room has a ceiling (section 3)
RJ_CLEARANCE_MIN = 900.0

# ---------------------------------------------------------------- solids
O = Opening


def _mir(b):
    """Point mirror (x, y -> -x, -y) of a Box; the id's W/E and N/S letters are swapped by the caller."""
    return b._replace(x0=-b.x1, x1=-b.x0, y0=-b.y1, y1=-b.y0)


def _pair(b, wid, eid):
    return [b._replace(id=wid), _mir(b)._replace(id=eid)]


BOXES = (
    [Box("Ground", "floor", "Floor", -3800, 3800, -2400, 2400, -100, 0)]
    # the two roofs (Tower), the roof-ramp heads (L2) and the sealed bases under the roof ramps (L1) with the End Terraces
    + _pair(Box("x", "platform", "Platform", -3800, -2600, -800, 800, 0, 640), "TowerW", "TowerE")
    + _pair(Box("x", "platform", "Platform", -3800, -3480, 800, 1400, 0, 640), "RampHeadNW", "RampHeadSE")
    + _pair(Box("x", "platform", "Platform", -3480, -2600, 800, 1400, 0, 320), "RampBaseNW", "RampBaseSE")
    + _pair(Box("x", "platform", "Platform", -3800, -2600, -1400, -800, 0, 320), "EndTerrSW", "EndTerrNE")
    # terraces (solid podium), the Spire deck, the Clivus head landings (carry the bridge heads)
    + _pair(Box("x", "platform", "Platform", -2600, -2000, -1400, 1400, 0, 320), "TerraceW", "TerraceE")
    + [Box("Spire", "platform", "Platform", -600, 600, -600, 600, 0, 320),
       Box("Spire_Core", "wall", "Wall", -100, 100, -100, 100, 320, 1000)]
    + _pair(Box("x", "platform", "Platform", -2000, -1700, -221, 221, 0, 320), "LandW", "LandE")
    # glass bridges: opaque deck 40 thick, 280 clear below. BrW / BrE: Terrace landings to the Spire; BrSW / BrSE: terrace south end to Viaduct
    + _pair(Box("x", "bridge", "Platform", -1700, -600, -150, 150, 280, 320), "BrW", "BrE")
    + [Box("BrSW", "bridge", "Platform", -2500, -2200, -1900, -1400, 280, 320),
       Box("BrSE", "bridge", "Platform", 2200, 2500, -1900, -1400, 280, 320)]   # B3: X mirror (the Viaduct is not point-mirrored); the point mirror spanned the Trench
    # 110 cm parapet stubs on the long sides of BrW / BrE (they float on the deck)
    + _pair(Box("x", "bridgewall", "Wall", -1700, -600, 150, 170, 320, 430), "BrW_ParN", "BrE_ParS")
    + _pair(Box("x", "bridgewall", "Wall", -1700, -600, -170, -150, 320, 430), "BrW_ParS", "BrE_ParN")
    # the elevated second lane and the parked maglev car
    + [Box("Viaduct", "platform", "Platform", -3000, 3000, -2400, -1900, 0, 320),
       Box("Car", "platform", "Platform", -600, 600, 1800, 2200, 0, 320),
       Box("Wall_N", "wall", "Wall", -3800, 3800, 2200, 2400, 0, 1000)]       # skyline wall: keeps the footprint symmetric, nothing climbs it
    # concourse kiosks (cover): blockout boxes, the 50 deg ridge cap of the art pass makes them unwalkable (section 4)
    + [Box("Kiosk1", "wall", "Wall", -2000, -1800, -1900, -1700, 0, 250),
       Box("Kiosk2", "wall", "Wall", -1200, -1000, -1600, -1400, 0, 250),
       Box("Kiosk3", "wall", "Wall", 1000, 1200, -1900, -1700, 0, 250),
       Box("Kiosk4", "wall", "Wall", 1800, 2000, -1600, -1400, 0, 250)]
    # roofs: one holo-billboard wall across the middle (20 thick, 300 high) and the 120 cm glass parapet on the Forum edge with 400 gaps on the pad lines
    + _pair(Box("x", "wall", "Wall", -3600, -2800, -10, 10, 640, 940), "Bill_W", "Bill_E")
    + _pair(Box("x", "wall", "Wall", -2620, -2600, -800, -650, 640, 760), "ParRoof_W1", "ParRoof_E1")
    + _pair(Box("x", "wall", "Wall", -2620, -2600, -250, 350, 640, 760), "ParRoof_W2", "ParRoof_E2")
    + _pair(Box("x", "wall", "Wall", -2620, -2600, 750, 800, 640, 760), "ParRoof_W3", "ParRoof_E3")
)

WALLS = [
    # Porticus: 360 + ridge to 480 (flat 480 in the blockout), gates 400 wide x 320 high (section 4)
    Wall("PorticusN", "x", -2000, 2000, 1200, 1400, 480, (O(-1600, -1200, 0, 320), O(1200, 1600, 0, 320))),
    Wall("PorticusS", "x", -2000, 2000, -1400, -1200, 480, (O(-200, 200, 0, 320),)),
]

STAIRS = []

RAMPS = [
    # roof ramps 20 deg, 600 wide, sealed fill below: from the terrace (z 320) up to the roof head (z 640)
    Ramp("RoofRampNW", -2600, 800, 1400, 320.0, "-x", 20.0, 320.0, "solid"),
    Ramp("RoofRampSE", 2600, -1400, -800, 320.0, "+x", 20.0, 320.0, "solid"),
    # Clivus: Forum ramps to the terraces, 300 wide
    Ramp("ClivusSW", -1100, -2000, -1700, 320.0, "+y", 20.0, 0.0, "solid"),
    Ramp("ClivusNE", 1100, 1700, 2000, 320.0, "-y", 20.0, 0.0, "solid"),
]

# ---------------------------------------------------------------- jump pads (AimAtTarget: the target is the apex of the origin, ADR 0002)
# Ground pads. Concept section 8 values; P10 / P11 apex raised for the 60 cm arc clearance, see the task notes.
PADS = [
    Pad("P1", -3640, 1700, (-3640, 1245, 972.1), (-3640, 1000, 640)),      # Trench west -> West Roof ramp head
    Pad("P2", 3640, -1700, (3640, -1245, 972.1), (3640, -1000, 640)),      # Concourse east -> East Roof ramp head
    Pad("P7", 1100, 2000, (596, 2000, 490.5), (350, 2000, 320)),           # Trench -> car roof (RG)
    Pad("P9", 0, -1650, (0, -1986, 490.5), (0, -2150, 320)),               # Concourse -> Viaduct centre (Haste)
    Pad("P10", -3400, -1650, (-3266, -1247, 534.6), (-3200, -1050, 320)),  # west pocket -> End Terrace SW (PG-W)
    Pad("P11", 3400, 1650, (3266, 1247, 534.6), (3200, 1050, 320)),        # Trench east -> End Terrace NE (SG-E)
]

# Raised pads: floor Z in PAD_FLOOR_Z (= Pad.z); they join PADS below.
PAD_FLOOR_Z = {"P3": 320.0, "P4": 320.0, "P5": 640.0, "P6": 640.0, "P8": 320.0}
PADS_RAISED = [
    Pad("P3", -350, -350, (-1924, -412, 911.1), (-2900, -450, 640)),       # Spire -> West Roof
    Pad("P4", 350, 350, (1924, 412, 911.1), (2900, 450, 640)),             # Spire -> East Roof
    Pad("P5", -2750, 550, (-1708.0, 463.2, 1170.8), (-350, 350, 320)),           # West Roof -> Spire NW
    Pad("P6", 2750, -550, (1708.0, -463.2, 1170.8), (350, -350, 320)),           # East Roof -> Spire SE
    Pad("P8", -350, 2000, (-1325, 1550, 930.5), (-2300, 1100, 320)),       # car roof -> Terrace W
]
PADS = sorted(PADS + [p._replace(z=PAD_FLOOR_Z[p.id]) for p in PADS_RAISED], key=lambda p: int(p.id[1:]))

# ---------------------------------------------------------------- items
Q = "/Game/Items/DA_Item_"
DUEL = "Velox.Mode.Duel"


def _it(i, name, x, y, z=0.0, dis=()):
    return Item(i, Q + name.replace(".", "_"), x, y, z, tuple(dis))


def ramp_z(r, x, y):
    f = None
    from math import tan, radians
    s = (x - r.x0) * (1.0 if r.dir == "+x" else -1.0)
    return r.z0 + s * tan(radians(r.deg))


_RNW, _RSE = RAMPS[0], RAMPS[1]
# B3 item iteration (FFA x10 at 3: never taken): NW_Shard_2..4 off the NW roof ramp onto the P1 landing -> RA line (west of Bill_W);
# T_H5_4 (2600, 1600) -> (2000, 1600), next to the Drop_TE_Trench landing. The SE shards stay on their ramp (taken).
ITEMS = [
    _it("RA", "Armor.Red", -3300, -350, 640),
    _it("MH", "Health.Mega", 3300, 350, 640),
    _it("YA_N", "Armor.Yellow", -1800, 1800),
    _it("YA_S", "Armor.Yellow", 1800, -2150, 320),
    _it("Quad", "Powerup.Quad", 0, 900, 0, (DUEL,)),
    _it("Haste", "Powerup.Haste", 0, -2150, 320, (DUEL,)),
    _it("RL", "Weapon.RocketLauncher", 0, -900),
    _it("RG", "Weapon.Railgun", 0, 2000, 320),
    _it("LG", "Weapon.LightningGun", -1800, -2150, 320),
    _it("PG_W", "Weapon.PlasmaGun", -3200, -1100, 320),
    _it("PG_E", "Weapon.PlasmaGun", 2600, -1650),
    _it("SG_E", "Weapon.Shotgun", 3200, 1100, 320),
    _it("SG_W", "Weapon.Shotgun", -2600, 2000),
    _it("RocketsAmmo", "Ammo.Rockets", 900, -900),
    _it("SlugsAmmo", "Ammo.Slugs", -1000, 1600),
    _it("LightningAmmo", "Ammo.Lightning", -900, -2150, 320),
    _it("CellsAmmo_W", "Ammo.Cells", -2300, -1000, 320),
    _it("CellsAmmo_E", "Ammo.Cells", 1700, -1750),
    _it("ShellsAmmo_E", "Ammo.Shells", 2300, 1000, 320),
    _it("ShellsAmmo_W", "Ammo.Shells", -3400, 2000),
    _it("BulletsAmmo", "Ammo.Bullets", 1500, 900),
    _it("H50_W", "Health.Large", -3400, -2100),
    _it("H50_E", "Health.Large", 3600, 2000),
    _it("H25_1", "Health.Medium", -1300, 400),
    _it("H25_2", "Health.Medium", 1300, -400),
    _it("H25_3", "Health.Medium", -3000, 1600),
    _it("H25_4", "Health.Medium", -2800, -1650),
    _it("H25_5", "Health.Medium", -2300, -600, 320),
    _it("H25_6", "Health.Medium", 2300, 600, 320),
    _it("H25_7", "Health.Medium", 0, -400, 320),
] + [_it(f"T_H5_{i + 1}", "Health.Small", x, 1600) for i, x in enumerate((1400, 1800, 2200, 2000))] \
  + [_it(f"C_H5_{i + 1}", "Health.Small", x, -1650) for i, x in enumerate((300, 500, 700, 900))] \
  + [_it("NW_Shard_1", "Armor.Shard", -2800, 1100, ramp_z(_RNW, -2800, 1100))] \
  + [_it(f"NW_Shard_{i + 2}", "Armor.Shard", x, y, 640) for i, (x, y) in enumerate(((-3650, 600), (-3700, 0), (-3550, -300)))] \
  + [_it(f"SE_Shard_{i + 1}", "Armor.Shard", x, -1100, ramp_z(_RSE, x, -1100)) for i, x in enumerate((2800, 3000, 3200, 3400))]

WEAPON_IDS = ("PG_W", "PG_E", "SG_E", "SG_W", "RL", "RG", "LG")
MEGA_ID, RA_ID = "MH", "RA"
AMMO_FOR = {"PG_W": "CellsAmmo_W", "PG_E": "CellsAmmo_E", "SG_E": "ShellsAmmo_E", "SG_W": "ShellsAmmo_W", "RL": "RocketsAmmo",
            "RG": "SlugsAmmo", "LG": "LightningAmmo"}
AMMO_MIN_CM, AMMO_MAX_CM = 600.0, 1200.0

# ---------------------------------------------------------------- spawns: S1-S7 on L0, S8-S10 on L1 (z 320), >= 10 m apart, <= 8 m from a weapon
SPAWNS = [
    Spawn("S1", -2000, 1600, 0),      # east, down the lane (SG-W)
    Spawn("S2", -600, 1600, 0),       # RG
    Spawn("S3", 600, 1600, 180),      # RG
    Spawn("S4", 3000, 1850, 180),     # SG-E
    Spawn("S5", 700, -1050, 135),     # RL
    Spawn("S6", -1400, -1650, 0),     # LG
    Spawn("S7", 3000, -1750, 180),    # PG-E
    Spawn("S8", -3300, -1250, 0, 320.0),    # End Terrace SW, east (PG-W 1.8 m)
    Spawn("S9", -2500, -2000, 0, 320.0),    # Viaduct, east (concept y -2150 sees RA over the open sky; moved 150 north; LG 7.2 m)
    Spawn("S10", 2500, 900, 180, 320.0),    # Terrace E north (B3: P6 spot (2000, -1250) sat on the terrace edge, nav could not reach it; every other Terrace E point sees RA or is < 10 m from S7; SG-E)
]
SPAWN_MIN_APART = 1000.0
SPAWN_MAX_WEAPON = 800.0
EYE_Z = 160.0

# ---------------------------------------------------------------- explicit sightline rules (section 10)
MAJOR_IDS = ("RA", "MH")
POINTS = {
    "TrenchEnds": [(-3400, 1600), (3400, 1600)],
    "ViaductEnds": [(-2800, -2150, 320), (2800, -2150, 320)],
    "BridgeEnds": [(-1900, 0, 320), (1900, 0, 320)],
}
LOS_BLOCKED = [("spawns", "majors"), ("BridgeEnds", "BridgeEnds")]     # no spawn sees RA / Mega; the Spire core blocks BrW - BrE
LOS_CLEAR = [("TrenchEnds", "TrenchEnds"), ("ViaductEnds", "ViaductEnds")]   # the two lanes are open end to end (76 m, 56 m)

# ---------------------------------------------------------------- nav links
DROP_IN, DROP_OUT, DROP_OUT_TALL = 60.0, 150.0, 350.0


def _drop(i, sx, sy, sz, ex, ey, ez):
    return Link(i, "DROP", (sx, sy, sz), (ex, ey, ez), None)


def _jf(i, s, e):
    return Link(i, "JUMP_FAST", s, e, None)


LINKS = [Link(f"PadLink_{p.id}", "JUMP_PAD", (p.x, p.y, p.z or 0.0), p.land, p.id) for p in PADS] + [
    # rocket jumps: run-up 450 cm from the wall, end 100 cm onto the target (RJ1..RJ4; RJ5 / RJ6 have no link)
    Link("RJ1", "ROCKET_JUMP", (-3200, -1250, 320), (-3200, -700, 640), None),
    Link("RJ2", "ROCKET_JUMP", (3200, 1250, 320), (3200, 700, 640), None),
    Link("RJ3", "ROCKET_JUMP", (-1050, 2000, 0), (-500, 2000, 320), None),
    Link("RJ4", "ROCKET_JUMP", (0, 1050, 0), (0, 500, 320), None),
    # G1: the 500 cm gap Terrace / End Terrace SW <-> Viaduct across the Concourse, both ways
    _jf("Gap_TW_V", (-2100, -1340, 320), (-2100, -1960, 320)), _jf("Gap_V_TW", (-2100, -1960, 320), (-2100, -1340, 320)),
    _jf("Gap_TE_V", (2100, -1340, 320), (2100, -1960, 320)), _jf("Gap_V_TE", (2100, -1960, 320), (2100, -1340, 320)),
    _jf("Gap_ET_V", (-2850, -1340, 320), (-2850, -1960, 320)), _jf("Gap_V_ET", (-2850, -1960, 320), (-2850, -1340, 320)),
] + [
    # drops: start 60 cm inside the free edge, end 250..350 cm outside it, E3 (d) holds for every one (300 x 300 clear top, 500 cm to the
    # wall ahead, 300 cm from any pad). Terrace inner edges into the Forum (3 per side, north of the Clivus landing)
    _drop(f"Drop_TW_Forum_{y}", -2000 - DROP_IN, y, 320, -2000 + DROP_OUT_TALL, y, 0) for y in (400, 700, 1000)
] + [
    _drop(f"Drop_TE_Forum_{-y}", 2000 + DROP_IN, -y, 320, 2000 - DROP_OUT_TALL, -y, 0) for y in (400, 700, 1000)
] + [
    # terrace north ends into the Trench (250 out: the Trench wall is 550 cm further)
    _drop("Drop_TW_Trench", -2300, 1400 - DROP_IN, 320, -2300, 1650, 0),
    _drop("Drop_TE_Trench", 2300, 1400 - DROP_IN, 320, 2300, 1650, 0),
    # Spire Deck east / west edges into the Forum plaza (the N / S edges face the Porticus 600 cm away: E3 forbids them; the bridge heads are at y +-150)
    _drop("Drop_Spire_NW", -600 + DROP_IN, 400, 320, -600 - DROP_OUT_TALL, 400, 0),
    _drop("Drop_Spire_SW", -600 + DROP_IN, -400, 320, -600 - DROP_OUT_TALL, -400, 0),
    _drop("Drop_Spire_NE", 600 - DROP_IN, 400, 320, 600 + DROP_OUT_TALL, 400, 0),
    _drop("Drop_Spire_SE", 600 - DROP_IN, -400, 320, 600 + DROP_OUT_TALL, -400, 0),
    # car roof west end into the Trench north half (east end: P7 is 150 cm from the landing; south edge: Porticus 250 cm ahead)
    _drop("Drop_Car_W", -600 + DROP_IN, 2000, 320, -600 - DROP_OUT_TALL, 2000, 0),
    # roofs onto the terraces through the parapet gaps (pad lines y -450 / 550 West, mirrored East) and Tower faces onto the End Terraces
    _drop("Drop_RoofW_S", -2600 - DROP_IN, -450, 640, -2600 + DROP_OUT_TALL, -450, 320),
    _drop("Drop_RoofW_N", -2600 - DROP_IN, 550, 640, -2600 + DROP_OUT_TALL, 550, 320),
    _drop("Drop_RoofE_N", 2600 + DROP_IN, 450, 640, 2600 - DROP_OUT_TALL, 450, 320),
    _drop("Drop_RoofE_S", 2600 + DROP_IN, -550, 640, 2600 - DROP_OUT_TALL, -550, 320),
    _drop("Drop_TowerW_S", -3000, -800 + DROP_IN, 640, -3000, -800 - DROP_OUT_TALL, 320),
    _drop("Drop_TowerE_N", 3000, 800 - DROP_IN, 640, 3000, 800 + DROP_OUT_TALL, 320),
    # Viaduct ends into the pockets (the north edge faces the Porticus S gate line: no E3 end but at the gate, where P9 stands)
    _drop("Drop_Via_W", -3000 + DROP_IN, -2150, 320, -3000 - DROP_OUT_TALL, -2150, 0),
    _drop("Drop_Via_E", 3000 - DROP_IN, -2150, 320, 3000 + DROP_OUT_TALL, -2150, 0),
]

# ---------------------------------------------------------------- lights (art mode; filled by the art task A3)
LIGHTS = []
CAPTURES = ()
LIGHTMASS_VOLUME = ((0.0, 0.0, 600.0), (8000.0, 5200.0, 1800.0))

# ---------------------------------------------------------------- clip: perimeter flush outside the footprint (open sky: no lid)
CLIPS = [
    Clip("Clip_W", -3900, -3800, -2500, 2500, 0, 1800),
    Clip("Clip_E", 3800, 3900, -2500, 2500, 0, 1800),
    Clip("Clip_S", -3900, 3900, -2500, -2400, 0, 1800),
    Clip("Clip_N", -3900, 3900, 2400, 2500, 0, 1800),
]

NAV_CENTER = (0.0, 0.0, 500.0)
NAV_SIZE = (7800.0, 5000.0, 1200.0)      # X -3900..3900, Y -2500..2500, Z -100..1100 (concept section 15)
NAV_TILE_SIZE_UU = 4000.0                # ADR 0008 1.2 (H12): pad arcs P3-P6 / P8 span 2 tiles at 1000; Detour links neighbour tiles only

INTERMISSION_CAMERA = ((0.0, 0.0, 3000.0), (0.0, -60.0, 90.0))
INTERMISSION_LABEL = (0.0, 300.0, 2500.0)

EXPECTED = {
    "pickup": 46, "playerstart": 10, "jumppad": 11, "targetpoint": 11, "navbounds": 1, "navlink": 42,
    "camera": 1, "text": 1, "light": 2,
}
