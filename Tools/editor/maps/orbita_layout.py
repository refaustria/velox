"""Orbita: the layout table (ADR 0008 sections 1.2, 3.4, 4; concept docs/maps/orbita.md, task M7 S5). Data only, no `unreal` import.

Centimetres, X east, Y north. Seven decks hang in open void around a stepped hub; there is no wall, no ceiling and no perimeter clip
(CLIP_MODE "none"). Lowest deck top (Hortus) is Z = -320; FLOOR_Z stays 0 (the hub lower deck, the pad datum of mapgen).
Footprint X -4400..4200, Y -3400..3800. The void is one `Hazard(type="void")` slab (top -1400, bottom -2000, footprint + 6000 on every
side); KILL_Z = -2600 is only the safety net below it (ADR 0008 3.1, 3.4).

Top-down, north up (concept section 3 drawing; the area table of section 3.1 is binding):

  Hortus (Z -320) y 2800..3800        Scopulus (Z 160) x -3000..-1800, y 2600..3800
        |  Clivus (ramp, y 1600..2794)
  Nucleus hub (Z 0) +-1600, Corona (Z 320) +-800, Specula (Z 640) +-300, dish on its south edge; hub ramps RN / RS (y +-800..1200)
  Armamentarium (Z 0) x -4400..-3200 -- Pons Fractus (gap 400) --> hub     Trabs (Z 320) x 3400..4000, y -1600..2000, node +-500
        |  Pons Australis (x +-200, y -2400..-1600)                                  ^ Clivus Orientis (ramp, y -2794..-1600)
  Horreum (Z 0) x -1400..2000, y -3400..-2400 ---- Semita (x 2000..3900, y -3200..-2800, Z 0) ----+

Pad datum. `mapgen` launches every pad from FLOOR_Z and `veloxmap.jump_pad` / `player_start` put a pad / spawn at FLOOR_Z + constant: a
pad or a spawn on another deck cannot be expressed yet. P3 (Scopulus 160), P4 (Hortus -320), P6 (Trabs 320) and S7 (Trabs 320) are
therefore declared in PADS_ELEVATED / SPAWNS_ELEVATED with their true deck height and join PADS / SPAWNS (and the links of the pads)
automatically as soon as `records.Pad` / `records.Spawn` get a trailing `z` field. Until then the tables below hold P1, P2, P5 and
S1..S6 and validate green; the six arcs are checked with their true origin by the task notes' script (docs/tasks/m7-maps/16-s5-orbita-spec.md).
"""
import math

from maps import records
from maps.records import Box, Clip, Hazard, Item, Link, Pad, Ramp, Spawn  # noqa: F401  (records live in maps/records.py)

MAP_ID = "ORBITA"
SLUG = "orbita"
DISPLAY_NAME = "Orbita"
MAP_PATH = "/Game/Maps/Orbita/L_Orbita_Blockout"
ART_MAP_PATH = "/Game/Maps/Orbita/L_Orbita"
LOG_PREFIX = "[VeloxOrbita]"
SKY_CUBEMAP = "/Game/Environment/Lighting/TC_Sky_Orbita"
THEME = "orbita"
FLOOR_Z = 0.0                    # hub lower deck; the lowest deck (Hortus) is -320 (E0.3 e exception, band file VELOX_ARENA_FLOOR_Z = -700)
KILL_Z = -2600.0                 # ADR 0008 3.4: a safety net below the void slab (top -1400), never a gameplay death
CLIP_MODE = "none"               # ADR 0008 3.4: no perimeter clip at all
LEVEL_STEP = 320.0
GRID = 100.0
MODULE = 400.0
RAMP_DEG = 15.0
RISER = 20.0
TREAD = 50.0
WALL_T = 100.0
DUEL = "Velox.Mode.Duel"
FFA = "Velox.Mode.FFA"

# Decks are "floor" boxes (solid, 200 thick, no nav fill: there is nothing under them); covers are "wall" (not standable) or "platform".
FILL_NAV_KINDS = ()
FLOATING_KINDS = ("ramp", "bridge", "bridgewall", "lintel")

