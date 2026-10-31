"""Shared helpers for the Velox Blender scripts (ADR 0006 section 4.2, E0.5).

Run scripts as: blender -b --factory-startup --python Tools/blender/<script>.py -- --out ArtSource/Export
Blender 4.3.2 on the build VM; numpy is available. Scene unit 0.01, so 1 Blender unit = 1 cm, -Y forward, Z up.
"""
import os
import struct
import sys
import zlib

import bpy


def script_args():
    """Arguments after the lone `--` of the blender command line."""
    argv = sys.argv
    return argv[argv.index("--") + 1:] if "--" in argv else []


def parse_out_dir(default="ArtSource/Export"):
    args = script_args()
    out = default
    if "--out" in args:
        out = args[args.index("--out") + 1]
    return os.path.abspath(out)


def reset_scene():
    """Empty the factory scene and set 1 BU = 1 cm."""
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.armatures, bpy.data.cameras, bpy.data.lights):
        for item in list(block):
            block.remove(item)
    unit = bpy.context.scene.unit_settings
    unit.system = "METRIC"
    unit.scale_length = 0.01
    unit.length_unit = "CENTIMETERS"
    bpy.context.preferences.filepaths.save_version = 0  # no .blend1 next to the .blend


def export_fbx(path, objects):
    """FBX with the ADR 0006 section 4.2 settings: mesh + armature (+ socket empties), no leaf bones, no animation, face smoothing."""
    bpy.ops.object.select_all(action="DESELECT")
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.export_scene.fbx(
        filepath=path,
        use_selection=True,
        object_types={"ARMATURE", "MESH", "EMPTY"},
        use_mesh_modifiers=True,
        mesh_smooth_type="FACE",
        add_leaf_bones=False,
        armature_nodetype="NULL",
        bake_anim=False,
        apply_unit_scale=True,
        apply_scale_options="FBX_SCALE_ALL",
        bake_space_transform=True,
        global_scale=1.0,
        axis_forward="-Y",
        axis_up="Z",
        use_tspace=False,
        path_mode="STRIP",
    )


def save_blend(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=path, compress=True)


def write_png_rgba(path, width, height, raw_rows):
    """Write an 8-bit RGBA PNG without PIL. raw_rows: height * (1 filter byte 0 + width * 4 bytes)."""
    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw_rows, 9)))
        f.write(chunk(b"IEND", b""))
