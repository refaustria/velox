"""Velox weapon and item meshes (ADR 0006 section 4.6, task 13; E0.5: Blender 4.3.2 headless on the build VM).

Six low-poly weapons (world model == view model) and the pickup meshes, flat-shaded, one shared 16 x 1 palette texture.

  blender -b --factory-startup --python Tools/blender/weapons_items.py -- \
      --out ArtSource/Export --tex ArtSource/Textures --blend ArtSource/Weapons/weapons.blend [--preview <dir>]

Conventions (docs/art/weapons-spec.md):
  * Modelled in centimetres in Unreal axes (X forward, Y right, Z up); Y is mirrored in make_object and the export axes are the
    kit's (Tools/blender/kit.py), so the imported mesh has exactly the bounds the manifest states.
  * Weapons: grip origin at (0, 0, 0), barrel along +X, an empty `SOCKET_muzzle` parented to the mesh at the barrel tip (Unreal's FBX
    importer turns a `SOCKET_` child null of a static mesh into the socket `muzzle`).
  * Items: origin at the base centre, height <= 60 cm.
  * UV0 only: every face samples one palette texel (T_Items_Palette), so one material serves all meshes. UV1 is not generated
    (dynamic objects take no lightmaps).
Deterministic: no timestamps, no random numbers. `python3 -I Tools/blender/weapons_items.py` (outside Blender) runs the geometry
checks only.
"""
import json
import math
import os
import struct
import sys
import zlib

import numpy as np

try:
    import bpy
except ImportError:
    bpy = None

MAX_WEAPON_TRIS = 1500
MAX_ITEM_TRIS = 400
MAX_ITEM_HEIGHT = 60.0
PALETTE_SIZE = 16

# index -> (name, sRGB 0-255). Item colours follow create_item_content.py (health green/yellow/orange/blue, armour yellow/red,
# ammo = weapon colour); the style guide wants saturated, distinct hues (ADR 0006 section 4.3).
PALETTE = [
    ("steel_dark", (62, 66, 78)), ("steel_mid", (110, 115, 125)), ("steel_light", (180, 185, 195)), ("white", (245, 245, 245)),
    ("red", (240, 40, 40)), ("orange", (255, 150, 20)), ("yellow", (255, 220, 40)), ("green", (40, 220, 40)),
    ("mint", (60, 240, 160)), ("cyan", (40, 230, 255)), ("blue", (60, 120, 255)), ("purple", (160, 80, 255)),
    ("pale_yellow", (240, 240, 120)), ("pale_blue", (220, 240, 255)), ("dark_red", (150, 20, 20)), ("brass", (170, 130, 60)),
]
C = {name: i for i, (name, _rgb) in enumerate(PALETTE)}