# ---------------------------------------------------------------- ramps (15 deg; run for a rise of 320 is 1194.2: feet placed so the top meets its deck flush)
# ramps: 15 deg, rise 320, run 1194.2; feet are placed so each top meets its deck flush
RAMPS = [
    Ramp("RN", -594.2, 800, 1200, 320.0, "+x"),                   # hub ramp north: foot -594.2 (N0), top x 600 -> landing 600..800 at 320
    Ramp("RS", 594.2, -1200, -800, 320.0, "-x"),                  # point mirror of RN: top x -600 -> landing -800..-600
    Ramp("CL", 2794.2, -200, 200, 320.0, "-y", None, -320.0),     # Clivus: foot at Hortus (z -320, y 2794.2 overlaps Hortus 5.8 cm), top y 1600 flush with N0
    Ramp("CO", -2794.2, 3500, 3900, 320.0, "+y"),                 # Clivus Orientis: foot on Semita (y -2794.2), top y -1600 flush with Trabs
]


def ramp_z(r, s):
    """Surface height s cm up the run of a ramp that starts at z 0 (items on a hub ramp)."""
    return s * math.tan(math.radians(RAMP_DEG))


# ---------------------------------------------------------------- solids (concept section 3.1, area table)
F = "floor"
P = "platform"
W = "wall"
B = "bridge"
BOXES = [
    # N0 Nucleus: hub lower deck (an 800 band around the Corona block)
    Box("N0", F, "Floor", -1600, 1600, -1600, 1600, -200, 0),
    Box("N1", P, "Platform", -800, 800, -800, 800, 0, 320),                    # Corona (solid block)
    Box("N2", P, "Platform", -300, 300, -300, 300, 320, 640),                  # Specula
    Box("N2d", W, "Wall", -300, 300, -300, -250, 640, 940),                    # antenna dish, backstop of P4, hides Specula from the south
    Box("RN_Land", P, "Platform", 590, 800, 800, 1200, 120, 320),              # hub ramp north landing (joins the Corona walk at y 800)
    Box("RS_Land", P, "Platform", -800, -590, -1200, -800, 120, 320),
    Box("Pylon_NW", W, "Wall", -1200, -1000, 1000, 1200, 0, 400),              # N0 corner pylons 200 x 200 x 400
    Box("Pylon_SW", W, "Wall", -1200, -1000, -1200, -1000, 0, 400),
    Box("Pylon_NE", W, "Wall", 1400, 1600, 1200, 1400, 0, 400),                # east pair moved out behind the spawns (see notes D3)
    Box("Pylon_SE", W, "Wall", 1400, 1600, -1400, -1200, 0, 400),
    # PA Pons Australis (south bridge) and HR Horreum (cargo deck)
    Box("PA", B, "Platform", -200, 200, -2400, -1600, -200, 0),
    Box("HR", F, "Floor", -1400, 2000, -3400, -2400, -200, 0),
    Box("K1", P, "Platform", -600, -200, -3300, -3100, 0, 160),                # container 400 x 200 x 160
    Box("K1_Crate", P, "Platform", -200, 0, -3300, -3100, 0, 100),             # 100 cm step beside it (200 wide: walkable-width rule)
    Box("K2", P, "Platform", 800, 1200, -2700, -2500, 0, 200),
    Box("HR_Parapet", W, "Wall", -400, 400, -3400, -3380, 0, 120),
    # SE Semita and TR Trabs (Clivus Orientis joins them)
    Box("SE", F, "Floor", 2000, 3900, -3200, -2800, -200, 0),
    Box("TR", F, "Floor", 3400, 4000, -1600, 2000, 120, 320),
    Box("TRn", F, "Floor", 3200, 4200, -500, 500, 120, 320),                   # node, P1 landing
    Box("TRn_Back", W, "Wall", 4150, 4200, -400, 400, 320, 620),
    Box("TR_ParapetN", W, "Wall", 3400, 3420, 800, 1200, 320, 440),
    Box("TR_ParapetS", W, "Wall", 3400, 3420, -1200, -800, 320, 440),
    Box("TR_Pylon", P, "Platform", 3400, 3600, 1400, 1600, 320, 620),
    # PF Pons Fractus (broken west bridge, 400 gap at x -2600..-2200), AR Armamentarium (west dock)
    Box("PFw", B, "Platform", -3200, -2600, -200, 200, -200, 0),
    Box("PFe", B, "Platform", -2200, -1600, -200, 200, -200, 0),
    Box("AR", F, "Floor", -4400, -3200, -800, 800, -200, 0),
    Box("AR_Cover", W, "Wall", -3950, -3850, 100, 500, 0, 200),                # RA cover block
    Box("AR_Parapet", W, "Wall", -4400, -4380, -400, 400, 0, 120),
    # HO Hortus (grow deck, z -320), HOj jetty, SC Scopulus (captured rock, z 160)
    Box("HO", F, "Floor", -800, 800, 2794.2, 3800, -520, -320),
    Box("HOj", F, "Floor", -1200, -800, 3000, 3400, -520, -320),
    Box("HO_Rack1", W, "Wall", -600, -200, 3300, 3400, -320, -120),
    Box("HO_Rack2", W, "Wall", 300, 700, 3550, 3650, -320, -120),
    Box("SC", F, "Floor", -3000, -1800, 2600, 3800, -40, 160),
    Box("SC_Rock", W, "Wall", -2200, -1800, 3400, 3800, 160, 460),            # rock outcrop, P2 backstop
]

