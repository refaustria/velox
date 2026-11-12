"""Record types shared by every map layout table (ADR 0008 section 1.2, task M7 P2). Data only, no `unreal` import.

Moved verbatim from maps/dm1_layout.py; each `maps/<slug>_layout.py` imports them and fills the lists.
"""
from collections import namedtuple

Room = namedtuple("Room", "id name x0 x1 y0 y1 floor_z ceiling cls")          # ceiling = clear height above floor_z; cls: atrium | room | corridor
Box = namedtuple("Box", "id kind mat x0 x1 y0 y1 z0 z1")                       # axis aligned; kind drives the collision / nav policy
Opening = namedtuple("Opening", "o0 o1 zlo zhi")                               # along the wall's run axis; zlo > 0: sill below, zhi < top: lintel above
Wall = namedtuple("Wall", "id run s0 s1 b0 b1 top openings")                   # run 'x': spans x s0..s1, band y b0..b1; run 'y': the converse
# Stairs: step s has tread `tread` (default TREAD) along x_dir, top z0 + (s+1)*riser (riser default RISER, z0 default FLOOR_Z).
# axis "y" swaps the roles: x_from is a Y value, x_dir runs along Y and y0..y1 is an X band. Read through mapgen.stairs_frame().
Stairs = namedtuple("Stairs", "id x_from x_dir y0 y1 steps axis z0 tread riser", defaults=("x", None, None, None))
# Ramp: `dir` is the uphill direction (+x -x +y -y), x0 the foot coordinate along the run axis, y0..y1 the band across it, `rise` the
# height gained, `deg` defaults to RAMP_DEG, `z0` (foot height) to FLOOR_Z; fill "solid" = stepped fill boxes down to z0, "none" = free slab.
# Read through mapgen.ramp_frame(). For a DM1 ramp (dir +x, z0 = FLOOR_Z = 0) `rise` is also the top height.
Ramp = namedtuple("Ramp", "id x0 y0 y1 rise dir deg z0 fill", defaults=("+x", None, None, "solid"))
Pad = namedtuple("Pad", "id x y target land")                                  # target = AimAtTarget apex point; land = authored landing (link end)
Item = namedtuple("Item", "id definition x y z disabled_in_modes")
Spawn = namedtuple("Spawn", "id x y yaw")
Link = namedtuple("Link", "id type start end pad")                              # type: JUMP_PAD | ROCKET_JUMP | DROP | JUMP | JUMP_FAST
Light = namedtuple("Light", "id kind x y z lumens rgb radius ceiling")        # art mode only (task 15): Static point light; `ceiling` = z of the ceiling underside the Light_Fixture hangs from (None: no fixture)
Clip = namedtuple("Clip", "id x0 x1 y0 y1 z0 z1")                               # invisible BlockAll, no nav influence

# ---- M7 P4 (ADR 0008 sections 1.3, 1.4)
# Oct: a regular octagon of across-flats `d`, spawned as FOUR boxes d x d*tan(22.5) x (z1-z0) at yaw 0/45/90/135 (veloxmap.octagon()). In the
# pure-Python checks it is a vertical cylinder: radius d/2 where it must block (line of sight), OCT_CLEAR_FACTOR * d where it must be cleared
# (pad arcs, gaps). `kit` names the single art-mode mesh that replaces it. The spec module lists them in `OCTS`.
Oct = namedtuple("Oct", "id kind mat cx cy d z0 z1 kit", defaults=(None,))
# Hazard: lethal / damaging world space (AVeloxHazardVolume, ADR 0008 section 3); type in HAZARD_TYPES. The spec module lists them in `HAZARDS`.
# A "lava" box top is 20-60 cm below the adjoining floor top and sits on a Box of kind "hazard" (the bed: BlockAll, no nav influence).
Hazard = namedtuple("Hazard", "id type x0 x1 y0 y1 z0 z1")

