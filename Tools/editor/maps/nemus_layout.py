"""Nemus: the layout table (M7 task S2; concept docs/maps/nemus.md, ADR 0008 sections 1.2, 1.3, 2.1, 4). Single source of truth for
`create_map.py --map nemus`. Data only: no `unreal` import; the helper functions are pure arithmetic over the records.

Centimetres, X east, Y north, 100 cm grid, 400 cm module. Footprint 68 x 56 m (X -3400..3400, Y -2800..2800), perimeter tree wall 100 cm
inside it (X +-3300..3400, Y +-2700..2800). FOUR height layers, level step 320 as on DM1:

  PIT  -320  Orchestra, the Cavea's 8 x 8 m floor            (concept Z 0)
  T1..T3 -240 / -160 / -80   the three seat tiers, 200 cm deep each (concept 80 / 160 / 240)
  F      0   L1 forest floor: lanes, groves, forecourt, rim  (concept 320)   <- FLOOR_Z
  U    320   L2 ruin ring: Templum, aqueduct, log, Pons, Tabulatum (concept 640)
  C    640   L3 Corona (concept 960)

DEVIATION D1 (datum): the concept puts the pit at Z 0 and the forest floor at 320. The generator takes ONE ground height for all spawns
(PlayerStart at FLOOR_Z + 100), all pads (veloxmap.jump_pad spawns at absolute Z 70, mapgen.pad_arc starts at FLOOR_Z) and the spawn line of
sight. Nemus has 11 spawns and four pads on the forest floor, so FLOOR_Z is the forest floor and every concept Z is lowered by 320 (the
pit goes to -320, like Orbita's lowest deck). Nothing else changes: all heights relative to each other are the concept's.

Top-down sketch (north up; see docs/maps/nemus.md section 4 for the full drawing):

  y 2700 +--------------------------- Via Sacra (L1, y 2000..2700) -------------------------------+
         |                       Aquaeductus deck (U, y 1600..2000, arches, 700 gap at x 700..1400) |
  y 1600 | Templum  | forecourt |          C A V E A  (+-1000)            | NE grove | Tabulatum    |
         | (U)      |  RL       |  tiers -240/-160/-80, Orchestra -320    | Pons N   | + Corona (C) |
         | x -3300..|           |  pit stairs N/S, pad P1 on the west rim | Radix    | trunk Oct    |
  y-1600 |  -2500   |           |  Radix Magna (20 deg) east to Tabulatum | Pons S   |              |
         |                      Trabs, the log (U, y -2000..-1600, breaks at x -2500..-2100 and -200..200)
  y-2700 +--------------------------- Rivus (L1, y -2700..-2000, three root humps) ----------------+
"""
import math

MAP_ID = "NEMUS"
SLUG = "nemus"
DISPLAY_NAME = "Nemus"
MAP_PATH = "/Game/Maps/Nemus/L_Nemus_Blockout"
ART_MAP_PATH = "/Game/Maps/Nemus/L_Nemus"
LOG_PREFIX = "[VeloxNemus]"
FLOOR_Z = 0.0                    # the forest floor (D1): spawns, pads and the spawn line of sight are measured from it
KILL_Z = -1000.0                 # safety net only; the lowest floor is the pit at -320
LEVEL_STEP = 320.0
GRID = 100.0
MODULE = 400.0
RAMP_DEG = 20.0                  # Radix Magna / Radix Australis; the Rivus humps carry deg=15 explicitly
RISER = 20.0
TREAD = 40.0                     # pit and temple stairs (concept section 5)
WALL_T = 100.0

CLIP_MODE = "walls"              # four clip walls flush outside the tree wall; the roof is the `Ceil_Sky` record in CLIPS (open sky, ADR 0008 1.2)
CLIP_HALF = (3400.0, 2800.0)
CLIP_H = 1680.0                  # concept: ceiling at Z 2000 = Corona 960 + 880 knockback cap + margin; lowered by the datum shift D1
THEME = "nemus"