WALLS = []
STAIRS = []
ROOMS = []

# One cheap lid above the hub so the two ROCKET_JUMP links satisfy the generic "clearance above the take-off" rule (RJ_CLEARANCE_MIN
# reads a ceiling clip; with CLIP_MODE none there is none). 1700 is far above every arc (highest pad apex +840) and every rocket jump (~880).
CLIPS = [Clip("Ceil_Hub", -1000, 1000, -1000, 1000, 1700, 1800)]
RJ_CLEARANCE_MIN = 900.0

# ---------------------------------------------------------------- void (ADR 0008 3.4): top -1400, bottom -2000, footprint + 6000 per side
HAZARDS = [Hazard("VoidSlab", "void", -4400 - 6000, 4200 + 6000, -3400 - 6000, 3800 + 6000, -2000, -1400)]
OCTS = []

# ---------------------------------------------------------------- pads (concept 5.1; AimAtTarget: target = apex; land = authored landing)
PADS = [
    Pad("P1", 1300, 0, (2730, 0, 520), (3500, 0, 320)),                  # Nucleus east -> Trabs node
    Pad("P2", -4000, 600, (-3250, 1885, 700), (-2600, 3000, 160)),       # Armamentarium -> Scopulus
    Pad("P5", -1000, -2900, (-2300, -1700, 640), (-3600, -500, 0)),      # Horreum -> Armamentarium ("Arcus Longus")
]
# (id, deck z of the pad, x, y, apex, landing); true heights, see the module docstring
PADS_ELEVATED = [
    ("P3", 160.0, -2000, 2800, (-1685, 2170, 560), (-1300, 1400, 0)),    # Scopulus -> Nucleus north-west
    ("P4", -320.0, -500, 3200, (-130, 830, 840), (0, 0, 640)),           # Hortus -> Specula ("Ascensus")
    ("P6", 320.0, 3700, 1800, (2615, 2310, 620), (500, 3300, -320)),     # Trabs north tip -> Hortus
]
PAD_Z_SUPPORTED = "z" in getattr(Pad, "_fields", ())
if PAD_Z_SUPPORTED:                                                          # records.Pad grew a z field: the three elevated pads join
    PADS += [Pad(i, x, y, t, l, z=z) for (i, z, x, y, t, l) in PADS_ELEVATED]
    PADS.sort(key=lambda p: p.id)

# ---------------------------------------------------------------- items (concept section 6; 37 pickups, 33 FFA / 35 Duel)
Q = "/Game/Items/DA_Item_"


