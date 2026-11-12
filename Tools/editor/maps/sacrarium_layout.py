"""Sacrarium: the layout table (M7 task S1; concept docs/maps/sacrarium.md, ADR 0008 sections 1.2, 2, 4). Single source of truth for
`create_map.py --map sacrarium`. Data only: no `unreal` import; the helper functions are pure arithmetic over the records.

Centimetres, X east, Y north, lowest walkable floor top at Z = 0 (FLOOR_Z), 100 cm grid, 400 cm module, 100 cm walls.
Footprint 56 x 56 m (X, Y -2800..2800), three levels (0 / 320 / 640), level step 320.

Plan (north up; `#` solid, `.` open at level 0, `a` the atrium's open well, `g` gallery deck over the arcade, `L` loft above `g`):

  y 2800 +----------------------------------------------------------------------------+
         |  C1 Red Chapel   |       mass       |        C2 Armoury                    |
         |  RA ledge + stairs| N aisle Via Longa|        RL                            |
  y 1300 +--door--+--landing NW-----------landing NE--door-+
         | W aisle |   atrium  (x, y +-1200): well +-800, arcade, galleries z 320, loft z 640 (south)  | E aisle + ramp |
         | Shrine  |   Quad dais at the centre                                                          | Ascent         |
  y-1300 +--door--+--------------------------------------+-------landing SE--+
         |  C4 Crypt        |  S aisle Chancel (12 m)    |        C3 Mega Chapel, Mega ledge   |
  y-2800 +----------------------------------------------------------------------------+

Levels: 0 cloister ring (four aisles, four chapels) and the atrium floor; 1 (320) gallery ring inside the atrium, three corner
landings, RA ledge, Mega ledge, top of the east ramp; 2 (640) the rail loft over the south gallery.

Decks are `bridge` boxes 300..320 (clear 300 below, as DM1's bridges). Walls are built by `_holed_wall` (a wall with rectangular
holes, possibly stacked: a ground door 0..300 below a level-1 opening 320..620; the 20 cm floating slab between is kind `lintel`).

Deviations from the concept (graph and rules binding, coordinates are not; each one is listed in docs/tasks/m7-maps/12-s1-sacrarium-spec.md):
  D1 12 arcade columns, not 16: none at x or y = 0, so the four arch axes (x or y -200..200) are not blocked at the arch centre.
  D2 stair shards sit 25 cm into the tread (x -2475 / -2225 / -1975) instead of on the tread's front edge.
  D3 pad landings are the authored concept points; the simulated landings are printed as `<pad>_landing` figures.
  D4 the window/door stacks are authored as stacked holes in one wall (ground door 0..300 and level-1 opening 320..620 share y or x).
"""
import math

MAP_ID = "SACRARIUM"
SLUG = "sacrarium"
DISPLAY_NAME = "Sacrarium"
MAP_PATH = "/Game/Maps/Sacrarium/L_Sacrarium_Blockout"
ART_MAP_PATH = "/Game/Maps/Sacrarium/L_Sacrarium"
LOG_PREFIX = "[VeloxSacrarium]"
FLOOR_Z = 0.0
KILL_Z = -1000.0
LEVEL_STEP = 320.0
GRID = 100.0
MODULE = 400.0
RAMP_DEG = 15.0
RISER = 20.0
TREAD = 50.0
WALL_T = 100.0

CLIP_MODE = "box"                # four perimeter clip walls flush outside the outer walls plus the roof slab (ADR 0008 1.2)
CLIP_HALF = (2800.0, 2800.0)
CLIP_H = 1800.0
THEME = "sacrarium"

from maps.records import Room, Box, Opening, Wall, Stairs, Ramp, Pad, Item, Spawn, Link, Light, Clip  # noqa: E402,F401

FILL_NAV_KINDS = ("floor", "wall", "platform", "step")
FLOATING_KINDS = ("ramp", "bridge", "bridgewall", "lintel")

RJ_CLEARANCE_MIN = 900.0
DUEL = "Velox.Mode.Duel"
EYE_Z = 160.0