from maps.records import Room, Box, Opening, Wall, Stairs, Ramp, Pad, Item, Spawn, Link, Light, Clip, Oct  # noqa: E402,F401

FILL_NAV_KINDS = ("floor", "wall", "platform", "step")
FLOATING_KINDS = ("ramp", "bridge", "bridgewall", "lintel")

RJ_CLEARANCE_MIN = 900.0
DUEL = "Velox.Mode.Duel"
EYE_Z = 160.0

# Levels (see the module docstring)
PIT, T1, T2, T3, F, U, C = -320.0, -240.0, -160.0, -80.0, 0.0, 320.0, 640.0
SKY = 1680.0                      # clip ceiling (underside)
TAN15 = math.tan(math.radians(15.0))
TAN20 = math.tan(math.radians(20.0))
HUMP = 400.0 * TAN15              # crest height of a root hump: 107.18 (concept 107)

# ---------------------------------------------------------------- rules (P4 spec fields)
RULES = {
    # concept section 8 (5 pads) / section 15 (5 JUMP_PAD, 3 ROCKET_JUMP, 2 JUMP, 1 JUMP_FAST, about 22 DROP) / section 6 (41 pickups)
    "pad_count": 5,
    "link_counts": {"JUMP_PAD": 5, "ROCKET_JUMP": 3, "JUMP": 2, "JUMP_FAST": 1, "DROP": 23},
    "item_mix": {"Powerup_Quad": 1, "Armor_Red": 1, "Health_Mega": 1, "Armor_Yellow": 2, "Weapon_Railgun": 1, "Weapon_RocketLauncher": 2,
                 "Weapon_LightningGun": 1, "Weapon_PlasmaGun": 2, "Weapon_Shotgun": 2, "Ammo_Slugs": 1, "Ammo_Rockets": 1,
                 "Ammo_Lightning": 1, "Ammo_Cells": 1, "Ammo_Shells": 1, "Ammo_Bullets": 1, "Health_Large": 1, "Health_Medium": 6,
                 "Health_Small": 7, "Armor_Shard": 8},
    # concept section 17.1: pad arcs keep >= 100 cm from the Tabulatum / Corona edges only where the numbers say so; the default arc rule stays
}
MAJOR_IDS = ("Quad", "RA", "Mega")


# ---------------------------------------------------------------- helpers (pure arithmetic over records)
def _b(bid, kind, x0, x1, y0, y1, z0, z1, mat=None):
    return Box(bid, kind, mat or {"floor": "Floor", "wall": "Wall"}.get(kind, "Platform"), x0, x1, y0, y1, z0, z1)


def _tier(n, outer, inner, top):
    """One seat tier: a ring of boxes cut for the pit stairs (|x| < 150, north and south) and the Radix Magna trench (|y| < 200, east)."""
    out = []
    for nm, y0, y1 in (("N", inner, outer), ("S", -outer, -inner)):
        out.append(_b(f"Tier{n}_{nm}W", "platform", -outer, -150, y0, y1, PIT, top))
        out.append(_b(f"Tier{n}_{nm}E", "platform", 150, outer, y0, y1, PIT, top))
    out.append(_b(f"Tier{n}_W", "platform", -outer, -inner, -inner, inner, PIT, top))
    out.append(_b(f"Tier{n}_EN", "platform", inner, outer, 200, inner, PIT, top))
    out.append(_b(f"Tier{n}_ES", "platform", inner, outer, -inner, -200, PIT, top))
    return out


# ---------------------------------------------------------------- rooms: open sky, none (the validator only reads them for ceiling classes)
ROOMS = []

