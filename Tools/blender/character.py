"""Velox character: low-poly flat-shaded body rigged to the UE5 Mannequin bone names (ADR 0006 section 4, E0.5).

  blender -b --factory-startup --python Tools/blender/character.py -- --out ArtSource/Export

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
from mathutils import Matrix, Vector

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