# P4 spec fields
RULES = {
    "pad_count": 4,
    "link_counts": {"JUMP_PAD": 4, "ROCKET_JUMP": 2, "DROP": 16},
    # concept section 14 (22 links) / section 6 (38 pickups)
    "item_mix": {"Powerup_Quad": 1, "Armor_Red": 1, "Health_Mega": 1, "Armor_Yellow": 2, "Weapon_Railgun": 1, "Weapon_RocketLauncher": 1,
                 "Weapon_LightningGun": 1, "Weapon_PlasmaGun": 1, "Weapon_Shotgun": 2, "Ammo_Slugs": 1, "Ammo_Rockets": 1,
                 "Ammo_Lightning": 1, "Ammo_Cells": 1, "Ammo_Shells": 1, "Ammo_Bullets": 1, "Health_Large": 1, "Health_Medium": 6,
                 "Health_Small": 8, "Armor_Shard": 6},
    # concept section 5 (E): the ground lane beside the east ramp is exactly 300 wide, so a 300 x 300 clear top (walls count as blocked on
    # the boundary) is impossible for the NE-landing drop; 280 keeps the drop landing on the lane.
    "drop_clear": 280.0,
}
MAJOR_IDS = ("Mega", "RA")


# ---------------------------------------------------------------- helpers (pure arithmetic over records)
def _holed_wall(wid, run, s0, s1, b0, b1, top, holes, mat="Wall"):
    """A wall strip from z 0 to `top` with rectangular holes (a0, a1, zlo, zhi) along its run axis ('x': spans x s0..s1 in the band
    y b0..b1; 'y': the converse). Holes may be stacked (same a-range, different z). Solid pieces that touch z 0 are kind `wall`,
    pieces floating above a hole are kind `lintel` (not nav-filled underneath). Equal neighbours are merged."""
    cuts = sorted({s0, s1} | {v for h in holes for v in h[:2] if s0 < v < s1})
    cols = []
    for a, b in zip(cuts, cuts[1:]):
        zs = sorted((h[2], h[3]) for h in holes if h[0] <= a and h[1] >= b)
        segs, cur = [], 0.0
        for lo, hi in zs:
            if lo > cur:
                segs.append((cur, lo))
            cur = max(cur, hi)
        if cur < top:
            segs.append((cur, top))
        cols.append((a, b, tuple(segs)))
    merged = []
    for a, b, segs in cols:
        if merged and merged[-1][2] == segs and merged[-1][1] == a:
            merged[-1] = (merged[-1][0], b, segs)
        else:
            merged.append((a, b, segs))
    out = []
    for n, (a, b, segs) in enumerate(merged):
        for j, (z0, z1) in enumerate(segs):
            kind = "wall" if z0 == 0.0 else "lintel"
            rid = f"{wid}_{n}_{j}"
            if run == "x":
                out.append(Box(rid, kind, mat, a, b, b0, b1, z0, z1))
            else:
                out.append(Box(rid, kind, mat, b0, b1, a, b, z0, z1))
    return out


# ---------------------------------------------------------------- rooms (for the assertions of mapgen.validate_layout)
ROOMS = [
    Room("A", "Atrium", -1200, 1200, -1200, 1200, 0, 1600, "atrium"),
    Room("N", "North aisle Via Longa", -1300, 1300, 1300, 1900, 0, 800, "corridor"),
    Room("W", "West aisle Shrine Walk", -1900, -1300, -1300, 1300, 0, 800, "corridor"),
    Room("S", "South aisle Chancel", -1300, 1300, -1900, -1300, 0, 1200, "corridor"),
    Room("E", "East aisle Ascent", 1300, 1900, -1300, 1300, 0, 800, "corridor"),
    Room("C1", "Red Chapel", -2700, -1400, 1400, 2700, 0, 800, "room"),
    Room("C2", "Armoury", 1400, 2700, 1400, 2700, 0, 800, "room"),
    Room("C3", "Mega Chapel", 1400, 2700, -2700, -1400, 0, 800, "room"),
    Room("C4", "Crypt", -2700, -1400, -2700, -1400, 0, 480, "room"),
    Room("Loft", "Rail loft", -1200, 1200, -1200, -800, 640, 960, "room"),
]