# ---------------------------------------------------------------- solid boxes
BOXES = (
    # ---- ground: forest floor (F) around the Cavea, the Orchestra (PIT); all kind floor so a pad arc starts on it (as DM1's Ground)
    [_b("Orchestra", "floor", -1000, 1000, -1000, 1000, PIT - 100, PIT),
     _b("Forest_W", "floor", -3300, -1000, -2700, 2700, PIT - 100, F),
     _b("Forest_N", "floor", -1000, 1000, 1000, 2700, PIT - 100, F),
     _b("Forest_S", "floor", -1000, 1000, -2700, -1000, PIT - 100, F),
     # east: cut for the Radix Magna trench (|y| < 200 up to x 1300), the root crosses the rim at x 1281
     _b("Forest_E_N", "floor", 1000, 1300, 200, 2700, PIT - 100, F),
     _b("Forest_E_S", "floor", 1000, 1300, -2700, -200, PIT - 100, F),
     _b("Forest_E", "floor", 1300, 3300, -2700, 2700, PIT - 100, F),
     # ---- perimeter tree wall (SM_Nemus_TreeWall_800: 1200 high)
     _b("Wall_W", "wall", -3400, -3300, -2800, 2800, F, 1200),
     _b("Wall_E", "wall", 3300, 3400, -2800, 2800, F, 1200),
     _b("Wall_S", "wall", -3300, 3300, -2800, -2700, F, 1200),
     _b("Wall_N", "wall", -3300, 3300, 2700, 2800, F, 1200),
     ]
    + _tier(1, 600, 400, T1) + _tier(2, 800, 600, T2) + _tier(3, 1000, 800, T3)
    + [
        # ---- Templum: solid podium, gate wall wings, column stubs
        _b("Templum", "platform", -3300, -2500, -2000, 2000, F, U),
        _b("Gate_Wing_N", "wall", -2600, -2500, 300, 1200, U, U + 400),
        _b("Gate_Wing_S", "wall", -2600, -2500, -1200, -300, U, U + 400),
        _b("Stub_1", "wall", -2990, -2910, 460, 540, U, U + 120),
        _b("Stub_2", "wall", -2990, -2910, -540, -460, U, U + 160),
        _b("Stub_3", "wall", -2990, -2910, 760, 840, U, U + 200),
        _b("Stub_4", "wall", -2990, -2910, -840, -760, U, U + 120),
        _b("Stub_5", "wall", -3190, -3110, 560, 640, U, U + 160),
        _b("Stub_6", "wall", -3190, -3110, -640, -560, U, U + 200),
        # ---- Aquaeductus: solid wall with two arches (floating deck, 300 clear), the 700 cm collapse at x 700..1400
        _b("Aq_A", "platform", -2500, -2100, 1600, 2000, F, U),
        _b("Aq_Arch_W", "bridge", -2100, -1700, 1600, 2000, U - 20, U),
        _b("Aq_B", "platform", -1700, -200, 1600, 2000, F, U),
        _b("Aq_Arch_C", "bridge", -200, 200, 1600, 2000, U - 20, U),
        _b("Aq_C", "platform", 200, 700, 1600, 2000, F, U),
        _b("Aq_D", "platform", 1400, 2800, 1600, 2000, F, U),
        # ---- Trabs, the log: two segments, breaks at x -2500..-2100 (terrace) and -200..200 (middle); Radix Australis lands on Log_Land
        _b("Log_W", "platform", -2100, -200, -2000, -1600, F, U),
        _b("Log_E", "platform", 200, 2800, -2000, -1600, F, U),
        _b("Log_Land", "platform", -400, -200, -1600, -1300, F, U),
        # ---- Pons N / S (bridges, underside 300), Tabulatum (solid podium round the trunk), Corona (bridge slab, underside 300 above)
        _b("Pons_N", "bridge", 2400, 2800, 600, 1600, U - 20, U),
        _b("Pons_S", "bridge", 2400, 2800, -1600, -600, U - 20, U),
        _b("Tabulatum", "platform", 2160, 3300, -600, 600, F, U),
        _b("Corona_N", "bridge", 2460, 3300, 200, 550, C - 20, C),
        _b("Corona_S", "bridge", 2460, 3300, -550, -200, C - 20, C),
        _b("Corona_W", "bridge", 2460, 2650, -200, 200, C - 20, C),
        _b("Corona_E", "bridge", 3050, 3300, -200, 200, C - 20, C),
    ]
)