def _it(i, name, x, y, z=0.0, dis=()):
    return Item(i, Q + name.replace(".", "_"), x, y, z, tuple(dis))


_RN, _RS = RAMPS[0], RAMPS[1]
ITEMS = [
    # majors
    _it("RA", "Armor.Red", -4200, 300, 0),
    _it("Mega", "Health.Mega", 0, 3650, -320),
    _it("YA_S", "Armor.Yellow", 3700, -3000, 0),
    _it("YA_N", "Armor.Yellow", -2400, 3200, 160, (DUEL,)),                  # FFA only (per-instance gating: see notes)
    _it("H25_Scop", "Health.Medium", -2400, 3200, 160, (FFA,)),              # Duel only, same spot
    _it("Quad", "Powerup.Quad", 0, 0, 640, (DUEL,)),
    _it("Shard_Sp1", "Armor.Shard", -150, 100, 640, (FFA,)),                 # Duel only
    _it("Shard_Sp2", "Armor.Shard", 150, 100, 640, (FFA,)),
    _it("Shard_Sp3", "Armor.Shard", 0, -100, 640, (FFA,)),
    # weapons
    _it("RL", "Weapon.RocketLauncher", 0, -3000, 0),
    _it("RG", "Weapon.Railgun", 3700, 1700, 320),
    _it("LG", "Weapon.LightningGun", -1200, 300, 0),
    _it("PG", "Weapon.PlasmaGun", 400, 1400, 0),
    _it("SG", "Weapon.Shotgun", -3300, 0, 0),
    # ammo
    _it("RocketsAmmo", "Ammo.Rockets", 1400, -2600, 0),                      # concept (1200, -2600) is the edge of container K2
    _it("SlugsAmmo", "Ammo.Slugs", 1300, 400, 0),
    _it("LightningAmmo", "Ammo.Lightning", 1300, -400, 0),
    _it("CellsAmmo", "Ammo.Cells", -550, 3600, -320),
    _it("ShellsAmmo", "Ammo.Shells", -4200, -600, 0),
    _it("BulletsAmmo", "Ammo.Bullets", 0, 550, 320),
    # health
    _it("H50", "Health.Large", 0, -550, 320),
    _it("H25_1", "Health.Medium", 1300, -1000, 0),
    _it("H25_2", "Health.Medium", -700, 1400, 0),
    _it("H25_3", "Health.Medium", 3700, 900, 320),
    _it("H25_4", "Health.Medium", -1200, -3200, 0),
    _it("H5_PA1", "Health.Small", 0, -1800, 0),
    _it("H5_PA2", "Health.Small", 0, -2000, 0),
    _it("H5_PA3", "Health.Small", 0, -2200, 0),
    _it("H5_SE1", "Health.Small", 2400, -3000, 0),
    _it("H5_SE2", "Health.Small", 2800, -3000, 0),
    _it("H5_SE3", "Health.Small", 3200, -3000, 0),
    # armour shards up the hub ramps
    _it("Shard_RN1", "Armor.Shard", -300, 1000, ramp_z(_RN, -300 - _RN.x0)),
    _it("Shard_RN2", "Armor.Shard", 0, 1000, ramp_z(_RN, 0 - _RN.x0)),
    _it("Shard_RN3", "Armor.Shard", 300, 1000, ramp_z(_RN, 300 - _RN.x0)),
    _it("Shard_RS1", "Armor.Shard", 300, -1000, ramp_z(_RS, _RS.x0 - 300)),
    _it("Shard_RS2", "Armor.Shard", 0, -1000, ramp_z(_RS, _RS.x0 - 0)),
    _it("Shard_RS3", "Armor.Shard", -300, -1000, ramp_z(_RS, _RS.x0 + 300)),
]
WEAPON_IDS = ("PG", "SG", "LG", "RL", "RG")
MEGA_ID, RA_ID = "Mega", "RA"
MAJOR_IDS = ("Mega", "RA", "YA_S", "YA_N", "Quad")
AMMO_FOR = {"PG": "CellsAmmo", "SG": "ShellsAmmo", "LG": "LightningAmmo", "RL": "RocketsAmmo", "RG": "SlugsAmmo"}
AMMO_MIN_CM, AMMO_MAX_CM = 600.0, 2800.0      # concept 6: ammo sits 11..27 m from its weapon (decks are far apart); DM1's 6..12 m does not fit