# ---------------------------------------------------------------- solid boxes
BOXES = [Box("Ground", "floor", "Floor", -2800, 2800, -2800, 2800, -100, 0)]

# outer ring (100 cm) and the masses between aisles and chapels
BOXES += [
    Box("Per_N", "wall", "Wall", -2800, 2800, 2700, 2800, 0, 800),
    Box("Per_E", "wall", "Wall", 2700, 2800, -2700, 2700, 0, 800),
    Box("Per_W_N", "wall", "Wall", -2800, -2700, -1400, 2700, 0, 800),
    Box("Per_W_Crypt", "wall", "Wall", -2800, -2700, -2700, -1400, 0, 480),
    Box("Per_S_W", "wall", "Wall", -2800, -1400, -2800, -2700, 0, 480),
    Box("Per_S_C", "wall", "Wall", -1400, 1400, -2800, -2700, 0, 1200),
    Box("Per_S_E", "wall", "Wall", 1400, 2800, -2800, -2700, 0, 800),
    Box("Mass_N", "wall", "Wall", -1400, 1400, 1900, 2700, 0, 800),
    Box("Mass_S", "wall", "Wall", -1400, 1400, -2700, -1900, 0, 1200),
    Box("Mass_W_a", "wall", "Wall", -2700, -2100, -1300, 1300, 0, 800),          # west aisle's outer mass with the shrine niche cut out
    Box("Mass_W_b", "wall", "Wall", -2100, -1900, 200, 1300, 0, 800),
    Box("Mass_W_c", "wall", "Wall", -2100, -1900, -1300, -200, 0, 800),
    Box("Mass_E", "wall", "Wall", 1900, 2700, -1300, 1300, 0, 800),
]

# atrium ring walls (inner +-1200, outer +-1300, 1600 high): ground arches 400 x 300 on the mid-sides; level-1 openings 300 x 300
# (landings, 320 sill); the west window 400 x 380 (z 320..700) above the west arch; the shrine window 400 x 360 (z 640..1000) in the south wall
BOXES += _holed_wall("At_N", "x", -1300, 1300, 1200, 1300, 1600, [(-200, 200, 0, 300), (-1200, -900, 320, 620)])
BOXES += _holed_wall("At_S", "x", -1300, 1300, -1300, -1200, 1600, [(-200, 200, 0, 300), (-500, -100, 640, 1000)])
BOXES += _holed_wall("At_E", "y", -1200, 1200, 1200, 1300, 1600, [(-200, 200, 0, 300), (900, 1200, 320, 620), (-1200, -900, 320, 620)])
BOXES += _holed_wall("At_W", "y", -1200, 1200, -1300, -1200, 1600, [(-200, 200, 0, 300), (-200, 200, 320, 700)])

# chapel walls: ground doors 400 x 300 (always under the ledge), level-1 openings 300 x 300 above them where a landing joins
BOXES += _holed_wall("Ch1_S", "x", -2700, -1400, 1300, 1400, 800, [(-1800, -1400, 0, 300)])                               # W aisle <-> Red Chapel
BOXES += _holed_wall("Ch1_E", "y", 1300, 1900, -1400, -1300, 800, [(1500, 1900, 0, 300), (1500, 1800, 320, 620)])         # N aisle <-> Red Chapel, NW landing <-> RA ledge
BOXES += _holed_wall("Ch2_S", "x", 1400, 2700, 1300, 1400, 800, [(1400, 1800, 0, 300), (1400, 1800, 320, 620)])           # E aisle <-> Armoury, NE landing window (drop only)
BOXES += _holed_wall("Ch2_W", "y", 1300, 1900, 1300, 1400, 800, [(1500, 1900, 0, 300)])                                  # N aisle <-> Armoury
BOXES += _holed_wall("Ch3_N", "x", 1400, 2700, -1400, -1300, 800, [(1400, 1800, 0, 300), (1500, 1800, 320, 620)])         # E aisle <-> Mega Chapel, SE landing <-> Mega ledge
BOXES += _holed_wall("Ch3_W", "y", -1900, -1300, 1300, 1400, 1200, [(-1800, -1400, 0, 300)])                              # S aisle <-> Mega Chapel
BOXES += _holed_wall("Ch4_E", "y", -1900, -1300, -1400, -1300, 1200, [(-1800, -1400, 0, 300)])                            # S aisle <-> Crypt
BOXES += _holed_wall("Ch4_N", "x", -2700, -1400, -1400, -1300, 480, [(-1800, -1400, 0, 300)])                             # W aisle <-> Crypt