# ---------------------------------------------------------------- octagonal trunks (four boxes each, ADR 0008 1.3)
# Minor giants (Trunk_300, 4 x 400 stacked = 1600 high) and the Mother Tree: lower Trunk_600 up to the Corona underside, upper Trunk_300 scaled to 400.
OCTS = [
    Oct("Trunk_FW_N", "wall", "Wall", -1500, 700, 300, F, F + 1600, "Trunk_300"),
    Oct("Trunk_FW_S", "wall", "Wall", -1500, -700, 300, F, F + 1600, "Trunk_300"),
    Oct("Trunk_Via_W", "wall", "Wall", -1200, 2550, 300, F, F + 1600, "Trunk_300"),
    Oct("Trunk_Via_E", "wall", "Wall", 1300, 2300, 300, F, F + 1600, "Trunk_300"),
    Oct("Trunk_GNE", "wall", "Wall", 1500, 900, 300, F, F + 1600, "Trunk_300"),
    Oct("Trunk_GSE", "wall", "Wall", 1500, -900, 300, F, F + 1600, "Trunk_300"),
    Oct("Mother_Lower", "wall", "Wall", 2850, 0, 600, U, C - 20, "Trunk_600"),
    Oct("Mother_Upper", "wall", "Wall", 2850, 0, 400, C - 20, SKY, "Trunk_300"),
]

# ---------------------------------------------------------------- walls: none (the tree wall and stubs are boxes)
WALLS = []

# ---------------------------------------------------------------- stairs
STAIRS = [
    # Cavea: 16 risers of 20 cm, 40 cm treads, |y| 360..1000, from the Orchestra (-320) to the rim (0)
    Stairs("Pit_StairsN", 360, +1, -150, 150, 16, "y", PIT, 40, 20),
    Stairs("Pit_StairsS", -360, -1, -150, 150, 16, "y", PIT, 40, 20),
    # Templum: from the forecourt (F) up to the podium (U) at x -2500
    Stairs("Temple_Stairs", -1860, -1, -300, 300, 16, "x", F, 40, 20),
]

# ---------------------------------------------------------------- ramps
RAMPS = [
    # Radix Magna: Orchestra floor (x 402) to the Tabulatum (x 2160), 20 deg, 400 wide, sealed underneath
    Ramp("Radix_Magna", 2160.0 - 640.0 / TAN20, -200, 200, 640.0, "+x", 20.0, PIT, "solid"),
    # Radix Australis: forest floor to the log landing (x -400), 20 deg, 300 wide
    Ramp("Radix_Australis", -400.0 - 320.0 / TAN20, -1600, -1300, 320.0, "+x", 20.0, F, "solid"),
    # Rivus root humps: 15 deg up 400 / down 400, crest +107, centred on x -1800, -600, 600 (southern 400 cm of the Rivus)
    Ramp("Hump1_Up", -2200, -2700, -2300, HUMP, "+x", 15.0, F, "solid"), Ramp("Hump1_Dn", -1400, -2700, -2300, HUMP, "-x", 15.0, F, "solid"),
    Ramp("Hump2_Up", -1000, -2700, -2300, HUMP, "+x", 15.0, F, "solid"), Ramp("Hump2_Dn", -200, -2700, -2300, HUMP, "-x", 15.0, F, "solid"),
    Ramp("Hump3_Up", 200, -2700, -2300, HUMP, "+x", 15.0, F, "solid"), Ramp("Hump3_Dn", 1000, -2700, -2300, HUMP, "-x", 15.0, F, "solid"),
]

