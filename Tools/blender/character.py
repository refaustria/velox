"""Velox character: low-poly flat-shaded body rigged to the UE5 Mannequin bone names (ADR 0006 section 4, E0.5).

  blender -b --factory-startup --python Tools/blender/character.py -- --out ArtSource/Export
  blender -b --factory-startup --python Tools/blender/character.py -- --animations --out ArtSource/Export   (task 12: A_Velox_*.fbx)

Writes <out>/SK_Velox.fbx, ArtSource/Characters/velox_character.blend (compressed) and ArtSource/Textures/T_Velox_Mask.png
(paths relative to the repo root, derived from this script). Deterministic: no random numbers, no time stamps in our data.

Proportions (cm, 1 BU = 1 cm): 140 tall, feet at Z 0, -Y forward, +X is the character's left (the _l bones), A-pose arms.
Mask texture (UV0): the u axis is split into four vertical bands, one per channel, so the mask survives any V flip:
  R (u 0.00-0.25) body, G (0.25-0.50) trim, B (0.50-0.75) skin / visor, A (0.75-1.00) emissive strips.
Skin weights are computed here (rigid parts, linear blend between two bones across a joint), not by bone heat: the body is
made of disjoint low-poly solids, which bone heat rejects. Every vertex has at most 2 influences (limit 4 enforced).
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Euler, Matrix, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import export_common as ec  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))

HEIGHT_CM = 140.0
MAX_TRIS = 6000
MAX_SHOULDER_CM = 75.0
MAX_INFLUENCES = 4
MASK_SIZE = 1024
SIDES = 8

# socket name -> bone (offset zero; the human tunes weapon_r in the editor)
SOCKETS = {"weapon_r": "hand_r", "foot_l": "ball_l", "foot_r": "ball_r", "eye": "head"}

# ---- skeleton: (name, parent, head, tail). Left (_l) is +X. Mannequin names and hierarchy (ADR 0006 section 4.2).
ARM_DIR = Vector((math.sin(math.radians(20.0)), 0.0, -math.cos(math.radians(20.0))))


def _side(s):
    n = "_l" if s > 0 else "_r"
    sh = Vector((14.0 * s, 0.0, 107.0))
    ua_len, la_len, hand_len = 18.0, 17.0, 9.0
    d = Vector((ARM_DIR.x * s, 0.0, ARM_DIR.z))
    elbow = sh + d * ua_len
    wrist = elbow + d * la_len
    hand_end = wrist + d * hand_len
    return [
        ("clavicle" + n, "spine_05", Vector((3.0 * s, 0.0, 106.0)), sh),
        ("upperarm" + n, "clavicle" + n, sh, elbow),
        ("lowerarm" + n, "upperarm" + n, elbow, wrist),
        ("hand" + n, "lowerarm" + n, wrist, hand_end),
        ("thigh" + n, "pelvis", Vector((8.0 * s, 0.0, 68.0)), Vector((9.0 * s, 0.0, 38.0))),
        ("calf" + n, "thigh" + n, Vector((9.0 * s, 0.0, 38.0)), Vector((9.0 * s, 0.0, 9.0))),
        ("foot" + n, "calf" + n, Vector((9.0 * s, 0.0, 9.0)), Vector((9.0 * s, -11.0, 3.0))),
        ("ball" + n, "foot" + n, Vector((9.0 * s, -11.0, 3.0)), Vector((9.0 * s, -19.0, 1.5))),
    ]


def skeleton():
    bones = [
        ("root", None, Vector((0, 0, 0)), Vector((0, 0, 6))),
        ("pelvis", "root", Vector((0, 0, 68)), Vector((0, 0, 74))),
        ("spine_01", "pelvis", Vector((0, 0, 74)), Vector((0, 0, 80))),
        ("spine_02", "spine_01", Vector((0, 0, 80)), Vector((0, 0, 88))),
        ("spine_03", "spine_02", Vector((0, 0, 88)), Vector((0, 0, 96))),
        ("spine_04", "spine_03", Vector((0, 0, 96)), Vector((0, 0, 103))),
        ("spine_05", "spine_04", Vector((0, 0, 103)), Vector((0, 0, 112))),
        ("neck_01", "spine_05", Vector((0, 0, 112)), Vector((0, 0, 116))),
        ("neck_02", "neck_01", Vector((0, 0, 116)), Vector((0, 0, 120))),
        ("head", "neck_02", Vector((0, 0, 120)), Vector((0, 0, 140))),
    ]
    bones += _side(1) + _side(-1)
    return bones


def sym(name, s):
    return name + ("_l" if s > 0 else "_r")


# ---- parts. channel: 0 body (R), 1 trim (G), 2 skin/visor (B), 3 emissive strip (A)
# frustum: a, b, ra=(rx, ry) at a, rb at b, chain = [(t along a->b, bone)] linear blend
# box: centre, size, bone
def parts():
    P = []

    def fr(name, a, b, ra, rb, ch, chain, n=SIDES):
        P.append(dict(kind="frustum", name=name, a=Vector(a), b=Vector(b), ra=ra, rb=rb, ch=ch, chain=chain, n=n))

    def bx(name, c, size, ch, bone):
        P.append(dict(kind="box", name=name, c=Vector(c), size=Vector(size), ch=ch, chain=[(0.0, bone)]))

    fr("pelvis", (0, 0, 64), (0, 0, 76), (13, 9), (12, 8.5), 0, [(0, "pelvis"), (1, "spine_01")])
    fr("belt", (0, 0, 74), (0, 0, 78), (13.6, 9.6), (13.6, 9.6), 1, [(0, "spine_01")])
    fr("waist", (0, 0, 76), (0, 0, 92), (11.5, 8), (13, 8.5), 0, [(0, "spine_01"), (0.5, "spine_02"), (1, "spine_03")])
    fr("chest", (0, 0, 92), (0, 0, 108), (13, 8.5), (17, 9.5), 0, [(0, "spine_03"), (0.5, "spine_04"), (1, "spine_05")])
    fr("neck", (0, 0, 108), (0, 0, 119), (4.5, 4.5), (4.5, 4.5), 2, [(0, "spine_05"), (0.3, "neck_01"), (0.7, "neck_02"), (1, "head")])
    fr("head_lower", (0, 0, 119), (0, 0, 134), (7, 8), (10, 10.5), 0, [(0, "head")])
    fr("head_top", (0, 0, 134), (0, 0, 140), (10, 10.5), (6, 7), 0, [(0, "head")])
    bx("visor", (0, -9.8, 130), (15, 3.5, 6), 2, "head")
    bx("backpack", (0, 8.5, 100), (14, 6, 16), 1, "spine_04")
    bx("chest_strip", (0, -9.4, 100), (4, 1.4, 12), 3, "spine_04")
    bx("back_strip", (0, 11.7, 100), (3, 0.8, 12), 3, "spine_04")
    for s in (1, -1):
        sk = {b[0]: b for b in _side(s)}
        ua, la, hd = sk[sym("upperarm", s)], sk[sym("lowerarm", s)], sk[sym("hand", s)]
        fr(sym("shoulder_pad", s), (14 * s, 0, 109), (14 * s, 0, 104), (6.5, 6), (5.5, 5), 1, [(0, sym("clavicle", s))])
        fr(sym("upperarm", s), ua[2], ua[3], (4.5, 4.5), (3.8, 3.8), 0,
           [(0, sym("clavicle", s)), (0.3, sym("upperarm", s)), (1, sym("upperarm", s))])
        fr(sym("lowerarm", s), la[2], la[3], (3.8, 3.8), (3.3, 3.3), 0,
           [(0, sym("upperarm", s)), (0.25, sym("lowerarm", s)), (1, sym("lowerarm", s))])
        fr(sym("hand", s), hd[2], hd[3], (3.6, 3.6), (3.0, 3.0), 2, [(0, sym("hand", s))])
        fr(sym("thigh", s), (8 * s, 0, 68), (9 * s, 0, 38), (7.5, 7.5), (5.5, 5.5), 0,
           [(0, "pelvis"), (0.25, sym("thigh", s)), (1, sym("thigh", s))])
        fr(sym("calf", s), (9 * s, 0, 38), (9 * s, 0, 9), (5.5, 5.5), (4.2, 4.2), 0,
           [(0, sym("thigh", s)), (0.2, sym("calf", s)), (1, sym("calf", s))])
        bx(sym("boot", s), (9 * s, -2, 4), (10, 22, 8), 1, sym("foot", s))
        bx(sym("toe", s), (9 * s, -16.5, 3), (9, 7, 6), 1, sym("ball", s))
        bx(sym("thigh_strip", s), (8.8 * s, -6.9, 53), (2.4, 1.4, 18), 3, sym("thigh", s))
        bx(sym("calf_strip", s), (9 * s, -4.9, 24), (2.4, 1.4, 14), 3, sym("calf", s))
    return P


def build_mesh(part_list):
    """One bmesh from all parts. Returns (bmesh, [(BMVert, weights)]); the channel is stored in the int face layer mask_channel."""
    bm = bmesh.new()
    weights = []
    ch_layer = bm.faces.layers.int.new("mask_channel")

    def chain_weights(chain, t):
        t = max(0.0, min(1.0, t))
        if len(chain) == 1 or t <= chain[0][0]:
            return {chain[0][1]: 1.0}
        for (t0, b0), (t1, b1) in zip(chain, chain[1:]):
            if t <= t1:
                k = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
                out = {}
                out[b0] = out.get(b0, 0.0) + (1.0 - k)
                out[b1] = out.get(b1, 0.0) + k
                return {b: w for b, w in out.items() if w > 1e-6}
        return {chain[-1][1]: 1.0}

    for part in part_list:
        verts = []
        if part["kind"] == "frustum":
            a, b, n = part["a"], part["b"], part["n"]
            axis = b - a
            length = axis.length
            ad = axis / length
            ref = Vector((0, 1, 0)) if abs(ad.y) < 0.9 else Vector((1, 0, 0))
            u = ref.cross(ad).normalized()
            v = ad.cross(u)
            ts = sorted({0.0, 1.0} | {k for k, _ in part["chain"]})
            rings = []
            for t in ts:
                centre = a + axis * t
                rx = part["ra"][0] + (part["rb"][0] - part["ra"][0]) * t
                ry = part["ra"][1] + (part["rb"][1] - part["ra"][1]) * t
                ring = []
                for i in range(n):
                    ang = 2.0 * math.pi * (i + 0.5) / n
                    p = centre + u * (math.cos(ang) * rx) + v * (math.sin(ang) * ry)
                    ring.append(bm.verts.new(p))
                rings.append(ring)
            faces = []
            for r in range(len(rings) - 1):
                for i in range(n):
                    j = (i + 1) % n
                    faces.append(bm.faces.new((rings[r][i], rings[r][j], rings[r + 1][j], rings[r + 1][i])))
            faces.append(bm.faces.new(list(reversed(rings[0]))))
            faces.append(bm.faces.new(rings[-1]))
            verts = [vtx for ring in rings for vtx in ring]
            for vtx in verts:
                t = (vtx.co - a).dot(ad) / length
                weights.append((vtx, chain_weights(part["chain"], t)))
        else:
            res = bmesh.ops.create_cube(bm, size=1.0)
            verts = res["verts"]
            for vtx in verts:
                vtx.co = Vector((vtx.co.x * part["size"].x, vtx.co.y * part["size"].y, vtx.co.z * part["size"].z)) + part["c"]
            faces = list({f for vtx in verts for f in vtx.link_faces})
            for vtx in verts:
                weights.append((vtx, {part["chain"][0][1]: 1.0}))
        for f in faces:
            f[ch_layer] = part["ch"]
    return bm, weights


# =====================================================================================================================
# Animations (task 12, ADR 0006 section 4.4): procedural keyframes on the rig above, one FBX per clip (armature only).
#
# Every pose is a Pose: per-bone Euler degrees relative to the parent's deformation (character axes: X = character left, Y = back,
# Z = up; +X rotation tips the top forward and swings a hanging limb backwards), a pelvis translation, and optional per-foot ankle
# targets solved by a two-bone IK (knee bends forward) so the feet stay planted. Rig.solve turns a Pose into pose-bone quaternions:
# with B the bone's rest rotation, D the accumulated deformation, Q = B^T Dparent^T D B (bone-local basis).
# Loops: the last key equals the first (frame N == frame 0), so the cycle length is exactly N / ANIM_FPS.
# =====================================================================================================================
ANIM_FPS = 60
# (name, seconds, loops). Seconds are the ADR 0006 section 4.4 numbers; Crouch_Idle is not specified there, 2 s like Idle.
CLIPS = [
    ("Idle", 2.0, True), ("Run_Fwd", 0.6, True), ("Run_Back", 0.6, True), ("Run_Left", 0.6, True), ("Run_Right", 0.6, True),
    ("Jump_Start", 0.15, False), ("Air_Loop", 0.5, True), ("Land", 0.2, False), ("Crouch_Idle", 2.0, True),
    ("Crouch_Walk", 0.8, True), ("Fire_Upper", 0.15, False), ("Death_A", 0.8, False), ("Death_B", 0.8, False),
]
RUN_STRIDE_CM, RUN_LIFT_CM = 24.0, 14.0            # foot excursion from the neutral position / swing height at 800 cm/s
CROUCH_STRIDE_CM, CROUCH_LIFT_CM = 14.0, 5.0
CROUCH_DROP_CM = 20.0                              # capsule half height 70 -> 50 (VeloxCharacterGeometry::CrouchedHalfHeight)
RUN_LEAN_DEG = 5.0
DEATH_FLOOR_Z_CM = 11.0                            # pelvis height when lying (body thickness, back pack included)
FOOTPRINT_LIMIT_CM = 37.5 + 40.0                   # capsule radius + 40 (task 12 acceptance)
FOOT_BACK_CM, FOOT_FRONT_CM = 9.0, 20.0            # ankle -> heel / toe tip distance (boot + toe boxes of parts())
FLOOR_TOLERANCE_CM = 1.0
SPINE_CHAIN = ("spine_01", "spine_02", "spine_03", "spine_04")
UPPER_BODY_BONES = ("spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "neck_01", "neck_02", "head",
                    "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l", "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r")


def ease(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)


class Pose:
    def __init__(self, stand=True):
        self.rot = {}
        self.pelvis = Vector((0.0, 0.0, 0.0))
        self.feet = {1: [Vector((0.0, 0.0, 0.0)), 0.0], -1: [Vector((0.0, 0.0, 0.0)), 0.0]} if stand else None

    def r(self, bone, x=0.0, y=0.0, z=0.0):
        a = self.rot.setdefault(bone, [0.0, 0.0, 0.0])
        a[0] += x
        a[1] += y
        a[2] += z
        return self

    def lean(self, forward_deg, side_deg=0.0):
        """Torso lean spread over spine_01..04; the head keeps looking ahead (60 % counter)."""
        for b in SPINE_CHAIN:
            self.r(b, x=forward_deg / len(SPINE_CHAIN), y=side_deg / len(SPINE_CHAIN))
        return self.r("head", x=-0.6 * forward_deg, y=-0.6 * side_deg)


def lerp_pose(a, b, w):
    out = Pose(stand=a.feet is not None and b.feet is not None)
    for bone in set(a.rot) | set(b.rot):
        ra, rb = a.rot.get(bone, [0.0] * 3), b.rot.get(bone, [0.0] * 3)
        out.rot[bone] = [ra[i] + (rb[i] - ra[i]) * w for i in range(3)]
    out.pelvis = a.pelvis.lerp(b.pelvis, w)
    if out.feet is not None:
        for s in (1, -1):
            out.feet[s] = [a.feet[s][0].lerp(b.feet[s][0], w), a.feet[s][1] + (b.feet[s][1] - a.feet[s][1]) * w]
    return out


def sole_lift(pitch_deg):
    """Ankle height change that keeps the lowest point of the boot on the floor when the foot pitches (toe down = positive)."""
    th = math.radians(pitch_deg)
    if th >= 0.0:
        return FOOT_FRONT_CM * math.sin(th) + 9.0 * math.cos(th) - 9.0
    return FOOT_BACK_CM * math.sin(-th) + 9.0 * math.cos(th) - 9.0


def set_foot(p, side, offset, pitch=0.0):
    p.feet[side] = [Vector((offset.x, offset.y, offset.z + sole_lift(pitch))), pitch]


def weapon_arms(p, ur=-25.0, lr=-60.0, ul=-18.0, ll=-50.0):
    """Ready stance: right (weapon) arm raised, left arm supporting."""
    p.r("upperarm_r", x=ur).r("lowerarm_r", x=lr).r("upperarm_l", x=ul).r("lowerarm_l", x=ll)
    return p


# ---- clips -----------------------------------------------------------------------------------------------------------------
def pose_idle(t):
    b = math.sin(2.0 * math.pi * t)
    p = Pose()
    p.pelvis = Vector((0.6 * b, 0.0, -1.2 + 0.35 * b))
    weapon_arms(p, ll=-50.0 - 1.5 * b)
    p.r("spine_02", x=0.7 * b).r("spine_03", x=0.8 * b, z=1.0 * b).r("spine_05", x=-1.0 * b)
    p.r("clavicle_l", y=-1.4 * b).r("clavicle_r", y=1.4 * b).r("head", x=0.5 * b, z=-1.2 * b)
    return p


def gait(u, stride, lift):
    """(position along the travel axis, height) of a foot at cycle phase u (0 = heel strike, stance until 0.5, then swing)."""
    if u < 0.5:
        return stride * (1.0 - 4.0 * u), 0.0
    v = (u - 0.5) / 0.5
    return -stride * math.cos(math.pi * v), lift * math.sin(math.pi * v)


def foot_pitch(u):
    """Toe up at heel strike, rolling to toe down at push-off, toe down at lift-off, recovering for the next strike."""
    if u < 0.5:
        return -12.0 + 32.0 * (u / 0.5)
    v = (u - 0.5) / 0.5
    return 20.0 - 32.0 * ease(v)


def pose_run(t, move, stride=RUN_STRIDE_CM, lift=RUN_LIFT_CM, drop=7.0, bob=2.0, lean_deg=RUN_LEAN_DEG):
    mx, my = move
    c = math.cos(2.0 * math.pi * t)
    p = Pose()
    p.pelvis = Vector((0.0, 0.0, -drop - bob * math.cos(4.0 * math.pi * t)))
    for side, phase in ((1, 0.0), (-1, 0.5)):
        u = (t + phase) % 1.0
        pos, lf = gait(u, stride, lift)
        set_foot(p, side, Vector((mx * pos, my * pos, lf)), foot_pitch(u) * abs(my))
    swing = 32.0 * abs(my) + 6.0 * abs(mx)
    # the arm opposite the leading leg swings forward; elbows flexed, flexing a bit more at the front of the swing
    p.r("upperarm_l", x=swing * c).r("upperarm_r", x=-swing * c)
    p.r("lowerarm_l", x=-65.0 + 0.5 * swing * c).r("lowerarm_r", x=-65.0 - 0.5 * swing * c)
    p.r("spine_01", z=-3.0 * c * abs(my)).r("spine_03", z=6.0 * c * abs(my)).r("head", z=-3.0 * c * abs(my))
    p.lean(-my * lean_deg, mx * lean_deg)
    return p


def pose_air(t):
    s = math.sin(2.0 * math.pi * t)
    p = Pose()
    p.pelvis = Vector((0.0, 0.0, -4.0))
    set_foot(p, 1, Vector((0.0, -16.0 + 1.5 * s, 12.0 + 1.5 * s)), 15.0)
    set_foot(p, -1, Vector((0.0, 10.0 - 1.5 * s, 5.0 - 1.0 * s)), 25.0)
    p.r("upperarm_l", x=-35.0 + 3.0 * s, y=-20.0).r("upperarm_r", x=-35.0 - 3.0 * s, y=20.0)
    p.r("lowerarm_l", x=-45.0).r("lowerarm_r", x=-45.0)
    p.lean(4.0).r("spine_05", x=1.0 * s)
    return p


def pose_crouch(t, walk):
    b = math.sin(2.0 * math.pi * t)
    p = Pose()
    p.pelvis = Vector((0.0, 5.0, -CROUCH_DROP_CM + (-1.2 * math.cos(4.0 * math.pi * t) if walk else 0.4 * b)))
    if walk:
        for side, phase in ((1, 0.0), (-1, 0.5)):
            u = (t + phase) % 1.0
            pos, lf = gait(u, CROUCH_STRIDE_CM, CROUCH_LIFT_CM)
            set_foot(p, side, Vector((0.0, -pos, lf)), foot_pitch(u) * 0.5)
    else:
        set_foot(p, 1, Vector((2.0, -3.0, 0.0)))
        set_foot(p, -1, Vector((-2.0, 3.0, 0.0)))
    weapon_arms(p, ur=-30.0, lr=-70.0, ul=-24.0, ll=-60.0)
    c = math.cos(2.0 * math.pi * t)
    if walk:
        p.r("upperarm_l", x=6.0 * c).r("upperarm_r", x=-6.0 * c)
    else:
        p.r("spine_03", x=0.6 * b).r("clavicle_l", y=-1.0 * b).r("clavicle_r", y=1.0 * b)
    p.lean(10.0)
    return p


def pose_deep_crouch():
    p = Pose()
    p.pelvis = Vector((0.0, 6.0, -24.0))
    set_foot(p, 1, Vector((3.0, -2.0, 0.0)))
    set_foot(p, -1, Vector((-3.0, 2.0, 0.0)))
    p.r("upperarm_l", x=-25.0).r("upperarm_r", x=-25.0).r("lowerarm_l", x=-40.0).r("lowerarm_r", x=-40.0)
    p.lean(14.0)
    return p


def pose_jump_start(t):
    p = lerp_pose(pose_idle(0.0), pose_air(0.0), ease(t))
    p.pelvis.z -= 5.0 * math.sin(math.pi * t)
    return p


def pose_land(t):
    if t < 0.4:
        return lerp_pose(pose_air(0.0), pose_deep_crouch(), ease(t / 0.4))
    return lerp_pose(pose_deep_crouch(), pose_idle(0.0), ease((t - 0.4) / 0.6))


def pose_fire_upper(t):
    """Additive delta over the reference pose: frame 0 and the last frame are the reference (all zero)."""
    k = (t / 0.3 if t < 0.3 else (1.0 - t) / 0.7)
    k = math.sin(0.5 * math.pi * max(0.0, min(1.0, k)))
    p = Pose(stand=False)
    p.r("spine_03", x=-1.5 * k).r("spine_05", x=-2.5 * k).r("neck_02", x=1.0 * k).r("head", x=0.5 * k)
    p.r("clavicle_r", y=-1.5 * k).r("upperarm_r", x=10.0 * k).r("lowerarm_r", x=-4.0 * k).r("hand_r", x=-6.0 * k)
    p.r("upperarm_l", x=2.0 * k)
    return p


def pose_death(t, forward):
    """Fall back (A) or forward (B), pivoting on the pelvis; ends lying on the floor with limbs splayed on the ground plane."""
    sign = 1.0 if forward else -1.0
    fall = min(1.0, t / 0.85)
    f = fall * fall
    settle = 0.0 if t < 0.85 else 0.03 * math.sin(math.pi * (t - 0.85) / 0.15)
    f = f - settle
    mid = math.sin(math.pi * min(1.0, f))
    p = Pose(stand=False)
    p.r("pelvis", x=sign * 90.0 * f)
    p.pelvis = Vector((0.0, 0.0, -(68.0 - DEATH_FLOOR_Z_CM) * max(f, 0.0) ** 1.5))
    p.r("spine_02", x=sign * -5.0 * f).r("spine_04", x=sign * -4.0 * f)
    arm = 40.0 if not forward else 55.0
    p.r("upperarm_l", y=-arm * f, x=-45.0 * mid).r("upperarm_r", y=arm * f, x=-45.0 * mid)
    p.r("lowerarm_l", x=-20.0 * mid).r("lowerarm_r", x=-20.0 * mid)
    p.r("thigh_l", y=-12.0 * f, x=-15.0 * mid).r("thigh_r", y=12.0 * f, x=-15.0 * mid)
    p.r("head", z=(25.0 if not forward else 70.0) * f)
    if forward:  # face down: point the toes along the legs so they do not dig into the floor
        p.r("foot_l", x=70.0 * f).r("foot_r", x=70.0 * f).r("calf_l", x=18.0 * f).r("calf_r", x=18.0 * f)
    return p


def clip_pose(name, t):
    if name == "Idle":
        return pose_idle(t)
    if name.startswith("Run_"):
        move = {"Fwd": (0.0, -1.0), "Back": (0.0, 1.0), "Left": (1.0, 0.0), "Right": (-1.0, 0.0)}[name[4:]]
        return pose_run(t, move)
    return {"Jump_Start": pose_jump_start, "Air_Loop": pose_air, "Land": pose_land, "Fire_Upper": pose_fire_upper,
            "Crouch_Idle": lambda x: pose_crouch(x, False), "Crouch_Walk": lambda x: pose_crouch(x, True),
            "Death_A": lambda x: pose_death(x, False), "Death_B": lambda x: pose_death(x, True)}[name](t)


# ---- solver ----------------------------------------------------------------------------------------------------------------
class Rig:
    def __init__(self, arm_obj, skel):
        self.arm = arm_obj
        self.order = [b[0] for b in skel]
        self.parent = {b[0]: b[1] for b in skel}
        self.head = {b[0]: b[2].copy() for b in skel}
        self.rest = {n: arm_obj.data.bones[n].matrix_local.to_3x3() for n in self.order}

    def leg_ik(self, pose):
        out = {}
        for s in (1, -1):
            thigh, calf, foot = sym("thigh", s), sym("calf", s), sym("foot", s)
            hip0, knee0, ank0 = self.head[thigh], self.head[calf], self.head[foot]
            off, pitch = pose.feet[s]
            hip = hip0 + pose.pelvis
            t = ank0 + off - hip
            l1, l2 = (knee0 - hip0).length, (ank0 - knee0).length
            d = min(max(t.length, abs(l1 - l2) + 1e-3), l1 + l2 - 1e-3)
            th = t.normalized()
            target = hip + th * d
            a = (l1 * l1 - l2 * l2 + d * d) / (2.0 * d)
            h = math.sqrt(max(l1 * l1 - a * a, 0.0))
            pole = Vector((0.0, -1.0, 0.0))
            pole = (pole - th * pole.dot(th)).normalized()
            knee = hip + th * a + pole * h
            out[thigh] = (knee0 - hip0).normalized().rotation_difference((knee - hip).normalized()).to_matrix()
            out[calf] = (ank0 - knee0).normalized().rotation_difference((target - knee).normalized()).to_matrix()
            out[foot] = Euler((math.radians(pitch), 0.0, 0.0), "XYZ").to_matrix()
        return out

    def solve(self, pose):
        """bone -> (quaternion, location or None) in pose-bone space."""
        ident = Matrix.Identity(3)
        if pose.feet is not None:
            assert "pelvis" not in pose.rot, "IK legs need an upright pelvis"
        ik = self.leg_ik(pose) if pose.feet is not None else {}
        deform, out = {}, {}
        for name in self.order:
            par = self.parent[name]
            dp = deform[par] if par else ident
            if name in ik:
                db = ik[name]
            else:
                e = pose.rot.get(name)
                db = dp @ (Euler([math.radians(v) for v in e], "XYZ").to_matrix() if e else ident)
            deform[name] = db
            b = self.rest[name]
            q = (b.transposed() @ dp.transposed() @ db @ b).to_quaternion()
            loc = None
            if name == "pelvis":
                loc = b.transposed() @ pose.pelvis
            out[name] = (q, loc)
        return out


def evaluated_vertices(mesh_obj):
    deps = bpy.context.evaluated_depsgraph_get()
    ev = mesh_obj.evaluated_get(deps)
    me = ev.to_mesh()
    pts = [v.co.copy() for v in me.vertices]
    ev.to_mesh_clear()
    return pts


def export_anim_fbx(path, arm_obj):
    bpy.ops.object.select_all(action="DESELECT")
    arm_obj.select_set(True)
    bpy.context.view_layer.objects.active = arm_obj
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.export_scene.fbx(
        filepath=path, use_selection=True, object_types={"ARMATURE"}, add_leaf_bones=False, armature_nodetype="NULL",
        bake_anim=True, bake_anim_use_all_bones=True, bake_anim_use_nla_strips=False, bake_anim_use_all_actions=False,
        bake_anim_force_startend_keying=True, bake_anim_step=1.0, bake_anim_simplify_factor=0.0,
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_ALL", bake_space_transform=True, global_scale=1.0,
        axis_forward="-Y", axis_up="Z", path_mode="STRIP")


def run_animations(arm_obj, mesh_obj, skel, out_dir):
    import json
    scene = bpy.context.scene
    scene.render.fps = ANIM_FPS
    scene.render.fps_base = 1.0
    rig = Rig(arm_obj, skel)
    arm_obj.animation_data_create()
    for pb in arm_obj.pose.bones:
        pb.rotation_mode = "QUATERNION"
    summary = {}
    for name, seconds, loops in CLIPS:
        frames = int(round(seconds * ANIM_FPS))
        act = bpy.data.actions.new("A_Velox_" + name)
        arm_obj.animation_data.action = act
        for pb in arm_obj.pose.bones:
            pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
            pb.location = (0.0, 0.0, 0.0)
        prev = {}
        poses = []
        for f in range(frames + 1):
            pose = clip_pose(name, f / frames)
            poses.append(pose)
            for bone, (q, loc) in rig.solve(pose).items():
                pb = arm_obj.pose.bones[bone]
                if bone in prev:
                    q.make_compatible(prev[bone])
                prev[bone] = q
                pb.rotation_quaternion = q
                pb.keyframe_insert("rotation_quaternion", frame=f)
                if loc is not None:
                    pb.location = loc
                    pb.keyframe_insert("location", frame=f)
        scene.frame_start, scene.frame_end = 0, frames
        # ---- checks on the evaluated mesh
        if loops:
            a, b = rig.solve(poses[0]), rig.solve(poses[-1])
            assert all((a[k][0].rotation_difference(b[k][0])).angle < 1e-4 for k in a), name + ": loop seam"
        lo_z, hi_z, span_xy = 1e9, -1e9, 0.0
        sample = range(0, frames + 1, max(1, frames // 12))
        if name.startswith("Death"):
            sample = [frames]
        for f in sample:
            scene.frame_set(f)
            pts = evaluated_vertices(mesh_obj)
            lo_z = min(lo_z, min(v.z for v in pts))
            hi_z = max(hi_z, max(v.z for v in pts))
            span_xy = max(span_xy, max(max(abs(v.x), abs(v.y)) for v in pts))
        root = arm_obj.pose.bones["root"]
        assert root.location.length < 1e-6 and root.rotation_quaternion.angle < 1e-6, name + ": root moved"
        if name.startswith("Death"):
            assert -2.0 < lo_z < 3.0 and hi_z < 36.0, "%s: end pose not on the floor z=[%.1f, %.1f]" % (name, lo_z, hi_z)
            assert span_xy <= FOOTPRINT_LIMIT_CM, "%s: end pose outside the footprint (%.1f cm)" % (name, span_xy)
        elif name in ("Idle", "Run_Fwd", "Run_Back", "Run_Left", "Run_Right", "Crouch_Idle", "Crouch_Walk"):
            assert lo_z > -FLOOR_TOLERANCE_CM, "%s: feet below the floor (z %.2f)" % (name, lo_z)
        if name == "Fire_Upper":
            first = rig.solve(poses[0])
            assert all(abs(q.w - 1.0) < 1e-6 for q, _ in first.values()), "Fire_Upper: first frame is not the reference pose"
            for bone, (q, _) in rig.solve(pose_fire_upper(0.3)).items():
                assert bone in UPPER_BODY_BONES or q.angle < 1e-6, "Fire_Upper moves " + bone
        scene.frame_set(0)
        path = os.path.join(out_dir, "A_Velox_%s.fbx" % name)
        export_anim_fbx(path, arm_obj)
        summary[name] = {"frames": frames, "seconds": seconds, "loop": loops}
        print("VELOX_ANIM %-11s %3d frames %.3f s loop=%d z=[%.1f, %.1f] reach=%.1f cm -> %s (%d bytes)" % (
            name, frames, frames / ANIM_FPS, loops, lo_z, hi_z, span_xy, path, os.path.getsize(path)))
        arm_obj.animation_data.action = None
        bpy.data.actions.remove(act)
    with open(os.path.join(out_dir, "A_Velox_clips.json"), "w") as fh:
        json.dump({"fps": ANIM_FPS, "clips": summary}, fh, indent=1, sort_keys=True)
    print("VELOX_ANIM wrote %d clips" % len(summary))


def main():
    out_dir = ec.parse_out_dir(os.path.join(REPO, "ArtSource", "Export"))
    ec.reset_scene()
    skel = skeleton()
    part_list = parts()

    # ---- mesh
    bm, weights = build_mesh(part_list)
    bm.verts.ensure_lookup_table()
    bm.verts.index_update()
    bmesh.ops.recalc_face_normals(bm, faces=list(bm.faces))
    for f in bm.faces:
        f.smooth = False

    # channel per face, projected UVs into the channel's u band
    uv_layer = bm.loops.layers.uv.new("UVMap")
    ch_layer = bm.faces.layers.int["mask_channel"]
    lo = Vector((min(v.co.x for v in bm.verts), min(v.co.y for v in bm.verts), min(v.co.z for v in bm.verts)))
    hi = Vector((max(v.co.x for v in bm.verts), max(v.co.y for v in bm.verts), max(v.co.z for v in bm.verts)))
    span = hi - lo
    margin = 0.02
    for f in bm.faces:
        ch = f[ch_layer]
        n = f.normal
        axes = sorted(range(3), key=lambda i: -abs(n[i]))[0]
        pu, pv = [i for i in range(3) if i != axes]
        for loop in f.loops:
            c = loop.vert.co
            fu = (c[pu] - lo[pu]) / span[pu]
            fv = (c[pv] - lo[pv]) / span[pv]
            loop[uv_layer].uv = ((ch + margin + fu * (1.0 - 2 * margin)) / 4.0, margin + fv * (1.0 - 2 * margin))
    vert_weights = {}
    bm.verts.index_update()
    for vtx, w in weights:
        vert_weights[vtx.index] = w

    bmesh.ops.triangulate(bm, faces=list(bm.faces))
    mesh = bpy.data.meshes.new("SK_Velox")
    bm.to_mesh(mesh)
    bm.free()

    mat = bpy.data.materials.new("M_Character_Master")
    mat.diffuse_color = (0.9, 0.9, 0.9, 1.0)
    mesh.materials.append(mat)
    mesh_obj = bpy.data.objects.new("SK_Velox", mesh)
    bpy.context.scene.collection.objects.link(mesh_obj)

    # ---- armature
    arm_data = bpy.data.armatures.new("Armature")
    arm_obj = bpy.data.objects.new("Armature", arm_data)
    bpy.context.scene.collection.objects.link(arm_obj)
    bpy.context.view_layer.objects.active = arm_obj
    arm_obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    edit = {}
    for name, parent, head, tail in skel:
        b = arm_data.edit_bones.new(name)
        b.head, b.tail = head, tail
        edit[name] = b
    for name, parent, _, _ in skel:
        if parent:
            edit[name].parent = edit[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    bone_names = [b[0] for b in skel]

    # ---- sockets: empties named SOCKET_<name> parented to a bone; the UE FBX importer turns them into sockets
    sockets = []
    for sname, bone in SOCKETS.items():
        empty = bpy.data.objects.new("SOCKET_" + sname, None)
        empty.empty_display_type = "PLAIN_AXES"
        empty.empty_display_size = 2.0
        bpy.context.scene.collection.objects.link(empty)
        empty.parent = arm_obj
        empty.parent_type = "BONE"
        empty.parent_bone = bone
        bpy.context.view_layer.update()
        empty.matrix_world = Matrix.Translation(edit[bone].head)
        sockets.append(empty)

    # ---- weights (max 4 influences, normalised) and modifier
    for name in bone_names:
        mesh_obj.vertex_groups.new(name=name)
    max_inf = 0
    for vi in range(len(mesh.vertices)):
        w = vert_weights[vi]
        top = sorted(w.items(), key=lambda kv: (-kv[1], kv[0]))[:MAX_INFLUENCES]
        total = sum(x for _, x in top)
        max_inf = max(max_inf, len(top))
        for bone, x in top:
            mesh_obj.vertex_groups[bone].add([vi], x / total, "REPLACE")
    mod = mesh_obj.modifiers.new("Armature", "ARMATURE")
    mod.object = arm_obj
    mesh_obj.parent = arm_obj

    # ---- verification
    depsgraph = bpy.context.evaluated_depsgraph_get()
    eval_obj = mesh_obj.evaluated_get(depsgraph)
    eval_mesh = eval_obj.to_mesh()
    zs = [v.co.z for v in eval_mesh.vertices]
    xs = [abs(v.co.x) for v in eval_mesh.vertices]
    ys = [v.co.y for v in eval_mesh.vertices]
    height = max(zs) - min(zs)
    tri_count = sum(len(p.vertices) - 2 for p in eval_mesh.polygons)
    pad_width = 2.0 * max(abs(v.co.x) for v in eval_mesh.vertices if v.co.z > 100.0 and abs(v.co.x) < 26.0)
    print("VELOX_CHAR bones=%d: %s" % (len(bone_names), ",".join(bone_names)))
    print("VELOX_CHAR tris=%d (budget %d) verts=%d max_influences=%d" % (tri_count, MAX_TRIS, len(eval_mesh.vertices), max_inf))
    print("VELOX_CHAR height=%.2f zmin=%.3f max|x|=%.2f y=[%.2f,%.2f] shoulder_width=%.2f" % (height, min(zs), max(xs), min(ys), max(ys), pad_width))
    assert tri_count <= MAX_TRIS, "triangle budget"
    assert abs(height - HEIGHT_CM) < 0.01 and abs(min(zs)) < 1e-4, "height / feet at Z 0"
    assert pad_width <= MAX_SHOULDER_CM, "shoulder width"
    assert len(bone_names) <= 70 and len(set(bone_names)) == len(bone_names)
    assert max_inf <= MAX_INFLUENCES
    assert len(mesh.materials) == 1
    for v in mesh.vertices:
        assert abs(sum(g.weight for g in v.groups) - 1.0) < 1e-4, "weights normalised"
    eval_obj.to_mesh_clear()

    if "--animations" in ec.script_args():
        run_animations(arm_obj, mesh_obj, skel, out_dir)
        return

    # ---- mask PNG (UV0 bands)
    band = MASK_SIZE // 4
    colors = [(255, 0, 0, 0), (0, 255, 0, 0), (0, 0, 255, 0), (0, 0, 0, 255)]
    row = b"\x00" + b"".join(bytes(colors[x // band]) for x in range(MASK_SIZE))
    raw = row * MASK_SIZE
    mask_path = os.path.join(REPO, "ArtSource", "Textures", "T_Velox_Mask.png")
    ec.write_png_rgba(mask_path, MASK_SIZE, MASK_SIZE, raw)

    # ---- save and export
    blend_path = os.path.join(REPO, "ArtSource", "Characters", "velox_character.blend")
    ec.save_blend(blend_path)
    fbx_path = os.path.join(out_dir, "SK_Velox.fbx")
    ec.export_fbx(fbx_path, [arm_obj, mesh_obj] + sockets)
    import json
    with open(os.path.join(out_dir, "SK_Velox.json"), "w") as f:
        json.dump({"triangles": tri_count, "bones": len(bone_names), "height_cm": round(height, 3)}, f, indent=1, sort_keys=True)
    print("VELOX_CHAR wrote %s (%d bytes), %s, %s" % (fbx_path, os.path.getsize(fbx_path), blend_path, mask_path))


main()