# Crypt pillars (100 x 100 to the 480 ceiling) and the 12 arcade columns (100 x 100 x 300 under the gallery edge; none on the arch axes, D1)
BOXES += [Box(f"Pillar_{i}", "wall", "Wall", cx - 50, cx + 50, cy - 50, cy + 50, 0, 480)
          for i, (cx, cy) in enumerate([(-2350, -2350), (-1750, -2350), (-2350, -1750), (-1750, -1750)])]
_COL = ((-800, -800), (-800, -400), (-800, 400), (-800, 800), (-400, -800), (-400, 800), (400, -800), (400, 800),
        (800, -800), (800, -400), (800, 400), (800, 800))
BOXES += [Box(f"Col_{i}", "wall", "Wall", cx - 50, cx + 50, cy - 50, cy + 50, 0, 300) for i, (cx, cy) in enumerate(_COL)]

# Quad dais (400 x 400, top +40) with one 20 cm step ring (500 x 500)
BOXES += [Box("Dais_Step", "platform", "Platform", -250, 250, -250, 250, 0, 20),
          Box("Dais", "platform", "Platform", -200, 200, -200, 200, 0, 40)]

# decks (bridges 300..320; the loft 620..640)
BOXES += [
    Box("Gal_N", "bridge", "Platform", -1200, 1200, 800, 1200, 300, 320),
    Box("Gal_S", "bridge", "Platform", -1200, 1200, -1200, -800, 300, 320),
    Box("Gal_W", "bridge", "Platform", -1200, -800, -800, 800, 300, 320),
    Box("Gal_E", "bridge", "Platform", 800, 1200, -800, 800, 300, 320),
    Box("Loft", "bridge", "Platform", -1200, 1200, -1200, -800, 620, 640),
    Box("Land_NW", "bridge", "Platform", -1300, -900, 1300, 1900, 300, 320),
    Box("Land_NE", "bridge", "Platform", 1300, 1900, 900, 1300, 300, 320),
    Box("Land_SE", "bridge", "Platform", 1300, 1900, -1300, -900, 300, 320),
    Box("Land_NE_Sill", "bridge", "Platform", 1400, 1800, 1300, 1500, 300, 320),      # walkable sill of the Armoury window (100 cm into the Armoury, D7)
    Box("RA_Ledge", "bridge", "Platform", -1800, -1400, 1400, 2700, 300, 320),
    Box("Mega_Ledge", "bridge", "Platform", 1400, 2700, -1800, -1400, 300, 320),
]

# ---------------------------------------------------------------- walls (the Wall record is not used: see _holed_wall)
WALLS = []

STAIRS = [
    Stairs("RA_Stairs", -2600, +1, 2300, 2700, 16),          # rises east to the RA ledge (x -1800), 16 x 20 cm
]

RAMPS = [
    Ramp("E_Ramp", -294.2, 1600, 1900, 320.0, dir="+y"),      # 15 deg, foot (y -294.2, z 0) to (y 900.0, z 320) flush with the NE landing
]