# ---------------------------------------------------------------- jump pads (AimAtTarget: the target is the capsule-centre apex, ADR 0002)
PADS = [
    # P1 (D2): on the Cavea's west rim instead of the Orchestra floor; lands in the Templum gate (concept: (-2922, 0, 640))
    Pad("P1", -1200, 0, (-2227, 0, 630), (-2900, 0, U)),
    Pad("P2", -3050, 2350, (-2950, 2000, 500), (-2899, 1823, U)),       # Via Sacra west -> Templum north wing
    Pad("P3", -3050, -2350, (-2950, -2000, 500), (-2899, -1823, U)),    # Rivus west -> Templum south wing
    Pad("P4", 3110, 1300, (3110, 640, 840), (3110, 369, C)),            # NE grove -> Corona north strip
    Pad("P5", 1800, -2450, (1800, -2000, 480), (1800, -1789, U)),       # Rivus -> east log
]

# ---------------------------------------------------------------- items (concept section 6; Z = concept Z - 320, D1)
Q = "/Game/Items/DA_Item_"


def _it(i, name, x, y, z=F, dis=()):
    return Item(i, Q + name.replace(".", "_"), x, y, z, tuple(dis))


ITEMS = [
    _it("Quad", "Powerup.Quad", 0, 0, PIT, (DUEL,)),
    _it("RA", "Armor.Red", -3000, -1100, U),
    _it("Mega", "Health.Mega", 500, -1800, U),
    _it("YA1", "Armor.Yellow", -1200, 1800, U),
    _it("YA2", "Armor.Yellow", 2400, -2350),
    _it("RG", "Weapon.Railgun", 3190, 0, C),
    _it("RL1", "Weapon.RocketLauncher", -1650, 0),
    _it("RL2", "Weapon.RocketLauncher", 2100, -1150),
    _it("LG", "Weapon.LightningGun", 0, 2350),
    _it("PG1", "Weapon.PlasmaGun", -900, -2150),
    _it("PG2", "Weapon.PlasmaGun", -2300, 2350),
    _it("SG1", "Weapon.Shotgun", 2100, 1150),
    _it("SG2", "Weapon.Shotgun", -2300, -2150),
    _it("H50", "Health.Large", 0, 1300),
    _it("H25_ForN", "Health.Medium", -2100, 1300),
    _it("H25_ForS", "Health.Medium", -2100, -1300),
    _it("H25_Root", "Health.Medium", 2300, 0, U),
    _it("H25_Via", "Health.Medium", 2400, 2350),
    _it("H25_Tier", "Health.Medium", -700, 0, T2),
    _it("H25_Temple", "Health.Medium", -2900, 1300, U),
    _it("H5_Aq1", "Health.Small", 1600, 1800, U),
    _it("H5_Aq2", "Health.Small", 1900, 1800, U),
    _it("H5_Aq3", "Health.Small", 2200, 1800, U),
    _it("H5_Aq4", "Health.Small", 2500, 1800, U),
    _it("H5_Hump1", "Health.Small", -1800, -2500, HUMP),
    _it("H5_Hump2", "Health.Small", -600, -2500, HUMP),
    _it("H5_Hump3", "Health.Small", 600, -2500, HUMP),
    _it("Shard_Log1", "Armor.Shard", -1700, -1800, U),
    _it("Shard_Log2", "Armor.Shard", -1300, -1800, U),
    _it("Shard_Log3", "Armor.Shard", -900, -1800, U),
    _it("Shard_Log4", "Armor.Shard", -500, -1800, U),
    _it("Shard_T3_NE", "Armor.Shard", 900, 900, T3),
    _it("Shard_T3_NW", "Armor.Shard", -900, 900, T3),
    _it("Shard_T3_SE", "Armor.Shard", 900, -900, T3),
    _it("Shard_T3_SW", "Armor.Shard", -900, -900, T3),
    _it("RocketsAmmo", "Ammo.Rockets", -1300, 900),
    _it("SlugsAmmo", "Ammo.Slugs", 2600, -1000, U),
    _it("LightningAmmo", "Ammo.Lightning", 1000, 2350),
    _it("CellsAmmo", "Ammo.Cells", 200, -2150),
    _it("ShellsAmmo", "Ammo.Shells", 2700, 1450),
    _it("BulletsAmmo", "Ammo.Bullets", 1300, 500),
]
WEAPON_IDS = ("RL1", "RL2", "LG", "PG1", "PG2", "SG1", "SG2", "RG")
MEGA_ID, RA_ID = "Mega", "RA"
AMMO_FOR = {"RL1": "RocketsAmmo", "RG": "SlugsAmmo", "LG": "LightningAmmo", "PG1": "CellsAmmo", "SG1": "ShellsAmmo"}
AMMO_MIN_CM, AMMO_MAX_CM = 600.0, 1200.0