# ---------------------------------------------------------------- spawns (concept 7; 7 starts, weapon within 25 m of travel)
SPAWNS = [
    Spawn("S1", -1350, -1350, 45),
    Spawn("S2", 1250, 1350, 225),         # concept (1350, 1350): moved 100 west so the pylon Pylon_NE stands behind it as seen from the Trabs node
    Spawn("S3", 1250, -1350, 135),
    Spawn("S4", -3400, 650, 225),
    Spawn("S5", -1300, -3100, 0),         # concept (-1200, -3100): 283 cm from the P5 trigger; moved to 360
    Spawn("S6", 1700, -3100, 180),
]
SPAWNS_ELEVATED = [("S7", 320.0, 3700, -700, 90)]
SPAWN_Z_SUPPORTED = "z" in getattr(Spawn, "_fields", ())
if SPAWN_Z_SUPPORTED:
    SPAWNS += [Spawn(i, x, y, yaw, z=z) for (i, z, x, y, yaw) in SPAWNS_ELEVATED]
SPAWN_MIN_APART = 1000.0
SPAWN_MAX_WEAPON = 2500.0                 # concept 7: within 25 m of travel (DM1's 8 m rule does not fit decks this far apart)
EYE_Z = 160.0

# ---------------------------------------------------------------- nav links (concept 11.2; link ends are decks, never void)
def _drop(i, sx, sy, sz, ex, ey, ez):
    return Link(i, "DROP", (sx, sy, sz), (ex, ey, ez), None)


def _gap(i, kind, sx, sy, ex, ey, z):
    return Link(i, kind, (sx, sy, z), (ex, ey, z), None)


LINKS = [Link("PadLink_" + p.id, "JUMP_PAD", (p.x, p.y, getattr(p, "z", 0.0)), p.land, p.id) for p in PADS]
LINKS += [
    # G1 Pons Fractus, both ways: start 60 cm before the take-off edge, end 100 cm onto the far deck
    _gap("G1_East", "JUMP", -2660, 0, -2100, 0, 0.0),
    _gap("G1_West", "JUMP", -2140, 0, -2700, 0, 0.0),
    # G2 Scopulus -> jetty (drop 480), one way
    Link("G2_Jetty", "JUMP", (-1860, 3200, 160.0), (-1100, 3200, -320.0), None),
    # D1 Corona -> N0 (60 inside the edge, 175 outside it; north/south only beside the hub ramps), D2 Specula -> Corona
    _drop("D1_W1", -740, -450, 320, -1000, -450, 0), _drop("D1_W2", -740, 450, 320, -1000, 450, 0),
    _drop("D1_E1", 740, -450, 320, 1000, -450, 0), _drop("D1_E2", 740, 450, 320, 1000, 450, 0),
    _drop("D1_N", -720, 740, 320, -720, 1000, 0), _drop("D1_S", 720, -740, 320, 720, -1000, 0),
    _drop("D2_N", 0, 240, 640, 0, 475, 320), _drop("D2_E", 240, 0, 640, 475, 0, 320), _drop("D2_W", -240, 0, 640, -475, 0, 320),
    # RJ1 Corona north / west band -> Specula
    Link("RJ1_N", "ROCKET_JUMP", (0, 650, 320.0), (0, 150, 640.0), None),
    Link("RJ1_W", "ROCKET_JUMP", (-650, 0, 320.0), (-150, 0, 640.0), None),
]
# the JUMP_PAD links of P3 / P4 / P6 come from PADS above once Pad.z exists