# ---------------------------------------------------------------- jump pads (AimAtTarget: the target is the apex, ADR 0002)
PADS = [
    Pad("PadQuad", 0, 400, (0, -520, 900), (0, -960, 640)),            # Quad Flight: over the dais onto the loft
    Pad("PadSouth", -300, -1850, (-300, -1250, 850), (-300, -996, 640)),  # South Shrine: through the shrine window onto the loft
    Pad("PadWest", -2000, 0, (-1250, 0, 440), (-974, 0, 320)),         # West Shrine (niche): through the west window onto the west gallery
    Pad("PadMega", 1700, -2300, (1700, -1780, 420), (1700, -1628, 320)),  # Mega pad: onto the Mega ledge
]

# ---------------------------------------------------------------- items
Q = "/Game/Items/DA_Item_"


def _it(i, name, x, y, z=0.0, dis=()):
    return Item(i, Q + name.replace(".", "_"), x, y, z, tuple(dis))


def stair_z(x_from, x_dir, x):
    """Top of the stair step whose tread contains x."""
    s = int(math.floor(abs(x - x_from) / TREAD))
    return (s + 1) * RISER


def ramp_z(r, y):
    return (y - r.x0) * math.tan(math.radians(RAMP_DEG))


_R = RAMPS[0]
_RA_S = STAIRS[0]
ITEMS = [
    _it("Quad", "Powerup.Quad", 0, 0, 40, (DUEL,)),
    _it("RA", "Armor.Red", -1600, 2100, 320),
    _it("Mega", "Health.Mega", 2550, -1600, 320),
    _it("YA_E", "Armor.Yellow", 1000, 0, 320),
    _it("YA_SW", "Armor.Yellow", -2050, -2050),
    _it("RG", "Weapon.Railgun", -900, -1000, 640),
    _it("RL", "Weapon.RocketLauncher", 2050, 2050),
    _it("LG", "Weapon.LightningGun", -1600, 400),
    _it("PG", "Weapon.PlasmaGun", 1450, -100),
    _it("SG_N", "Weapon.Shotgun", 0, 1600),
    _it("SG_S", "Weapon.Shotgun", 0, -1600),
    _it("SlugsAmmo", "Ammo.Slugs", 0, -1000, 320),
    _it("RocketsAmmo", "Ammo.Rockets", 1100, 1600),
    _it("LightningAmmo", "Ammo.Lightning", -1600, -700),
    _it("CellsAmmo", "Ammo.Cells", 1450, -1000),
    _it("ShellsAmmo", "Ammo.Shells", -1100, 1600),
    _it("BulletsAmmo", "Ammo.Bullets", -2500, -1700),
    _it("H50", "Health.Large", 0, 1000, 320),
    _it("H25_Red", "Health.Medium", -2400, 1700),
    _it("H25_Arm", "Health.Medium", 2500, 2500),
    _it("H25_E", "Health.Medium", 1450, 500),
    _it("H25_S", "Health.Medium", 1000, -1600),
    _it("H25_W", "Health.Medium", -1000, -600),
    _it("H25_Crypt", "Health.Medium", -2500, -2500),
] + [_it(f"H5_Ramp{i}", "Health.Small", 1750, y, ramp_z(_R, y)) for i, y in enumerate((0, 250, 500, 750), 1)] + [
    _it("H5_N1", "Health.Small", 300, 1450), _it("H5_N2", "Health.Small", 500, 1750),
    _it("H5_N3", "Health.Small", 700, 1450), _it("H5_N4", "Health.Small", 900, 1750),
] + [_it(f"Shard_W{i}", "Armor.Shard", -1000, y, 320) for i, y in enumerate((400, 600, 800), 1)] + [
    _it(f"Shard_RA{i}", "Armor.Shard", x, 2500, stair_z(_RA_S.x_from, _RA_S.x_dir, x)) for i, x in enumerate((-2475, -2225, -1975), 1)
]
WEAPON_IDS = ("RG", "RL", "LG", "PG", "SG_N", "SG_S")
MEGA_ID, RA_ID = "Mega", "RA"
AMMO_FOR = {"PG": "CellsAmmo", "SG_N": "ShellsAmmo", "LG": "LightningAmmo", "RL": "RocketsAmmo", "RG": "SlugsAmmo"}   # SG_S has no box of its own
AMMO_MIN_CM, AMMO_MAX_CM = 600.0, 1200.0

