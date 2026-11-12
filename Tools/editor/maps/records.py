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