# ---------------------------------------------------------------- spawns (11, concept section 7): all on the forest floor
SPAWNS = [
    Spawn("S1", -700, 2400, 0),
    Spawn("S2", 700, 2400, 180),
    Spawn("S3", -1800, 2300, 0),
    Spawn("S4", -2000, -600, 45),
    Spawn("S5", -2650, -2500, 0),
    Spawn("S6", -300, -2150, 180),
    Spawn("S7", 1800, -1450, 0),
    Spawn("S8", 2850, -1100, 180),
    Spawn("S9", 1800, 1450, 0),
    Spawn("S10", 2850, 1100, 180),
    Spawn("S11", -2800, 2500, 0),
]
SPAWN_MIN_APART = 1000.0
SPAWN_MAX_WEAPON = 800.0

# ---------------------------------------------------------------- nav links (ADR 0005 section 4; concept section 15)
def _drop(i, sx, sy, sz, ex, ey, ez):
    return Link(i, "DROP", (sx, sy, sz), (ex, ey, ez), None)


LINKS = [Link(f"PadLink_{p.id}", "JUMP_PAD", (p.x, p.y, F), p.land, p.id) for p in PADS] + [
    Link("RJ1", "ROCKET_JUMP", (2600, 1000, U), (2600, 375, C), None),       # Pons N -> Corona north strip, run-up 450 cm
    Link("RJ2", "ROCKET_JUMP", (2600, -1000, U), (2600, -375, C), None),
    Link("RJ3", "ROCKET_JUMP", (-900, 2450, F), (-900, 1800, U), None),      # Via Sacra -> aqueduct deck
    Link("Jump_TerraceLog", "JUMP", (-2560, -1800, U), (-2000, -1800, U), None),    # 400 gap at x -2500..-2100
    Link("Jump_LogBreak", "JUMP", (-260, -1800, U), (300, -1800, U), None),         # 400 break at x -200..200
    Link("JumpFast_Gap", "JUMP_FAST", (640, 1800, U), (1500, 1800, U), None),       # 700 collapse at x 700..1400, west -> east only
] + [
    # DROPs (concept section 15): start 60 cm inside the free edge, end 175 (or 150, 190) cm outside it where the landing is a clear >= 300 x 300 top,
    # >= 500 cm from a wall and >= 300 cm from a pad (E3 (d)). The Pons east sides, the Templum north / south wing corners and the root are left out
    # (the tree wall is < 500 cm ahead, or the landing is a slope); the concept's "Corona west -> root" lands on the Tabulatum west ledge instead.
    _drop("Drop_Temple_E_N", -2560, 1350, U, -2325, 1350, F),
    _drop("Drop_Temple_E_S", -2560, -1450, U, -2325, -1450, F),
    _drop("Drop_Temple_N", -2700, 1940, U, -2700, 2175, F),
    _drop("Drop_Temple_S", -2700, -1940, U, -2700, -2175, F),
    _drop("Drop_AqS_1", -1200, 1660, U, -1200, 1425, F),
    _drop("Drop_AqS_2", 300, 1660, U, 300, 1425, F),
    _drop("Drop_AqS_3", 1800, 1660, U, 1800, 1425, F),
    _drop("Drop_AqS_4", 2400, 1660, U, 2400, 1425, F),
    _drop("Drop_AqN_1", -900, 1940, U, -900, 2175, F),
    _drop("Drop_AqN_2", 500, 1940, U, 500, 2175, F),
    _drop("Drop_AqN_3", 1800, 1940, U, 1800, 2175, F),
    _drop("Drop_AqN_4", 2400, 1940, U, 2400, 2175, F),
    _drop("Drop_LogN_1", -1900, -1660, U, -1900, -1425, F),
    _drop("Drop_LogN_2", 700, -1660, U, 700, -1425, F),
    _drop("Drop_LogN_3", 1900, -1660, U, 1900, -1425, F),
    _drop("Drop_LogS_1", -1000, -1940, U, -1000, -2175, F),
    _drop("Drop_LogS_2", 1100, -1940, U, 1100, -2175, F),
    _drop("Drop_LogS_3", 2300, -1940, U, 2300, -2175, F),
    _drop("Drop_PonsN", 2460, 1000, U, 2250, 1000, F),
    _drop("Drop_PonsS", 2460, -1000, U, 2250, -1000, F),
    _drop("Drop_Corona_N", 2600, 490, C, 2600, 700, U),         # onto Pons N / the Tabulatum
    _drop("Drop_Corona_S", 2600, -490, C, 2600, -700, U),       # onto Pons S
    _drop("Drop_Corona_W", 2520, 0, C, 2310, 0, U),             # onto the Tabulatum west ledge
]