# ---------------------------------------------------------------- spawns (9): none sees Mega or RA, >= 10 m apart, <= 8 m (horizontal) from a weapon
SPAWNS = [
    Spawn("S1", 2500, 2300, 225),      # Armoury corner, RL
    Spawn("S2", 780, 1600, 180),       # North aisle east, SG_N
    Spawn("S3", -780, 1600, 0),        # North aisle west, SG_N
    Spawn("S4", -1600, 1000, 270),     # West aisle north, LG (the RA ledge deck cuts the line to the RA)
    Spawn("S5", -1600, -350, 270),     # West aisle south, LG
    Spawn("S6", -780, -1600, 0),       # South aisle west, SG_S
    Spawn("S7", 780, -1600, 180),      # South aisle east, SG_S
    Spawn("S8", 1450, -700, 90),       # East aisle, PG
    Spawn("S9", 0, -950, 90),          # south arcade under the gallery and the loft, faces the dais
]
SPAWN_MIN_APART = 1000.0
SPAWN_MAX_WEAPON = 800.0

# ---------------------------------------------------------------- nav links (ADR 0005 section 4; concept section 14: 22 links)
DROP_IN, DROP_OUT_TALL = 60.0, 350.0


def _drop(i, sx, sy, sz, ex, ey, ez):
    return Link(i, "DROP", (sx, sy, sz), (ex, ey, ez), None)


LINKS = [Link(f"PadLink_{p.id[3:]}", "JUMP_PAD", (p.x, p.y, 0), p.land, p.id) for p in PADS] + [
    Link("RJ_West", "ROCKET_JUMP", (-1000, -350, 320), (-1000, -1000, 640), None),   # run south 450 cm to the loft edge (y -800), land 200 cm onto it
    Link("RJ_East", "ROCKET_JUMP", (1000, -350, 320), (1000, -1000, 640), None),
    # gallery -> well x 8 (inner edge +-800; ends 350 cm out, >= 400 from the Quad Flight pad (0, 400), >= 200 from the dais)
    _drop("Drop_GalW_N", -800 - DROP_IN, 500, 320, -800 + DROP_OUT_TALL, 500, 0),
    _drop("Drop_GalW_S", -800 - DROP_IN, -500, 320, -800 + DROP_OUT_TALL, -500, 0),
    _drop("Drop_GalE_N", 800 + DROP_IN, 500, 320, 800 - DROP_OUT_TALL, 500, 0),
    _drop("Drop_GalE_S", 800 + DROP_IN, -500, 320, 800 - DROP_OUT_TALL, -500, 0),
    _drop("Drop_GalN_W", -500, 800 + DROP_IN, 320, -500, 800 - DROP_OUT_TALL, 0),
    _drop("Drop_GalN_E", 500, 800 + DROP_IN, 320, 500, 800 - DROP_OUT_TALL, 0),
    _drop("Drop_GalS_W", -500, -800 - DROP_IN, 320, -500, -800 + DROP_OUT_TALL, 0),
    _drop("Drop_GalS_E", 500, -800 - DROP_IN, 320, 500, -800 + DROP_OUT_TALL, 0),
    # loft -> west / east gallery (loft north edge y -800)
    _drop("Drop_Loft_W", -1000, -800 - DROP_IN, 640, -1000, -800 + DROP_OUT_TALL, 320),
    _drop("Drop_Loft_E", 1000, -800 - DROP_IN, 640, 1000, -800 + DROP_OUT_TALL, 320),
    # ledges -> chapel floors
    _drop("Drop_RALedge", -1800 + DROP_IN, 2000, 320, -1800 - DROP_OUT_TALL, 2000, 0),
    _drop("Drop_MegaLedge", 2500, -1800 + DROP_IN, 320, 2500, -1800 - DROP_OUT_TALL, 0),
    # corner landings -> aisle floors, NE landing -> Armoury through the window
    _drop("Drop_LandNW", -900 - DROP_IN, 1600, 320, -900 + DROP_OUT_TALL, 1600, 0),
    _drop("Drop_LandNE", 1450, 900 + DROP_IN, 320, 1450, 900 - DROP_OUT_TALL, 0),
    _drop("Drop_LandSE", 1650, -900 - DROP_IN, 320, 1650, -900 + DROP_OUT_TALL, 0),
    _drop("Drop_LandNE_Arm", 1600, 1500 - DROP_IN, 320, 1600, 1500 + DROP_OUT_TALL, 0),
]