# ---------------------------------------------------------------- geometry builder
class Builder:
    """Vertex-welded polygon soup with one palette index per face. Solids are convex, so a face is wound outward by comparing its
    normal with (face centre - solid centre)."""

    def __init__(self):
        self.verts = []
        self._index = {}
        self.faces = []  # (tuple of vertex indices, colour index)

    def _v(self, p):
        key = (round(float(p[0]), 3), round(float(p[1]), 3), round(float(p[2]), 3))
        if key not in self._index:
            self._index[key] = len(self.verts)
            self.verts.append(key)
        return self._index[key]

    def poly(self, pts, color, centre):
        pts = [np.array(p, float) for p in pts]
        n = np.zeros(3)
        for i in range(len(pts)):  # Newell normal
            a, b = pts[i], pts[(i + 1) % len(pts)]
            n += np.array([(a[1] - b[1]) * (a[2] + b[2]), (a[2] - b[2]) * (a[0] + b[0]), (a[0] - b[0]) * (a[1] + b[1])])
        assert np.linalg.norm(n) > 1e-6, "degenerate face"
        if np.dot(n, np.mean(pts, axis=0) - np.array(centre, float)) < 0:
            pts.reverse()
        self.faces.append((tuple(self._v(p) for p in pts), color))

    def tri_count(self):
        return sum(len(f) - 2 for f, _c in self.faces)

    def bounds(self):
        a = np.array(self.verts)
        return a.min(axis=0), a.max(axis=0)

    # ---- primitives
    def box(self, lo, hi, color):
        x0, y0, z0 = lo
        x1, y1, z1 = hi
        c = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
        for quad in (
            [(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)], [(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)],
            [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], [(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)],
            [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)], [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)],
        ):
            self.poly(quad, color, c)

    def tube(self, p0, p1, r0, r1, sides, color, cap_color=None, offset=0.0):
        """Frustum from p0 (radius r0) to p1 (radius r1); a radius of 0 makes a cone tip. `color` may be a list: side faces cycle."""
        p0, p1 = np.array(p0, float), np.array(p1, float)
        axis = p1 - p0
        axis /= np.linalg.norm(axis)
        helper = np.array([0, 0, 1.0]) if abs(axis[2]) < 0.9 else np.array([0, 1.0, 0])
        u = np.cross(axis, helper)
        u /= np.linalg.norm(u)
        v = np.cross(axis, u)
        centre = (p0 + p1) / 2
        colors = color if isinstance(color, (list, tuple)) else [color]
        if cap_color is None:
            cap_color = colors[0]
        ang = [offset + 2 * math.pi * i / sides for i in range(sides)]
        ring = lambda p, r: [p + r * (math.cos(a) * u + math.sin(a) * v) for a in ang]
        a_ring, b_ring = ring(p0, r0), ring(p1, r1)
        for i in range(sides):
            j = (i + 1) % sides
            col = colors[i % len(colors)]
            if r1 <= 0:
                self.poly([a_ring[i], a_ring[j], p1], col, centre)
            elif r0 <= 0:
                self.poly([p0, b_ring[j], b_ring[i]], col, centre)
            else:
                self.poly([a_ring[i], a_ring[j], b_ring[j], b_ring[i]], col, centre)
        if r0 > 0:
            self.poly(a_ring, cap_color, centre)
        if r1 > 0:
            self.poly(b_ring, cap_color, centre)

    def sphere(self, c, r, rings, segs, color, squash=1.0):
        """Low-poly UV sphere (`rings` latitude bands, pole to pole; rings=2 is a bipyramid); squash scales the Z radius."""
        c = np.array(c, float)
        colors = color if isinstance(color, (list, tuple)) else [color]

        def pt(k, i):
            th, ph = math.pi * k / rings, 2 * math.pi * i / segs
            return c + np.array([r * math.sin(th) * math.cos(ph), r * math.sin(th) * math.sin(ph), r * squash * math.cos(th)])

        for k in range(rings):
            for i in range(segs):
                j = (i + 1) % segs
                col = colors[(i + k) % len(colors)]
                if k == 0:
                    self.poly([pt(0, 0), pt(1, j), pt(1, i)], col, c)
                elif k == rings - 1:
                    self.poly([pt(k, i), pt(k, j), pt(rings, 0)], col, c)
                else:
                    self.poly([pt(k, i), pt(k, j), pt(k + 1, j), pt(k + 1, i)], col, c)

    def extrude_xz(self, pts, y0, y1, color):
        """Convex polygon given as (x, z) points, extruded along Y from y0 to y1."""
        centre = (sum(p[0] for p in pts) / len(pts), (y0 + y1) / 2, sum(p[1] for p in pts) / len(pts))
        n = len(pts)
        for i in range(n):
            a, b = pts[i], pts[(i + 1) % n]
            self.poly([(a[0], y0, a[1]), (b[0], y0, b[1]), (b[0], y1, b[1]), (a[0], y1, a[1])], color, centre)
        self.poly([(p[0], y0, p[1]) for p in pts], color, centre)
        self.poly([(p[0], y1, p[1]) for p in pts], color, centre)

    def extrude_yz(self, x0, x1, pts, color):
        """Convex polygon in (y, z), extruded along X from x0 to x1 (fins)."""
        centre = ((x0 + x1) / 2, sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
        n = len(pts)
        for i in range(n):
            a, b = pts[i], pts[(i + 1) % n]
            self.poly([(x0, a[0], a[1]), (x0, b[0], b[1]), (x1, b[0], b[1]), (x1, a[0], a[1])], color, centre)
        self.poly([(x0, p[0], p[1]) for p in pts], color, centre)
        self.poly([(x1, p[0], p[1]) for p in pts], color, centre)


# ---------------------------------------------------------------- weapons: grip at the origin, barrel along +X
def grip(b):
    b.box((-3.5, -2.2, -15), (2.5, 2.2, 1), C["steel_dark"])


def w_machinegun(b):
    """Short barrel + a big drum magazine and a stock."""
    grip(b)
    b.box((-10, -3, 0), (18, 3, 9), C["steel_mid"])
    b.box((-6, -1, 9), (14, 1, 11), C["steel_dark"])                      # rail
    b.box((-20, -2, 1), (-10, 2, 7), C["steel_dark"])                     # stock
    b.tube((18, 0, 5.5), (32, 0, 5.5), 2.9, 2.9, 6, C["steel_light"])      # shroud
    b.tube((32, 0, 5.5), (46, 0, 5.5), 1.5, 1.5, 6, C["steel_dark"])       # barrel
    b.tube((43, 0, 5.5), (47, 0, 5.5), 2.3, 2.3, 6, C["steel_dark"])       # muzzle brake
    b.tube((6, -4.2, -5), (6, 4.2, -5), 7.5, 7.5, 8, C["pale_yellow"], cap_color=C["steel_dark"], offset=math.pi / 8)  # drum
    return (47.0, 0.0, 5.5)


def w_shotgun(b):
    """Two parallel barrels, a pump forend, a brass stock."""
    grip(b)
    b.box((-12, -3.4, 0), (10, 3.4, 8), C["steel_mid"])
    b.box((-26, -2, -1), (-12, 2, 8), C["brass"])                          # stock
    b.tube((6, -2.4, 5), (58, -2.4, 5), 2.3, 2.3, 6, C["steel_dark"])       # left barrel
    b.tube((6, 2.4, 5), (58, 2.4, 5), 2.3, 2.3, 6, C["steel_dark"])         # right barrel
    b.box((22, -4.2, -3), (42, 4.2, 2.5), C["orange"])                     # pump forend
    b.box((54, -4, 7), (58, 4, 8.5), C["steel_light"])                     # front sight bar
    return (58.0, 0.0, 5.0)


def w_plasmagun(b):
    """Coil rings along a core and a glowing sphere emitter."""
    grip(b)
    b.box((-10, -3.5, -1), (12, 3.5, 9), C["steel_mid"])
    b.box((-10, -3.5, 9), (4, 3.5, 12), C["steel_dark"])
    b.tube((12, 0, 4), (40, 0, 4), 2.2, 2.2, 6, C["pale_blue"])            # core
    for x in (15, 22, 29):
        b.tube((x, 0, 4), (x + 3.5, 0, 4), 6.0, 6.0, 8, C["purple"], cap_color=C["steel_dark"], offset=math.pi / 8)  # coil rings
    b.sphere((44, 0, 4), 5.5, 4, 8, [C["pale_blue"], C["cyan"]])            # emitter
    return (49.5, 0.0, 4.0)


def w_rocketlauncher(b):
    """Fat tube with a flared rear and a front ring, sight box on top."""
    grip(b)
    b.box((8, -3, -9), (22, 3, -1), C["steel_dark"])                       # forward handle
    b.tube((-12, 0, 6), (-4, 0, 6), 7.8, 5.8, 8, C["steel_mid"], offset=math.pi / 8)   # rear flare
    b.tube((-4, 0, 6), (54, 0, 6), 5.8, 5.8, 8, [C["steel_mid"], C["steel_light"]], cap_color=C["steel_dark"], offset=math.pi / 8)  # tube
    b.tube((46, 0, 6), (58, 0, 6), 6.6, 6.6, 8, C["red"], cap_color=C["steel_dark"], offset=math.pi / 8)   # front band
    b.tube((-2, 0, 6), (4, 0, 6), 6.6, 6.6, 8, C["red"], offset=math.pi / 8)       # rear band
    b.box((10, -2, 11.8), (24, 2, 16), C["steel_dark"])                    # sight box
    return (58.0, 0.0, 6.0)


def w_lightninggun(b):
    """Open frame: two converging prongs around a glowing core rod."""
    grip(b)
    b.box((-10, -3.5, 0), (12, 3.5, 9), C["steel_mid"])
    b.box((-16, -2, 2), (-10, 2, 8), C["steel_dark"])
    for s in (-1, 1):
        b.tube((10, s * 7, 4.5), (40, s * 3.2, 4.5), 1.7, 1.2, 4, C["steel_light"], offset=math.pi / 4)   # prong
        b.tube((40, s * 3.2, 4.5), (46, s * 2.2, 4.5), 1.2, 0, 4, C["pale_blue"], offset=math.pi / 4)     # prong tip
    b.tube((12, 0, 4.5), (38, 0, 4.5), 1.4, 1.4, 6, C["cyan"])                                              # core rod
    for x in (18, 28):
        half = 7.5 - (x - 10) * 0.14
        b.box((x, -half, 3.4), (x + 1.6, half, 5.6), C["steel_dark"])                                       # cross braces
    return (46.0, 0.0, 4.5)


def w_railgun(b):
    """Long thin rail with ring coils and tail fins."""
    grip(b)
    b.box((-12, -3, 0), (16, 3, 8), C["mint"])
    b.box((-12, -3, 8), (-2, 3, 10), C["steel_dark"])
    b.tube((-6, 0, 4), (74, 0, 4), 1.9, 1.9, 6, C["steel_light"])            # rail
    for x in (22, 38, 54):
        b.tube((x, 0, 4), (x + 3, 0, 4), 4.8, 4.8, 6, C["steel_dark"], cap_color=C["mint"], offset=math.pi / 6)     # coils
    b.box((16, -0.8, -3), (70, 0.8, 4), C["steel_dark"])                     # lower spine
    b.extrude_yz(-12, -2, [(-0.8, 9), (0.8, 9), (0.8, 17), (-0.8, 13)], C["mint"])    # tail fin (top)
    b.extrude_yz(-12, -2, [(-0.8, -1), (0.8, -1), (0.8, -8), (-0.8, -5)], C["mint"])  # tail fin (bottom)
    return (74.0, 0.0, 4.0)


WEAPONS = [  # file name stem, builder
    ("MachineGun", w_machinegun), ("Shotgun", w_shotgun), ("PlasmaGun", w_plasmagun),
    ("RocketLauncher", w_rocketlauncher), ("LightningGun", w_lightninggun), ("Railgun", w_railgun),
]


# ---------------------------------------------------------------- items: origin at the base centre
def cross_bars(b, half, thick, raise_, z_centre, size, color, top_z=None):
    """A raised plus sign on the four side faces (and the top when top_z is given) of a body of half-width `half`."""
    s, t = size / 2, thick / 2
    for sx in (-1, 1):  # +-X faces: bars in the YZ plane
        x0, x1 = (half, half + raise_) if sx > 0 else (-half - raise_, -half)
        b.box((x0, -s, z_centre - t), (x1, s, z_centre + t), color)
        b.box((x0, -t, z_centre - s), (x1, t, z_centre + s), color)
    for sy in (-1, 1):  # +-Y faces: bars in the XZ plane
        y0, y1 = (half, half + raise_) if sy > 0 else (-half - raise_, -half)
        b.box((-s, y0, z_centre - t), (s, y1, z_centre + t), color)
        b.box((-t, y0, z_centre - s), (t, y1, z_centre + s), color)
    if top_z is not None:
        b.box((-s, -t, top_z), (s, t, top_z + raise_), color)
        b.box((-t, -s, top_z), (t, s, top_z + raise_), color)


def health_cube(side, cross_color):
    def build(b):
        h = side / 2
        b.box((-h, -h, 0), (h, h, side), C["white"])
        cross_bars(b, h, side * 0.22, 0.8, side / 2, side * 0.7, cross_color, top_z=side)
    return build


def health_mega(b):
    r, hgt = 20.0, 38.0
    b.tube((0, 0, 0), (0, 0, hgt), r, r, 8, C["blue"], cap_color=C["white"], offset=math.pi / 8)
    cross_bars(b, r * math.cos(math.pi / 8), 7.0, 0.9, hgt / 2, 26.0, C["white"])  # apothem: the flat faces sit on the axes
    b.box((-13, -3.5, hgt), (13, 3.5, hgt + 0.9), C["white"])
    b.box((-3.5, -13, hgt), (3.5, 13, hgt + 0.9), C["white"])


def armor_shard(b):
    b.sphere((0, 0, 11.4), 6.0, 2, 6, [C["green"], C["mint"]], squash=1.9)   # a stretched hex bipyramid, tip to tip ~22 cm


def armor_shield(scale, outer, inner):
    def build(b):
        s = scale
        b.extrude_xz([(-16 * s, 34 * s), (16 * s, 34 * s), (16 * s, 14 * s), (0, 0), (-16 * s, 14 * s)], -3 * s, 3 * s, outer)
        b.extrude_xz([(-11 * s, 30 * s), (11 * s, 30 * s), (11 * s, 15 * s), (0, 6 * s), (-11 * s, 15 * s)], -4 * s, 4 * s, inner)
    return build


def ammo_box(color, emblem):
    def build(b):
        b.box((-11, -8, 0), (11, 8, 11), color)
        b.box((-11.6, -8.6, 8), (11.6, 8.6, 11.4), C["steel_dark"])        # lid band
        emblem(b, 11.4)
    return build


def em_bullets(b, z):
    for x in (-6, 0, 6):
        b.tube((x, 0, z), (x, 0, z + 9), 1.6, 1.6, 4, C["brass"], offset=math.pi / 4)
        b.tube((x, 0, z + 9), (x, 0, z + 12.5), 1.6, 0, 4, C["steel_light"], offset=math.pi / 4)


def em_shells(b, z):
    for x in (-5, 5):
        b.tube((x, 0, z), (x, 0, z + 11), 3.0, 3.0, 6, C["red"], cap_color=C["brass"])


def em_rockets(b, z):
    b.tube((0, 0, z), (0, 0, z + 12), 3.4, 3.4, 6, C["steel_light"])
    b.tube((0, 0, z + 12), (0, 0, z + 18), 3.4, 0, 6, C["red"])


def em_cells(b, z):
    b.sphere((0, 0, z + 6), 6.0, 4, 8, [C["purple"], C["pale_blue"]])


def em_lightning(b, z):
    b.sphere((0, 0, z + 9), 5.0, 2, 6, [C["pale_blue"], C["cyan"]], squash=1.8)


def em_slugs(b, z):
    b.tube((-4, 0, z), (-4, 0, z + 13), 1.8, 1.8, 6, C["mint"])
    b.tube((4, 0, z), (4, 0, z + 13), 1.8, 1.8, 6, C["mint"])


AMMO = [  # name, box colour, emblem (matches the DA_Item_Ammo_* colours)
    ("Bullets", C["pale_yellow"], em_bullets), ("Shells", C["orange"], em_shells), ("Rockets", C["red"], em_rockets),
    ("Cells", C["purple"], em_cells), ("Lightning", C["pale_blue"], em_lightning), ("Slugs", C["mint"], em_slugs),
]


def powerup(main, light):
    def build(b):
        b.tube((0, 0, 0), (0, 0, 4), 15, 15, 6, C["steel_dark"], cap_color=main)               # base disc
        b.sphere((0, 0, 29.3), 11.0, 2, 6, [main, light], squash=2.3)                              # crystal, 4..54.6 cm
        for k in range(4):
            a = math.pi / 2 * k + math.pi / 4
            x, y = 13 * math.cos(a), 13 * math.sin(a)
            b.tube((x, y, 4), (x, y, 16), 1.8, 0, 4, light, offset=math.pi / 4)                  # corner spikes
    return build


ITEMS = [  # asset stem (SM_Item_<stem>), builder, DA_Item_* asset the mesh is wired to
    ("Health_Small", health_cube(14, C["green"]), "DA_Item_Health_Small"),
    ("Health_Medium", health_cube(22, C["yellow"]), "DA_Item_Health_Medium"),
    ("Health_Large", health_cube(30, C["orange"]), "DA_Item_Health_Large"),
    ("Health_Mega", health_mega, "DA_Item_Health_Mega"),
    ("Armor_Shard", armor_shard, "DA_Item_Armor_Shard"),
    ("Armor_Yellow", armor_shield(1.0, C["yellow"], C["brass"]), "DA_Item_Armor_Yellow"),
    ("Armor_Red", armor_shield(1.3, C["red"], C["dark_red"]), "DA_Item_Armor_Red"),
] + [("Ammo_" + n, ammo_box(col, em), "DA_Item_Ammo_" + n) for n, col, em in AMMO] + [
    ("Powerup_Quad", powerup(C["cyan"], C["pale_blue"]), "DA_Item_Powerup_Quad"),
    ("Powerup_Haste", powerup(C["yellow"], C["pale_yellow"]), "DA_Item_Powerup_Haste"),
    ("Powerup_Regen", powerup(C["red"], C["orange"]), "DA_Item_Powerup_Regen"),
    ("Powerup_BattleSuit", powerup(C["green"], C["mint"]), "DA_Item_Powerup_BattleSuit"),
]


# ---------------------------------------------------------------- checks, texture, Blender objects
def validate_weapon(name, b, muzzle):
    tris = b.tri_count()
    lo, hi = b.bounds()
    assert tris <= MAX_WEAPON_TRIS, f"{name}: {tris} tris"
    assert muzzle[0] >= hi[0] - 1.0, f"{name}: muzzle {muzzle[0]} is not at the barrel tip {hi[0]}"
    assert lo[0] < 0 < hi[0] and lo[2] <= -10, f"{name}: grip origin / barrel direction wrong ({lo}, {hi})"
    assert hi[0] - lo[0] <= 110, f"{name}: too long"
    return tris, lo, hi


def validate_item(name, b):
    tris = b.tri_count()
    lo, hi = b.bounds()
    assert tris <= MAX_ITEM_TRIS, f"{name}: {tris} tris"
    assert abs(lo[2]) < 1e-3, f"{name}: origin not at the base ({lo[2]})"
    assert hi[2] <= MAX_ITEM_HEIGHT + 1e-3, f"{name}: {hi[2]} cm tall"
    return tris, lo, hi


def write_palette(path):
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    row = b"\x00" + b"".join(bytes(rgb) + b"\xff" for _n, rgb in PALETTE)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", PALETTE_SIZE, 1, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(row, 9)))
        f.write(chunk(b"IEND", b""))


def make_object(name, b, palette_mat, muzzle=None):
    """Mirror Y (Blender +Y exports to Unreal -Y) and reverse every face so the normals stay outward."""
    me = bpy.data.meshes.new(name)
    me.from_pydata([(v[0], -v[1], v[2]) for v in b.verts], [], [tuple(reversed(f)) for f, _c in b.faces])
    me.update()
    uv = me.uv_layers.new(name="UVMap")
    for poly, (_f, col) in zip(me.polygons, b.faces):
        for li in poly.loop_indices:
            uv.data[li].uv = ((col + 0.5) / PALETTE_SIZE, 0.5)
    me.polygons.foreach_set("use_smooth", [False] * len(me.polygons))
    me.materials.append(palette_mat)
    ob = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(ob)
    objects = [ob]
    if muzzle is not None:
        em = bpy.data.objects.new("SOCKET_muzzle", None)
        assert em.name == "SOCKET_muzzle", "socket empty name collided: " + em.name
        em.empty_display_type = "ARROWS"
        em.empty_display_size = 2.0
        em.location = (muzzle[0], -muzzle[1], muzzle[2])
        em.parent = ob
        bpy.context.scene.collection.objects.link(em)
        objects.append(em)
    return objects


def export_fbx(path, objects):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={"MESH", "EMPTY"},
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", bake_space_transform=True,
        axis_forward="-Z", axis_up="Y", mesh_smooth_type="FACE", add_leaf_bones=False,
        use_mesh_modifiers=True, path_mode="STRIP")