# ---------------------------------------------------------------- line of sight (concept 9)
POINTS = {
    "HubW": [(-1300, -1000, 0.0), (-1300, 0, 0.0), (-1300, 1000, 0.0), (-1300, 1400, 0.0)],
    "HubE": [(1300, -1000, 0.0), (1300, 1000, 0.0)],
    "HubN": [(-600, 1400, 0.0), (0, 1400, 0.0), (600, 1400, 0.0)],
    "HubS": [(-600, -1400, 0.0), (0, -1400, 0.0), (600, -1400, 0.0)],
    "TrabsNode": [(3500, 0, 320.0), (3700, 0, 320.0)],
    "TrabsTip": (3700, 1700, 320.0),
    "SpeculaEye": (0, 0, 640.0),
    "HorreumMid": [(-1000, -3000, 0.0), (0, -3000, 0.0), (1000, -3000, 0.0), (1400, -2900, 0.0)],
    "ArmEye": [(-3500, -300, 0.0), (-3500, 300, 0.0)],
    "Scopulus": (-2600, 3000, 160.0),
}
LOS_BLOCKED = [
    ("HubN", "HubS"),                  # everything through the hub centre (Corona 320, spindle 640, dish 940)
    ("HubW", "HubE"),
    ("SpeculaEye", "HorreumMid"),      # Specula cannot see Horreum (dish)
    ("HubW", "RA"),                    # RA cannot be railed from the hub (cover block)
    ("TrabsNode", "S1"), ("TrabsNode", "S2"), ("TrabsNode", "S3"),   # every hub spawn stands behind a corner pylon as seen from the Trabs node
]
LOS_CLEAR = [
    ("TrabsTip", "Mega"),              # Trabs tip <-> Hortus, 40 m looking 640 down
    ("ArmEye", "Scopulus"),            # Armamentarium <-> Scopulus 26 m
    ("TrabsNode", "HubE"),             # Trabs node <-> N0 east band
]

# ---------------------------------------------------------------- rules (concept sections; every override says which)
RULES = {
    "pad_count": len(PADS),                                              # 3 until Pad.z exists, then 6 (concept 5.1)
    "link_counts": {"JUMP_PAD": len(PADS), "JUMP": 3, "DROP": 9, "ROCKET_JUMP": 2},       # concept 11.2 (D1 6 of 8, see notes D5)
    "item_mix": {"Armor_Red": 1, "Health_Mega": 1, "Armor_Yellow": 2, "Powerup_Quad": 1, "Armor_Shard": 9, "Health_Medium": 5,
                 "Health_Large": 1, "Health_Small": 6, "Weapon_RocketLauncher": 1, "Weapon_Railgun": 1, "Weapon_LightningGun": 1,
                 "Weapon_PlasmaGun": 1, "Weapon_Shotgun": 1, "Ammo_Rockets": 1, "Ammo_Slugs": 1, "Ammo_Lightning": 1, "Ammo_Cells": 1,
                 "Ammo_Shells": 1, "Ammo_Bullets": 1},                  # concept 6: 37 pickups
    "free_edge_report": True,            # every deck edge is a void edge by design (concept 10.4): figure only
}
FREE_EDGE_EXEMPT = []

# ---------------------------------------------------------------- art-mode data (task A5 fills lights; sun, fill and sky are the blockout's two lights)
LIGHTS = []
CAPTURES = ()
LIGHTMASS_VOLUME = ((-100.0, 200.0, -100.0), (9000.0, 7600.0, 2600.0))      # concept 12: X -4600..4400, Y -3600..4000, Z -1400..1200

NAV_CENTER = (-100.0, 200.0, 200.0)
NAV_SIZE = (9000.0, 7600.0, 1800.0)       # concept 11.1: X -4600..4400, Y -3600..4000, Z -700..1100

INTERMISSION_CAMERA = ((-100.0, -7000.0, 1500.0), (0.0, -12.0, 90.0))
INTERMISSION_LABEL = (-100.0, -4600.0, 500.0)

EXPECTED = {
    "pickup": len(ITEMS), "playerstart": len(SPAWNS), "jumppad": len(PADS), "targetpoint": len(PADS), "navbounds": 1,
    "navlink": len(LINKS), "camera": 1, "text": 1, "light": 2,
}