# ---------------------------------------------------------------- art-mode tables (task A1; empty until the art pass)
LIGHTS = []
CAPTURES = ()
LIGHTMASS_VOLUME = ((0.0, 0.0, 500.0), (7200.0, 6000.0, 2400.0))

# ---------------------------------------------------------------- clip: perimeter walls come from CLIP_MODE ("walls"), the roof is this record
CLIPS = [
    Clip("Ceil_Sky", -3500, 3500, -2900, 2900, SKY, SKY + 100),
]

NAV_CENTER = (0.0, 0.0, 600.0)
NAV_SIZE = (7000.0, 5800.0, 2400.0)      # X -3500..3500, Y -2900..2900, Z -600..1800

INTERMISSION_CAMERA = ((0.0, -3300.0, 2600.0), (0.0, -45.0, 90.0))   # location, (roll, pitch, yaw)
INTERMISSION_LABEL = (0.0, 0.0, 2200.0)

UPPER_RING_MIN_M, UPPER_RING_MAX_M = 38.0, 45.0       # RA -> Mega on the upper ring (concept section 17.1; measured in the task notes)

# Per-kind actor counts that do not depend on the geometry expansion (the geometry kinds are counted by the generator).
EXPECTED = {
    "pickup": 41, "playerstart": 11, "jumppad": 5, "targetpoint": 5, "navbounds": 1, "navlink": len(LINKS),
    "camera": 1, "text": 1, "light": 2,
}

# ---------------------------------------------------------------- line of sight (concept section 17.1)
POINTS = {
    # Corona standing points: the west, north and south strips (eye 160 above the slab)
    "Corona": [(2570, 0, C), (2570, 150, C), (2570, -150, C), (2800, 250, C), (2800, 500, C), (3000, 250, C), (3000, 500, C),
               (3250, 250, C), (3250, 500, C), (2800, -250, C), (2800, -500, C), (3000, -250, C), (3000, -500, C),
               (3250, -250, C), (3250, -500, C)],
}
LOS_BLOCKED = [("spawns", "majors"), ("Corona", "RA")]
LOS_CLEAR = []
FREE_EDGE_EXEMPT = ()