def make_palette_material(tex_path):
    mat = bpy.data.materials.new("Palette")
    mat.use_nodes = True
    nt = mat.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(tex_path)
    tex.interpolation = "Closest"
    nt.links.new(tex.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Base Color"])
    return mat


def render_preview(outdir, names):
    """Optional Workbench render per mesh (three-quarter view), for a quick silhouette review. Not a deliverable."""
    os.makedirs(outdir, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "TEXTURE"
    sc.render.resolution_x, sc.render.resolution_y = 480, 270
    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = "ORTHO"
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    sc.camera = cam
    target = bpy.data.objects.new("target", None)
    sc.collection.objects.link(target)
    track = cam.constraints.new("TRACK_TO")
    track.target = target
    track.track_axis = "TRACK_NEGATIVE_Z"
    track.up_axis = "UP_Y"
    for n in names:
        ob = bpy.data.objects[n]
        for o in bpy.data.objects:
            if o.type == "MESH":
                o.hide_render = o is not ob
        xs = [v.co.x for v in ob.data.vertices]
        zs = [v.co.z for v in ob.data.vertices]
        cx, cz = (max(xs) + min(xs)) / 2, (max(zs) + min(zs)) / 2
        cam_data.ortho_scale = max(max(xs) - min(xs), 60) * 1.25
        target.location = (cx, 0, cz)
        cam.location = (cx + 70, 160, cz + 110)       # Blender +Y is Unreal -Y: looks at the weapon's left side from above
        sc.render.filepath = os.path.join(outdir, n + ".png")
        bpy.ops.render.render(write_still=True)


def main(argv):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="ArtSource/Export")
    ap.add_argument("--tex", default="ArtSource/Textures")
    ap.add_argument("--blend", default="ArtSource/Weapons/weapons.blend")
    ap.add_argument("--preview", default="")
    args = ap.parse_args(argv)
    assert len(PALETTE) == PALETTE_SIZE
    for d in (args.out, args.tex, os.path.dirname(os.path.abspath(args.blend))):
        os.makedirs(d, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 0.01
    sc.unit_settings.length_unit = "CENTIMETERS"
    bpy.context.preferences.filepaths.save_version = 0

    tex_path = os.path.abspath(os.path.join(args.tex, "T_Items_Palette.png"))
    write_palette(tex_path)
    mat = make_palette_material(tex_path)

    manifest = {"palette": [{"name": n, "srgb": list(rgb)} for n, rgb in PALETTE], "weapons": [], "items": []}
    names = []
    total = 0
    for stem, fn in WEAPONS:
        b = Builder()
        muzzle = fn(b)
        tris, lo, hi = validate_weapon(stem, b, muzzle)
        name = "SM_Weapon_" + stem
        objects = make_object(name, b, mat, muzzle)
        export_fbx(os.path.join(args.out, name + ".fbx"), objects)
        # Blender object names are unique per file: rename this weapon's empty so the next one can be called SOCKET_muzzle again
        # (otherwise it exports as SOCKET_muzzle.001 and Unreal names the socket "muzzle.001").
        objects[1].name = "Muzzle_" + stem
        manifest["weapons"].append({"name": name, "fbx": name + ".fbx", "tris": tris, "muzzle": [round(float(x), 2) for x in muzzle],
                                    "bounds_min": [round(float(x), 2) + 0.0 for x in lo], "bounds_max": [round(float(x), 2) + 0.0 for x in hi]})
        names.append(name)
        total += tris
        print(f"[weapons] {name}: tris={tris} bounds={lo.round(1).tolist()}..{hi.round(1).tolist()} muzzle={muzzle}")
    for stem, fn, asset in ITEMS:
        b = Builder()
        fn(b)
        tris, lo, hi = validate_item(stem, b)
        name = "SM_Item_" + stem
        export_fbx(os.path.join(args.out, name + ".fbx"), make_object(name, b, mat))
        manifest["items"].append({"name": name, "fbx": name + ".fbx", "tris": tris, "asset": asset,
                                  "bounds_min": [round(float(x), 2) + 0.0 for x in lo], "bounds_max": [round(float(x), 2) + 0.0 for x in hi]})
        names.append(name)
        total += tris
        print(f"[weapons] {name}: tris={tris} bounds={lo.round(1).tolist()}..{hi.round(1).tolist()}")
    manifest["total_tris"] = total
    with open(os.path.join(args.out, "weapons_manifest.json"), "w") as fh:
        json.dump(manifest, fh, indent=1, sort_keys=True)
        fh.write("\n")
    if args.preview:
        render_preview(args.preview, names)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(args.blend), compress=True)
    print(f"[weapons] done weapons={len(WEAPONS)} items={len(ITEMS)} tris={total}")


if __name__ == "__main__":
    if bpy is None:
        for stem, fn in WEAPONS:
            bb = Builder()
            m = fn(bb)
            print(stem, validate_weapon(stem, bb, m)[0])
        for stem, fn, _a in ITEMS:
            bb = Builder()
            fn(bb)
            print(stem, validate_item(stem, bb)[0])
    else:
        main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