# ---------------------------------------------------------------- art-mode tables (task A1; empty until the art pass)
LIGHTS = []
CAPTURES = ()
LIGHTMASS_VOLUME = ((0.0, 0.0, 800.0), (5800.0, 5800.0, 1800.0))

# ---------------------------------------------------------------- clip: perimeter and roof come from CLIP_MODE; a ceiling slab per room class
CLIPS = [
    Clip("Ceil_Atrium", -1300, 1300, -1300, 1300, 1600, 1700),
    Clip("Ceil_N", -1300, 1300, 1300, 2800, 800, 900),
    Clip("Ceil_S", -1300, 1300, -2800, -1300, 1200, 1300),
    Clip("Ceil_W", -2800, -1300, -1300, 1300, 800, 900),
    Clip("Ceil_E", 1300, 2800, -1300, 1300, 800, 900),
    Clip("Ceil_NW", -2800, -1300, 1300, 2800, 800, 900),
    Clip("Ceil_NE", 1300, 2800, 1300, 2800, 800, 900),
    Clip("Ceil_SE", 1300, 2800, -2800, -1300, 800, 900),
    Clip("Ceil_SW", -2800, -1300, -2800, -1300, 480, 580),
]

NAV_CENTER = (0.0, 0.0, 600.0)
NAV_SIZE = (5800.0, 5800.0, 1800.0)      # X, Y -2900..2900, Z -300..1500

INTERMISSION_CAMERA = ((0.0, -1800.0, 3200.0), (0.0, -60.0, 90.0))   # location, (roll, pitch, yaw)
INTERMISSION_LABEL = (0.0, 0.0, 2300.0)

UPPER_ROUTE_MIN_M, UPPER_ROUTE_MAX_M = 65.0, 70.0      # RA -> Mega on the upper route (concept section 6; measured in the task notes)

# Per-kind actor counts that do not depend on the geometry expansion (the geometry kinds are counted by the generator).
EXPECTED = {
    "pickup": 38, "playerstart": 9, "jumppad": 4, "targetpoint": 4, "navbounds": 1, "navlink": 22,
    "camera": 1, "text": 1, "light": 2,
}

# Concept section 10: the loft (eye on z 640) must not see the aisle floors through the ground arches; arcade slivers are tolerated (D8): measured 9.33 % of 75 pairs (the N-aisle axis and the E/W arch strips), tolerance 10 %.
POINTS = {
    "LoftEyes": [(x, -1000) + (640.0,) for x in (-1000, -500, 0, 500, 1000)],
    "AisleFloors": [(x, 1500) for x in (-1000, -500, 0, 500, 1000)] + [(-1600, y) for y in (-1000, -500, 0, 500, 1000)]
                   + [(1600, y) for y in (-1000, -500, 0, 500, 1000)],
}
LOS_BLOCKED = [("spawns", "majors"), ("LoftEyes", "AisleFloors", 0.10)]
FREE_EDGE_EXEMPT = ()

# (was a TODO for P4): LOS_BLOCKED = [(loft eye, every ground-arch floor sample), ("spawns", "Mega"), ("spawns", "RA")] with POINTS for the
# loft samples (concept section 10 / ADR 0008 1.2); measured by hand in the task notes until mapgen.los exists.