HAZARD_TYPES = ("lava", "void")
NONWALKABLE_KINDS = ("hazard", "lintel")              # box kinds that are never a standing surface
OCT_CLEAR_FACTOR = 1.0 / (2.0 * 0.9238795325112867)   # circumradius / across-flats = 1 / (2 cos 22.5 deg) = 0.5412
PAD_MAX_LAUNCH_SPEED = 4000.0                         # = veloxmap.PAD_MAX_LAUNCH_SPEED (ADR 0002 section 7)

# Every constant validate_layout() reads as a tunable. A spec module sets `RULES = {...}` (merged over this by mapgen.rules(L); unknown keys
# are an error) and comments each override with its concept section. Up to "pad_land_tol_z" it is what the validator used before M7.
DEFAULT_RULES = {
    "walkable_width": 120.0,        # platforms, bridges, ledges
    "door_min": 240.0, "door_max": 400.0,
    "ramp_max_deg": 45.5,           # walkable floor angle
    "pad_zone": 300.0,              # landing zone edge
    "pad_land_tol": 60.0,           # simulated landing vs authored landing (xy)
    "pad_land_tol_z": 30.0,
    # M7 P4
    "pad_arc_clearance": 0.0,       # cm between the feet arc and every solid (before M7 the arc only had to hit nothing; Culmen 60)
    "pad_max_launch_speed": PAD_MAX_LAUNCH_SPEED,
    "link_end_ground": True,        # hard rule: every Link start and end stands on ground (see LEGACY_RULES)
    "drop_rule": True,              # ADR 0006 E3 (d) for DROP ends (see LEGACY_RULES)
    "link_end_inset": 45.0,         # agent radius: a link end lies this far inside the edge of the top it stands on
    "link_end_z_tol": 5.0,
    "drop_clear": 300.0,            # E3 (d): clear top around a DROP end (square edge)
    "drop_wall_ahead": 500.0,       # ... to the wall ahead
    "drop_pad_dist": 300.0,         # ... from any pad trigger
    "oct_gap_min": 120.0,           # a gap between an Oct (circumcircle) and another Oct / vertical solid is <= 0 (touching) or >= this
    "pad_count": None,              # exact number of Pad records (DM1 3); None = not checked
    "link_counts": None,            # {"JUMP_PAD": n, ...}: exact Link count per type for the types named (DM1 {JUMP_PAD 3, ROCKET_JUMP 2})
    "item_mix": None,               # {definition suffix after "DA_Item_": count}: the exact item mix (DM1: ADR 0006 section 3)
    "free_edge_drop": 45.0,         # report only
    "free_edge_report": False,      # True: validate_layout adds the free-edge figures (CLI --free-edges prints them anyway)
}

# DM1 is frozen (golden, committed .umap, ADR 0008 1.5) and predates two of the new rules: RJ_West / RJ_East start at x = +-650, which is the
# 100 cm gap between the walkway and the dock, and six drops end less than 300 cm from a pad or without 300 x 300 of clear top. Gated here by
# map id rather than weakened for everyone; applied by mapgen.rules() under the spec module's own RULES.
DM1_ITEM_MIX = {"Health_Mega": 1, "Armor_Red": 1, "Armor_Yellow": 2, "Armor_Shard": 6, "Health_Large": 1, "Health_Medium": 5,
                "Health_Small": 8, "Weapon_RocketLauncher": 1, "Weapon_Railgun": 1, "Weapon_LightningGun": 1, "Weapon_Shotgun": 1,
                "Weapon_PlasmaGun": 1, "Ammo_Rockets": 1, "Ammo_Slugs": 1, "Ammo_Lightning": 1, "Ammo_Shells": 1, "Ammo_Cells": 1,
                "Powerup_Quad": 1}
LEGACY_RULES = {"DM1": {"link_end_ground": False, "drop_rule": False, "pad_count": 3, "link_counts": {"JUMP_PAD": 3, "ROCKET_JUMP": 2},
                        "item_mix": DM1_ITEM_MIX}}
