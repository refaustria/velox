"""Velox art import (ADR 0006 section 4, tasks 11/12): imports ArtSource output headless and fills the content tree.

Run (absolute paths; the argument string goes inside the quoted -script value):
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script="$PWD/Tools/editor/import_art.py --character $PWD/ArtSource/Export/SK_Velox.fbx --verify" \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Options:
  --character <fbx>   import SK_Velox: T_Velox_Mask, M_Character_Master, SK_Velox, SKEL_Velox, PA_Velox (+ sockets),
                      MI_Skin_<name> for every skin of create_cosmetics_content.py, then fill DA_Cosmetics_Velox
  --animations <dir>  import ArtSource/Export/A_Velox_*.fbx (task 12, character.py --animations) as A_Velox_<Name> on SKEL_Velox into
                      /Game/Characters/Velox/Anims (loop flags, Fire_Upper additive), then fill DA_Cosmetics_Velox Sequences/RunSequences
  --kit <dir>         import the modular kit (task 14): delegates to import_kit.run(dir, verify, fixture)
  --static <dir>      import the weapon and item meshes of Tools/blender/weapons_items.py (task 13): T_Items_Palette, M_Items_Palette,
                      M_Weapon_Flat, SM_Weapon_* (/Game/Weapons/Meshes, `muzzle` socket), SM_Item_* (/Game/Items/Meshes); then runs
                      create_cosmetics_content.py and create_item_content.py so DA_WeaponCosmetics_* and DA_Item_* point at them.
                      With --fixture: self-test of the static import path only (engine Cylinder -> FBX -> import -> socket -> delete).
  --mask <png>        mask texture (default ArtSource/Textures/T_Velox_Mask.png next to the fbx's ArtSource)
  --fixture           self-test of this import path: import the FBX (SK_Velox.fbx, or with --animations A_Velox_Idle.fbx) into a scratch
                      folder with the same code, verify it, delete the scratch assets
  --verify            reopen everything and assert the budgets (bone count, sockets, one material slot, triangles, LFS size)
Idempotent. Raises on any error so the commandlet exits non-zero.
"""
import colorsys
import os
import runpy
import sys

import unreal

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
MEL = unreal.MaterialEditingLibrary
ERRORS = []

FOLDER = "/Game/Characters/Velox"
ANIM_FOLDER = FOLDER + "/Anims"
SCRATCH = "/Game/_Scratch/ImportFixture"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))

MAX_BONES = 70
MAX_TRIS = 6000
LFS_BUDGET_BYTES = 600 * 1024 * 1024
HEIGHT_CM = 140.0
CAPSULE_RADIUS = 37.5
CAPSULE_HALF_HEIGHT = 70.0
EMISSIVE_STRENGTH = 0.35
SOCKETS = {  # name -> (bone, relative location)
    "weapon_r": ("hand_r", (0.0, 0.0, 0.0)),
    "foot_l": ("ball_l", (0.0, 0.0, 0.0)),
    "foot_r": ("ball_r", (0.0, 0.0, 0.0)),
    "eye": ("head", (0.0, 0.0, 0.0)),
}
# ADR 0006 section 4.4 / task 12: clip -> (seconds, loops). Fire_Upper is additive; the 60 fps of character.py makes every length exact.
ANIM_FPS = 60
ANIM_CLIPS = {
    "Idle": (2.0, True), "Run_Fwd": (0.6, True), "Run_Back": (0.6, True), "Run_Left": (0.6, True), "Run_Right": (0.6, True),
    "Jump_Start": (0.15, False), "Air_Loop": (0.5, True), "Land": (0.2, False), "Crouch_Idle": (2.0, True),
    "Crouch_Walk": (0.8, True), "Fire_Upper": (0.15, False), "Death_A": (0.8, False), "Death_B": (0.8, False),
}
ANIM_ADDITIVE = "Fire_Upper"
MIN_VALUE, MIN_SATURATION = 0.6, 0.7  # ADR 0006 section 4.3 colour rule (HSV, sRGB)


def log(msg):
    unreal.log("[VeloxImportArt] " + msg)


def fail(msg):
    unreal.log_error("[VeloxImportArt] " + msg)
    ERRORS.append(msg)


def check(cond, msg):
    if not cond:
        fail("verify: " + msg)
    return cond


def parse_args():
    argv = list(sys.argv[1:])
    extra = unreal.SystemLibrary.get_command_line().split()
    for token in extra:  # the commandlet may leave the script arguments only in the command line
        if token.startswith("--") and token not in argv:
            pass
    opts = {"animations": None, "kit": None, "static": None, "character": None, "mask": None, "fixture": False, "verify": False}
    flat = []
    for a in argv:
        flat += a.split()
    i = 0
    while i < len(flat):
        a = flat[i]
        if a == "--character" and i + 1 < len(flat) and not flat[i + 1].startswith("--"):
            opts["character"] = flat[i + 1]
            i += 1
        elif a == "--character":
            opts["character"] = ""
        elif a == "--animations" and i + 1 < len(flat) and not flat[i + 1].startswith("--"):
            opts["animations"] = flat[i + 1]
            i += 1
        elif a == "--animations":
            opts["animations"] = ""
        elif a == "--kit" and i + 1 < len(flat):
            opts["kit"] = flat[i + 1]
            i += 1
        elif a == "--static" and i + 1 < len(flat) and not flat[i + 1].startswith("--"):
            opts["static"] = flat[i + 1]
            i += 1
        elif a == "--static":
            opts["static"] = ""
        elif a == "--mask":
            opts["mask"] = flat[i + 1]
            i += 1
        elif a == "--fixture":
            opts["fixture"] = True
        elif a == "--verify":
            opts["verify"] = True
        i += 1
    return opts


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def srgb_to_linear_value(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def srgb_hsv_to_linear(h, s, v):
    r, g, b = colorsys.hsv_to_rgb(h, s, v)
    return unreal.LinearColor(srgb_to_linear_value(r), srgb_to_linear_value(g), srgb_to_linear_value(b), 1.0)


def skin_colors(palette_color):
    """(body, trim, skin) sRGB-space HSV triples for one palette entry (FColor 0-255): same hue, shifted value/saturation."""
    h, s, v = colorsys.rgb_to_hsv(palette_color.r / 255.0, palette_color.g / 255.0, palette_color.b / 255.0)
    body = (h, s, v)
    trim = (h, 1.0, 0.65)       # darker, fully saturated accent
    skin = (h, MIN_SATURATION, 1.0)  # lighter visor / skin tone
    for name, (hh, ss, vv) in (("body", body), ("trim", trim), ("skin", skin)):
        if vv < MIN_VALUE - 1e-6 or ss < MIN_SATURATION - 1e-6:
            fail(f"skin colour {name} hsv=({hh:.2f},{ss:.2f},{vv:.2f}) violates V>={MIN_VALUE} S>={MIN_SATURATION}")
    return body, trim, skin


# ---------------------------------------------------------------- textures
def import_texture(png_path, folder, name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png_path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", False)
    ATOOLS.import_asset_tasks([task])
    tex = EAL.load_asset(f"{folder}/{name}")
    if tex is None:
        fail(f"texture import failed: {png_path}")
        return None
    tex.set_editor_property("srgb", False)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("never_stream", True)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    EAL.save_loaded_asset(tex, False)
    log(f"texture {tex.get_path_name()} srgb=off masks no-mips never-stream")
    return tex


# ---------------------------------------------------------------- material
def vparam(mat, name, default, x, y):
    e = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, x, y)
    e.set_editor_property("parameter_name", name)
    e.set_editor_property("default_value", default)
    return e


def build_master(mask_tex):
    path = f"{FOLDER}/M_Character_Master"
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
    else:
        mat = ATOOLS.create_asset("M_Character_Master", FOLDER, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        fail("could not create " + path)
        return None
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property("used_with_skeletal_mesh", True)

    tex = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -1400, 0)
    tex.set_editor_property("texture", mask_tex)
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    body = vparam(mat, "BodyColor", unreal.LinearColor(1.0, 0.02, 0.02, 1.0), -1400, -300)
    trim = vparam(mat, "TrimColor", unreal.LinearColor(0.4, 0.0, 0.0, 1.0), -1400, -500)
    skin = vparam(mat, "SkinColor", unreal.LinearColor(1.0, 0.4, 0.4, 1.0), -1400, -700)
    tint = vparam(mat, "PowerupTint", unreal.LinearColor(0.0, 0.0, 0.0, 0.0), -1400, -900)
    strength = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -1400, 300)
    strength.set_editor_property("parameter_name", "EmissiveStrength")
    strength.set_editor_property("default_value", EMISSIVE_STRENGTH)

    def mul(a, a_out, b, b_out, x, y):
        m = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, x, y)
        MEL.connect_material_expressions(a, a_out, m, "A")
        MEL.connect_material_expressions(b, b_out, m, "B")
        return m

    def add(a, b, x, y):
        m = MEL.create_material_expression(mat, unreal.MaterialExpressionAdd, x, y)
        MEL.connect_material_expressions(a, "", m, "A")
        MEL.connect_material_expressions(b, "", m, "B")
        return m

    r_body = mul(tex, "R", body, "", -1000, -200)
    g_trim = mul(tex, "G", trim, "", -1000, -400)
    b_skin = mul(tex, "B", skin, "", -1000, -600)
    a_body = mul(tex, "A", body, "", -1000, -800)
    base = add(add(r_body, g_trim, -800, -300), add(b_skin, a_body, -800, -700), -600, -500)
    # PowerupTint (VeloxCombatFeedbackComponent): rgb tint, a strength; alpha 0 = no tint.
    tinted = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -400, -500)
    MEL.connect_material_expressions(base, "", tinted, "A")
    MEL.connect_material_expressions(tint, "", tinted, "B")
    MEL.connect_material_expressions(tint, "A", tinted, "Alpha")
    MEL.connect_material_property(tinted, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # Emissive = lit colour * lerp(EmissiveStrength, 1, mask.A): the whole figure glows a little, the strips fully.
    glow = MEL.create_material_expression(mat, unreal.MaterialExpressionLinearInterpolate, -800, 300)
    MEL.connect_material_expressions(strength, "", glow, "A")
    glow.set_editor_property("const_b", 1.0)
    MEL.connect_material_expressions(tex, "A", glow, "Alpha")
    emissive = mul(tinted, "", glow, "", -200, 0)
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 200)
    rough.set_editor_property("r", 1.0)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -400, 300)
    spec.set_editor_property("r", 0.0)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat, False)
    log(f"material {path} built")
    return mat


def build_skins(master):
    palette = unreal.VeloxCombatSettings.get_default_object().get_editor_property("player_color_palette")
    names = runpy_skin_names()
    mis = {}
    for index, name in enumerate(names):
        asset_name = f"MI_Skin_{name}"
        path = f"{FOLDER}/{asset_name}"
        if EAL.does_asset_exist(path):
            mi = EAL.load_asset(path)
        else:
            mi = ATOOLS.create_asset(asset_name, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        if mi is None:
            fail("could not create " + path)
            continue
        MEL.set_material_instance_parent(mi, master)
        body, trim, skin = skin_colors(palette[index])
        MEL.set_material_instance_vector_parameter_value(mi, "BodyColor", srgb_hsv_to_linear(*body))
        MEL.set_material_instance_vector_parameter_value(mi, "TrimColor", srgb_hsv_to_linear(*trim))
        MEL.set_material_instance_vector_parameter_value(mi, "SkinColor", srgb_hsv_to_linear(*skin))
        MEL.update_material_instance(mi)
        EAL.save_loaded_asset(mi, False)
        mis[name] = mi
        log(f"{asset_name}: body hsv=({body[0]:.2f},{body[1]:.2f},{body[2]:.2f}) trim v={trim[2]:.2f} skin s={skin[1]:.2f}")
    return mis


_SKIN_NAMES = None


def runpy_skin_names():
    """SKIN_NAMES of create_cosmetics_content.py without running its main (the names are the single source)."""
    global _SKIN_NAMES
    if _SKIN_NAMES is None:
        src = open(os.path.join(SCRIPT_DIR, "create_cosmetics_content.py")).read()
        for line in src.splitlines():
            if line.startswith("SKIN_NAMES"):
                _SKIN_NAMES = eval(line.split("=", 1)[1].strip())
                break
    return _SKIN_NAMES


# ---------------------------------------------------------------- skeletal mesh
def import_skeletal_mesh(fbx_path, folder, name):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", True)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("create_physics_asset", True)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    data = ui.get_editor_property("skeletal_mesh_import_data")
    data.set_editor_property("import_morph_targets", False)
    data.set_editor_property("import_mesh_lods", False)
    data.set_editor_property("update_skeleton_reference_pose", False)
    existing_skeleton = EAL.load_asset(f"{folder}/SKEL_Velox") if name == "SK_Velox" and EAL.does_asset_exist(f"{folder}/SKEL_Velox") else None
    if existing_skeleton is not None:
        ui.set_editor_property("skeleton", existing_skeleton)  # re-import: keep SKEL_Velox
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx_path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    ATOOLS.import_asset_tasks([task])
    paths = [str(p) for p in task.get_editor_property("imported_object_paths")]
    log(f"imported objects: {paths}")
    mesh = EAL.load_asset(f"{folder}/{name}")
    if mesh is None or not isinstance(mesh, unreal.SkeletalMesh):
        fail(f"skeletal mesh import failed for {fbx_path}: {mesh}")
        return None
    return mesh


def rename_asset(old, new):
    if old == new:
        return
    if EAL.does_asset_exist(new):
        EAL.delete_asset(new)
    if EAL.does_asset_exist(old) and not EAL.rename_asset(old, new):
        fail(f"rename {old} -> {new} failed")


def bone_names(skeleton):
    """Bone names via the animation pose helpers (AnimPoseExtensions); logs what exists if none works."""
    pose = skeleton.get_reference_pose()
    for holder in (getattr(unreal, "AnimPoseExtensions", None), pose):
        if holder is not None and hasattr(holder, "get_bone_names"):
            try:
                names = holder.get_bone_names(pose) if holder is not pose else holder.get_bone_names()
                return [str(n) for n in names]
            except Exception as exc:  # noqa: BLE001
                log(f"get_bone_names failed: {exc}")
    fail("no way to list bone names; AnimPose members: " + ", ".join(d for d in dir(pose) if not d.startswith("_")))
    return []


def has_bone(mesh, name):
    try:
        parent = str(mesh.get_bone_parent(name))
        return name == "root" or parent not in ("None", "")
    except Exception:  # noqa: BLE001
        return False


def add_sockets(mesh):
    """Sockets come from the FBX (SOCKET_<name> empties parented to bones, see character.py); this only reports them."""
    found = {n: mesh.find_socket(n) is not None for n in SOCKETS}
    log("sockets from FBX: " + ", ".join(f"{n}={'ok' if ok else 'MISSING'}" for n, ok in found.items()))
    EAL.save_loaded_asset(mesh, False)


def build_physics_asset(mesh, folder):
    """PA_Velox: one capsule body on pelvis (ADR 0006 section 4.1). The body list of UPhysicsAsset is not reflected for
    Python in 5.8 (no skeletal_body_setups property), so when it cannot be edited the PA is left as the importer made it
    and the gap is reported (verify warns, does not fail)."""
    pa = mesh.get_editor_property("physics_asset")
    if pa is None:  # the importer made none (create_physics_asset needs a successful bone-size pass); make an empty one
        path = f"{folder}/PA_Velox"
        pa = EAL.load_asset(path) if EAL.does_asset_exist(path) else None
        if pa is None:
            pa = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem).create_physics_asset(mesh)
            if pa is not None:
                rename_asset(pa.get_path_name().split(".")[0], path)
                pa = EAL.load_asset(path)
        if pa is None:
            fail("could not create " + path)
            return None
        mesh.set_editor_property("physics_asset", pa)
        EAL.save_loaded_asset(pa, False)
    try:
        setups = list(pa.get_editor_property("skeletal_body_setups"))
    except Exception as exc:  # noqa: BLE001
        unreal.log_warning(f"[VeloxImportArt] PA body list not editable from Python ({type(exc).__name__}); PA_Velox keeps the importer's bodies")
        return pa
    keep = [x for x in setups if str(x.get_editor_property("bone_name")) == "pelvis"]
    if not keep:
        fail("physics asset has no pelvis body")
        return pa
    agg = keep[0].get_editor_property("agg_geom")
    capsule = unreal.KSphylElem()
    capsule.set_editor_property("radius", CAPSULE_RADIUS)
    capsule.set_editor_property("length", 2.0 * (CAPSULE_HALF_HEIGHT - CAPSULE_RADIUS))
    agg.set_editor_property("sphyl_elems", [capsule])
    agg.set_editor_property("sphere_elems", [])
    agg.set_editor_property("box_elems", [])
    agg.set_editor_property("convex_elems", [])
    keep[0].set_editor_property("agg_geom", agg)
    pa.set_editor_property("skeletal_body_setups", keep)
    pa.set_editor_property("constraint_setup", [])
    return pa


def import_character(fbx_path, mask_path, folder, prefix_names=True):
    mask = import_texture(mask_path, folder, "T_Velox_Mask")
    master = build_master(mask) if mask else None
    mesh = import_skeletal_mesh(fbx_path, folder, "SK_Velox")
    if mesh is None or master is None:
        return
    skel = mesh.get_editor_property("skeleton")
    skel_path = skel.get_path_name().split(".")[0]
    if skel_path != f"{folder}/SKEL_Velox":
        rename_asset(skel_path, f"{folder}/SKEL_Velox")
    mesh = EAL.load_asset(f"{folder}/SK_Velox")
    mats = list(mesh.get_editor_property("materials"))
    if len(mats) != 1:
        fail(f"imported mesh has {len(mats)} material slots, expected 1")
    else:
        slot = mats[0]
        slot.set_editor_property("material_interface", master)
        try:
            slot.set_editor_property("material_slot_name", "M_Character_Master")
        except Exception as exc:  # noqa: BLE001
            log(f"slot name not settable: {exc}")
        mats[0] = slot
    mesh.set_editor_property("materials", mats)
    build_physics_asset(mesh, folder)
    add_sockets(mesh)
    EAL.save_loaded_asset(mesh, False)
    EAL.save_asset(f"{folder}/SKEL_Velox", False)
    EAL.save_asset(f"{folder}/PA_Velox", False)
    build_skins(master)
    log("character imported")


# ---------------------------------------------------------------- animations (task 12)
def import_animation(fbx_path, folder, name, skeleton):
    """FBX -> UAnimSequence `name` on `skeleton`. Returns the sequence or None."""
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", False)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_animations", True)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_ANIMATION)
    ui.set_editor_property("skeleton", skeleton)
    data = ui.get_editor_property("anim_sequence_import_data")
    data.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    data.set_editor_property("import_bone_tracks", True)
    data.set_editor_property("remove_redundant_keys", False)
    # 0.15 s clips (9 frames at 60 fps) are "not compatible with the import frame rate" at the 30 fps default: pin the source rate.
    data.set_editor_property("use_default_sample_rate", False)
    data.set_editor_property("custom_sample_rate", ANIM_FPS)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx_path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    ATOOLS.import_asset_tasks([task])
    paths = [str(p) for p in task.get_editor_property("imported_object_paths")]
    seq = EAL.load_asset(f"{folder}/{name}")
    if seq is None or not isinstance(seq, unreal.AnimSequence):
        fail(f"animation import failed for {fbx_path}: imported={paths} loaded={seq}")
        return None
    return seq


def seq_seconds(seq):
    return float(seq.get_editor_property("sequence_length"))


def prop_names(name):
    """The Python spelling of a UPROPERTY: the editor drops a leading b of bools (bLoop -> loop); try both."""
    return (name, "b_" + name)


def set_prop(obj, name, value):
    for candidate in prop_names(name):
        try:
            obj.set_editor_property(candidate, value)
            return True
        except Exception:  # noqa: BLE001
            pass
    fail(f"cannot set {name} on {obj.get_name()}")
    return False


def get_prop(obj, name):
    for candidate in prop_names(name):
        try:
            return obj.get_editor_property(candidate)
        except Exception:  # noqa: BLE001
            pass
    fail(f"cannot read {name} on {obj.get_name()}")
    return None


def configure_animation(seq, clip):
    seconds, loops = ANIM_CLIPS[clip]
    set_prop(seq, "enable_root_motion", False)
    set_prop(seq, "loop", loops)
    if clip == ANIM_ADDITIVE:
        set_prop(seq, "additive_anim_type", unreal.AdditiveAnimationType.AAT_LOCAL_SPACE_BASE)
        set_prop(seq, "ref_pose_type", unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME)
        set_prop(seq, "ref_frame_index", 0)
    EAL.save_loaded_asset(seq, False)


def import_animations(directory):
    skel = EAL.load_asset(f"{FOLDER}/SKEL_Velox")
    if skel is None:
        fail("SKEL_Velox missing: import the character first (--character)")
        return
    ensure_dir(ANIM_FOLDER)
    for clip in ANIM_CLIPS:
        fbx = os.path.join(directory, f"A_Velox_{clip}.fbx")
        if not os.path.exists(fbx):
            fail("missing " + fbx)
            continue
        seq = import_animation(fbx, ANIM_FOLDER, f"A_Velox_{clip}", skel)
        if seq is not None:
            configure_animation(seq, clip)
            log(f"A_Velox_{clip}: {seq_seconds(seq):.3f} s loop={ANIM_CLIPS[clip][1]}")


def verify_animations(folder=ANIM_FOLDER):
    skel = EAL.load_asset(f"{FOLDER}/SKEL_Velox")
    names = [str(p) for p in EAL.list_assets(folder, recursive=False, include_folder=False)]
    found = [n for n in names if n.split("/")[-1].startswith("A_Velox_")]
    check(len(found) == len(ANIM_CLIPS), f"animation count {len(found)} != {len(ANIM_CLIPS)}")
    for clip, (seconds, loops) in ANIM_CLIPS.items():
        seq = EAL.load_asset(f"{folder}/A_Velox_{clip}")
        if not check(seq is not None, f"A_Velox_{clip} missing"):
            continue
        length = seq_seconds(seq)
        check(abs(length - seconds) <= 1.0 / ANIM_FPS + 1e-4, f"A_Velox_{clip} length {length:.4f} s != {seconds} s (+-1 frame)")
        check(seq.get_editor_property("skeleton") == skel, f"A_Velox_{clip} skeleton")
        check(bool(get_prop(seq, "loop")) == loops, f"A_Velox_{clip} loop flag")
        additive = get_prop(seq, "additive_anim_type") != unreal.AdditiveAnimationType.AAT_NONE
        check(additive == (clip == ANIM_ADDITIVE), f"A_Velox_{clip} additive={additive}")
        if clip == ANIM_ADDITIVE:
            check(get_prop(seq, "ref_pose_type") == unreal.AdditiveBasePoseType.ABPT_ANIM_FRAME, "Fire_Upper base pose type")
            check(get_prop(seq, "ref_frame_index") == 0, "Fire_Upper base frame")
        log(f"verify: A_Velox_{clip} {length:.3f} s loop={loops} additive={additive}")
    cos = EAL.load_asset(f"{FOLDER}/DA_Cosmetics_Velox")
    if check(cos is not None, "DA_Cosmetics_Velox missing"):
        seqs = cos.get_editor_property("sequences")
        check(len(seqs) == 8, f"cosmetics Sequences has {len(seqs)} entries, expected one per EVeloxLegState (8)")
        check(len(cos.get_editor_property("run_sequences")) == 4, "cosmetics RunSequences != 4 entries")
        check(bool(cos.get_editor_property("use_sequence_playback")), "bUseSequencePlayback off")
    log("verify: animations ok")


def run_animation_fixture(fbx):
    """Self-test of import_animation without touching the real assets: re-import one clip FBX into a scratch folder under another
    name, check it, delete the scratch folder (no engine animation exists on this machine to export, see run_fixture)."""
    if not os.path.exists(fbx):
        fail("fixture: missing " + fbx)
        return
    skel = EAL.load_asset(f"{FOLDER}/SKEL_Velox")
    if skel is None:
        fail("fixture: SKEL_Velox missing")
        return
    ensure_dir(SCRATCH)
    seq = import_animation(fbx, SCRATCH, "A_Fixture_Idle", skel)
    if seq is not None:
        length = seq_seconds(seq)
        log(f"fixture: re-imported {seq.get_path_name()} {length:.3f} s")
        check(abs(length - ANIM_CLIPS["Idle"][0]) <= 1.0 / ANIM_FPS + 1e-4, "fixture length")
        check(seq.get_editor_property("skeleton") == skel, "fixture skeleton")
    EAL.delete_directory(SCRATCH)
    check(not EAL.does_directory_exist(SCRATCH) or not EAL.list_assets(SCRATCH), "fixture scratch assets not deleted")
    log("fixture: scratch assets deleted")


# ---------------------------------------------------------------- verify
def lfs_total_bytes():
    total = 0
    for root in ("Content", "ArtSource"):
        for dirpath, _, files in os.walk(os.path.join(REPO, root)):
            for f in files:
                if f.endswith((".uasset", ".umap", ".fbx", ".blend", ".png", ".wav", ".ogg", ".tga", ".jpg")):
                    total += os.path.getsize(os.path.join(dirpath, f))
    return total


def verify_character(folder):
    mesh = EAL.load_asset(f"{folder}/SK_Velox")
    if not check(mesh is not None, "SK_Velox missing"):
        return
    skel = EAL.load_asset(f"{folder}/SKEL_Velox")
    check(skel is not None, "SKEL_Velox missing")
    check(mesh.get_editor_property("skeleton") == skel, "SK_Velox.skeleton is not SKEL_Velox")
    names = bone_names(skel)
    log(f"verify: {len(names)} bones")
    check(0 < len(names) <= MAX_BONES, f"bone count {len(names)} (limit {MAX_BONES})")
    for needed in ("root", "pelvis", "spine_05", "head", "hand_r", "hand_l", "ball_l", "ball_r", "thigh_l", "calf_r"):
        check(has_bone(mesh, needed), f"bone {needed} missing")
    for sname in SOCKETS:
        sock = mesh.find_socket(sname)
        check(sock is not None and str(sock.get_editor_property("bone_name")) == SOCKETS[sname][0], f"socket {sname}")
    check(len(mesh.get_editor_property("materials")) == 1, "material slot count != 1")
    pa = EAL.load_asset(f"{folder}/PA_Velox")
    if check(pa is not None, "PA_Velox missing"):
        check(mesh.get_editor_property("physics_asset") == pa, "SK_Velox.physics_asset is not PA_Velox")
        try:
            setups = pa.get_editor_property("skeletal_body_setups")
            check(len(setups) == 1 and str(setups[0].get_editor_property("bone_name")) == "pelvis", "PA_Velox is not one pelvis body")
        except Exception:  # noqa: BLE001
            unreal.log_warning("[VeloxImportArt] verify: PA body list not readable from Python; body count unchecked")
    bounds = mesh.get_bounds()
    height = bounds.box_extent.z * 2.0
    log(f"verify: bounds origin={bounds.origin} extent={bounds.box_extent} height={height:.1f}")
    check(abs(height - HEIGHT_CM) < 2.0, f"height {height:.1f} != {HEIGHT_CM}")
    tris = tri_count(mesh, os.path.join(REPO, "ArtSource", "Export", "SK_Velox.json"))
    log(f"verify: triangles={tris} imported_verts={num_verts(mesh)}")
    check(0 < tris <= MAX_TRIS, f"triangles {tris} over budget {MAX_TRIS}")
    tex = EAL.load_asset(f"{folder}/T_Velox_Mask")
    if check(tex is not None, "T_Velox_Mask missing"):
        check(not tex.get_editor_property("srgb"), "mask texture is sRGB")
        check(bool(tex.get_editor_property("never_stream")), "mask texture streams")
    master = EAL.load_asset(f"{folder}/M_Character_Master")
    if check(master is not None, "M_Character_Master missing"):
        vecs = [str(n) for n in MEL.get_vector_parameter_names(master)]
        scal = [str(n) for n in MEL.get_scalar_parameter_names(master)]
        for p in ("BodyColor", "TrimColor", "SkinColor"):
            check(p in vecs, f"master vector parameter {p}")
        check("EmissiveStrength" in scal, "master scalar parameter EmissiveStrength")
        check(abs(MEL.get_material_default_scalar_parameter_value(master, "EmissiveStrength") - EMISSIVE_STRENGTH) < 1e-4, "EmissiveStrength default")
    for name in runpy_skin_names():
        mi = EAL.load_asset(f"{folder}/MI_Skin_{name}")
        if check(mi is not None, f"MI_Skin_{name} missing"):
            check(mi.get_editor_property("parent") == master, f"MI_Skin_{name} parent")
            for p in ("BodyColor", "TrimColor", "SkinColor"):
                c = MEL.get_material_instance_vector_parameter_value(mi, p)
                hsv = colorsys.rgb_to_hsv(*[linear_to_srgb(x) for x in (c.r, c.g, c.b)])
                check(hsv[1] >= MIN_SATURATION - 1e-3 and hsv[2] >= MIN_VALUE - 1e-3, f"MI_Skin_{name}.{p} hsv={hsv}")
    cos = EAL.load_asset(f"{folder}/DA_Cosmetics_Velox")
    if check(cos is not None, "DA_Cosmetics_Velox missing"):
        check(cos.get_editor_property("skeletal_mesh") is not None, "cosmetics.SkeletalMesh empty")
        skins = cos.get_editor_property("skins")
        check(len(skins) == len(runpy_skin_names()), "cosmetics skin count")
        for s in skins:
            check(s.get_editor_property("material") is not None, f"skin {s.get_editor_property('name')} has no material")
    total = lfs_total_bytes()
    log(f"verify: LFS-tracked bytes Content+ArtSource = {total / 1048576.0:.1f} MB (budget {LFS_BUDGET_BYTES / 1048576:.0f} MB)")
    check(total <= LFS_BUDGET_BYTES, "LFS budget")


def linear_to_srgb(x):
    return 12.92 * x if x <= 0.0031308 else 1.055 * (x ** (1 / 2.4)) - 0.055


def num_verts(mesh):
    for holder in (getattr(unreal, "EditorSkeletalMeshLibrary", None), unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)):
        if holder is not None and hasattr(holder, "get_num_verts"):
            return holder.get_num_verts(mesh, 0)
    return 0


def tri_count(mesh, sidecar=None):
    """Triangle count: the sidecar written by character.py (the import API exposes only vertex counts), else the
    vertex count / 3 lower bound... flagged in the log."""
    if sidecar and os.path.exists(sidecar):
        import json
        with open(sidecar) as f:
            return int(json.load(f)["triangles"])
    verts = num_verts(mesh)
    log(f"no triangle sidecar; vertex count {verts} (flat shading: verts = 3 * tris)")
    return verts // 3


# ---------------------------------------------------------------- static meshes: weapons and items (task 13)
WEAPON_MESH_DIR = "/Game/Weapons/Meshes"
ITEM_MESH_DIR = "/Game/Items/Meshes"
PALETTE_TEX_NAME = "T_Items_Palette"
STATIC_FIXTURE_DIR = SCRATCH + "/Static"
MAX_WEAPON_TRIS = 1500
MAX_ITEM_TRIS = 400
MAX_ITEM_HEIGHT = 60.0
MUZZLE_SOCKET = "muzzle"
# material name -> (folder, EmissiveStrength): lit, but every texel also glows a little so the pickups and the view model stay
# readable in baked-dark corners (ADR 0006 section 4.3 readability rule).
STATIC_MATERIALS = {"M_Items_Palette": (ITEM_MESH_DIR, 0.45), "M_Weapon_Flat": (WEAPON_MESH_DIR, 0.3)}


def build_palette_material(name, folder, tex, emissive):
    path = f"{folder}/{name}"
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
    else:
        mat = ATOOLS.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        fail("could not create " + path)
        return None
    MEL.delete_all_material_expressions(mat)
    sample = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -800, 0)
    sample.set_editor_property("texture", tex)
    MEL.connect_material_property(sample, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    strength = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -800, 250)
    strength.set_editor_property("parameter_name", "EmissiveStrength")
    strength.set_editor_property("default_value", emissive)
    glow = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -500, 100)
    MEL.connect_material_expressions(sample, "RGB", glow, "A")
    MEL.connect_material_expressions(strength, "", glow, "B")
    MEL.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 300)
    rough.set_editor_property("r", 1.0)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    spec = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -500, 400)
    spec.set_editor_property("r", 0.0)
    MEL.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat, False)
    log(f"material {path} built (EmissiveStrength {emissive})")
    return mat


def import_palette(png_path):
    ensure_dir(ITEM_MESH_DIR)
    ensure_dir(WEAPON_MESH_DIR)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", png_path)
    task.set_editor_property("destination_path", ITEM_MESH_DIR)
    task.set_editor_property("destination_name", PALETTE_TEX_NAME)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("save", False)
    ATOOLS.import_asset_tasks([task])
    tex = EAL.load_asset(f"{ITEM_MESH_DIR}/{PALETTE_TEX_NAME}")
    if tex is None:
        fail("palette import failed: " + png_path)
        return {}
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property("never_stream", True)
    tex.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
    EAL.save_loaded_asset(tex, False)
    return {name: build_palette_material(name, folder, tex, strength) for name, (folder, strength) in STATIC_MATERIALS.items()}


def import_static_fbx(fbx_path, dest_dir, name):
    """Static mesh without collision, without generated lightmap UVs (weapons and pickups are dynamic objects), no materials or
    textures from the FBX; a `SOCKET_<name>` child null of the mesh becomes the socket <name>."""
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("automated_import_should_detect_type", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    data = ui.static_mesh_import_data
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("generate_lightmap_u_vs", False)
    data.set_editor_property("remove_degenerates", True)
    data.set_editor_property("normal_import_method", unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
    data.set_editor_property("normal_generation_method", unreal.FBXNormalGenerationMethod.MIKK_T_SPACE)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx_path)
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", ui)
    ATOOLS.import_asset_tasks([task])
    mesh = EAL.load_asset(f"{dest_dir}/{name}")
    if mesh is None:
        fail("static import failed: " + fbx_path)
    return mesh


def configure_static(mesh, material):
    sub = unreal.EditorStaticMeshLibrary
    sub.remove_collisions(mesh)
    body = mesh.get_editor_property("body_setup")
    inst = body.get_editor_property("default_instance")
    inst.set_editor_property("collision_profile_name", "NoCollision")
    body.set_editor_property("default_instance", inst)
    if material is not None:
        mesh.set_material(0, material)
    bs = sub.get_lod_build_settings(mesh, 0)
    before = bs.get_editor_property("generate_lightmap_u_vs")
    bs.set_editor_property("generate_lightmap_u_vs", False)
    sub.set_lod_build_settings(mesh, 0, bs)
    log(f"configure {mesh.get_name()}: generate_lightmap_u_vs {before} -> "
        f"{sub.get_lod_build_settings(mesh, 0).get_editor_property('generate_lightmap_u_vs')}")
    if not EAL.save_loaded_asset(mesh, False):
        fail("could not save " + mesh.get_path_name())


def mesh_socket_names(mesh):
    # StaticMesh.Sockets is protected for Python, so probe the one name this pipeline uses (the C++ test counts them).
    return [name for name in (MUZZLE_SOCKET,) if mesh.find_socket(name) is not None]


def verify_static_mesh(mesh, max_tris, expect_uv=1, expect_socket=None, expect_tris=None, expect_bounds=None, max_height=None, check_lightmap_flag=False):
    """Asserts the ADR 0006 section 8 budgets and the import settings of one static mesh; returns its triangle count."""
    n = mesh.get_name()
    # get_num_uv_channels returns 0 for imported meshes; the render-data count (get_num_tex_coords) is the real one (task 14).
    uv = mesh.get_num_tex_coords(0)
    check(uv == expect_uv, f"{n}: {uv} UV channels, expected {expect_uv} (UV0 palette, no generated lightmap UV)")
    tris = mesh.get_num_triangles(0)
    check(0 < tris <= max_tris, f"{n}: {tris} triangles (limit {max_tris})")
    if expect_tris is not None:
        check(tris == expect_tris, f"{n}: {tris} triangles, manifest says {expect_tris}")
    check(len(mesh.get_editor_property("static_materials")) == 1, f"{n}: material slots != 1")
    bs = unreal.EditorStaticMeshLibrary.get_lod_build_settings(mesh, 0)
    log(f"verify {n}: build settings generate_lightmap_u_vs={bs.get_editor_property('generate_lightmap_u_vs')}")
    # The Python read-back of this flag stays True whatever is written (observed 5.8: "True -> True" after set_lod_build_settings with
    # False), so it is only logged. The render-data UV count above is the real proof: a generated lightmap UV would add a channel.
    if check_lightmap_flag:
        check(not bs.get_editor_property("generate_lightmap_u_vs"), f"{n}: lightmap UV generation still on")
    prof = mesh.get_editor_property("body_setup").get_editor_property("default_instance").get_editor_property("collision_profile_name")
    check(str(prof) == "NoCollision", f"{n}: collision profile {prof}")
    agg = mesh.get_editor_property("body_setup").get_editor_property("agg_geom")
    shapes = sum(len(agg.get_editor_property(k)) for k in ("sphere_elems", "box_elems", "sphyl_elems", "convex_elems"))
    check(shapes == 0, f"{n}: {shapes} simple collision shapes")
    bb = mesh.get_bounds()
    lo = [bb.origin.x - bb.box_extent.x, bb.origin.y - bb.box_extent.y, bb.origin.z - bb.box_extent.z]
    hi = [bb.origin.x + bb.box_extent.x, bb.origin.y + bb.box_extent.y, bb.origin.z + bb.box_extent.z]
    if expect_bounds is not None:  # the axis convention of weapons-spec.md survived the FBX round trip (Blender +Y -> Unreal -Y)
        check(all(abs(a - b) <= 0.5 for a, b in zip(lo + hi, list(expect_bounds[0]) + list(expect_bounds[1]))),
              f"{n}: bounds {[round(v, 1) for v in lo]}..{[round(v, 1) for v in hi]} != manifest {expect_bounds}")
    if max_height is not None:
        check(abs(lo[2]) <= 0.5 and hi[2] <= max_height + 0.5, f"{n}: z range {lo[2]:.1f}..{hi[2]:.1f} (base origin, max {max_height})")
    sockets = mesh_socket_names(mesh)
    if expect_socket is not None:
        name, loc = expect_socket
        check(sockets == [name], f"{n}: sockets {sockets}, expected [{name}]")
        sock = mesh.find_socket(name)
        if sock is not None and loc is not None:
            rel = sock.get_editor_property("relative_location")
            check(abs(rel.x - loc[0]) <= 0.6 and abs(rel.y - loc[1]) <= 0.6 and abs(rel.z - loc[2]) <= 0.6,
                  f"{n}: socket {name} at ({rel.x:.1f}, {rel.y:.1f}, {rel.z:.1f}), manifest {loc}")
    else:
        check(not sockets, f"{n}: unexpected sockets {sockets}")
    log(f"verify {n}: tris={tris} uv={uv} slots=1 bounds={[round(v, 1) for v in lo]}..{[round(v, 1) for v in hi]} sockets={sockets}")
    return tris


def import_static(static_dir, verify):
    import json
    static_dir = os.path.abspath(static_dir)
    manifest_path = os.path.join(static_dir, "weapons_manifest.json")
    if not os.path.exists(manifest_path):
        fail("missing " + manifest_path + " (run Tools/blender/weapons_items.py)")
        return
    with open(manifest_path) as fh:
        manifest = json.load(fh)
    png = os.path.join(REPO, "ArtSource", "Textures", PALETTE_TEX_NAME + ".png")
    materials = import_palette(png)
    if ERRORS:
        return
    total_w = total_i = 0
    for kind, dest, mat_name, max_tris in (("weapons", WEAPON_MESH_DIR, "M_Weapon_Flat", MAX_WEAPON_TRIS), ("items", ITEM_MESH_DIR, "M_Items_Palette", MAX_ITEM_TRIS)):
        for entry in manifest[kind]:
            mesh = import_static_fbx(os.path.join(static_dir, entry["fbx"]), dest, entry["name"])
            if mesh is None:
                continue
            configure_static(mesh, materials.get(mat_name))
            tris = 0
            if verify:
                is_weapon = kind == "weapons"
                tris = verify_static_mesh(
                    mesh, max_tris, expect_tris=entry["tris"], expect_bounds=(entry["bounds_min"], entry["bounds_max"]),
                    expect_socket=(MUZZLE_SOCKET, entry["muzzle"]) if is_weapon else None, max_height=None if is_weapon else MAX_ITEM_HEIGHT)
                check(mesh.get_material(0) == materials.get(mat_name), f"{entry['name']}: slot 0 is not {mat_name}")
            if kind == "weapons":
                total_w += tris
            else:
                total_i += tris
    log(f"static: {len(manifest['weapons'])} weapons ({total_w} tris), {len(manifest['items'])} items ({total_i} tris) verified={verify}")
    for script in ("create_cosmetics_content.py", "create_item_content.py"):  # point DA_WeaponCosmetics_* / DA_Item_* at the meshes
        runpy.run_path(os.path.join(SCRIPT_DIR, script), run_name="__velox_import__")
    if verify:
        check(lfs_total_bytes() <= LFS_BUDGET_BYTES, "LFS budget exceeded")
        log(f"LFS total {lfs_total_bytes() / 1048576:.1f} MiB of {LFS_BUDGET_BYTES // 1048576}")


def run_static_fixture():
    """Self-test of the static import path: /Engine/BasicShapes/Cylinder -> FBX -> the same import/configure/verify code ->
    a `muzzle` socket added through Python (the FBX has no SOCKET_ node) -> verified -> scratch assets deleted."""
    scratch_disk = os.path.join(unreal.Paths.project_saved_dir(), "StaticFixture")
    os.makedirs(scratch_disk, exist_ok=True)
    fbx = os.path.join(scratch_disk, "SM_Fixture.fbx")
    if EAL.does_directory_exist(STATIC_FIXTURE_DIR):
        EAL.delete_directory(STATIC_FIXTURE_DIR)
    ensure_dir(STATIC_FIXTURE_DIR)
    src = EAL.duplicate_asset("/Engine/BasicShapes/Cylinder", STATIC_FIXTURE_DIR + "/Cylinder_Src")
    if src is None:
        fail("fixture: cannot duplicate /Engine/BasicShapes/Cylinder")
        return
    src_uvs = src.get_num_tex_coords(0)
    exp = unreal.AssetExportTask()
    exp.set_editor_property("object", src)
    exp.set_editor_property("filename", fbx)
    exp.set_editor_property("automated", True)
    exp.set_editor_property("replace_identical", True)
    exp.set_editor_property("exporter", unreal.StaticMeshExporterFBX())
    if not unreal.Exporter.run_asset_export_task(exp) or not os.path.exists(fbx):
        fail("fixture: export failed")
        return
    mesh = import_static_fbx(fbx, STATIC_FIXTURE_DIR, "SM_Fixture")
    if mesh is not None:
        configure_static(mesh, None)
        log(f"fixture source has {src_uvs} UV channel(s)")
        verify_static_mesh(mesh, MAX_WEAPON_TRIS, expect_uv=src_uvs, check_lightmap_flag=False)
        socket = unreal.StaticMeshSocket(mesh)
        socket.set_editor_property("socket_name", MUZZLE_SOCKET)
        socket.set_editor_property("relative_location", unreal.Vector(30.0, 0.0, 0.0))
        mesh.add_socket(socket)
        check(mesh.find_socket(MUZZLE_SOCKET) is not None, "fixture: muzzle socket not found after add_socket")
        check(mesh_socket_names(mesh) == [MUZZLE_SOCKET], "fixture: socket list wrong")
        log("fixture ok (cylinder imported without lightmap UVs or collision, muzzle socket added and found)")
    src = mesh = socket = None  # drop the Python references so the assets can be deleted
    for asset in ("Cylinder_Src", "SM_Fixture"):
        if EAL.does_asset_exist(f"{STATIC_FIXTURE_DIR}/{asset}"):
            EAL.delete_asset(f"{STATIC_FIXTURE_DIR}/{asset}")
    EAL.delete_directory(STATIC_FIXTURE_DIR)
    check(not any(EAL.does_asset_exist(f"{STATIC_FIXTURE_DIR}/{a}") for a in ("Cylinder_Src", "SM_Fixture")), "fixture: scratch assets not deleted")
    log("static fixture: scratch assets deleted")


# ---------------------------------------------------------------- fixture
def run_fixture(fbx):
    """Self-test of the import code path without touching the real assets: import the FBX into a scratch folder under
    another name, verify it, delete the scratch assets. (Exporting an engine skeletal mesh with AssetExportTask asserts in
    SkinnedMeshComponent.cpp under -nullrhi on 5.8, so the source FBX is the one character.py wrote.)"""
    if not os.path.exists(fbx):
        fail("fixture: missing " + fbx)
        return
    ensure_dir(SCRATCH)
    mesh = import_skeletal_mesh(fbx, SCRATCH, "SK_Fixture")
    if mesh is not None:
        names = bone_names(mesh.get_editor_property("skeleton"))
        tris = tri_count(mesh, os.path.join(os.path.dirname(fbx), "SK_Velox.json"))
        log(f"fixture: re-imported {len(names)} bones, tris={tris}, verts={num_verts(mesh)}, slots={len(mesh.get_editor_property('materials'))}")
        check(0 < len(names) <= MAX_BONES, "fixture bone count")
        check(tris > 0 and num_verts(mesh) > 0, "fixture geometry")
        check(has_bone(mesh, "pelvis"), "fixture pelvis")
        check(mesh.find_socket("weapon_r") is not None, "fixture socket weapon_r")
    EAL.delete_directory(SCRATCH)
    check(not EAL.does_directory_exist(SCRATCH) or not EAL.list_assets(SCRATCH), "fixture scratch assets not deleted")
    log("fixture: scratch assets deleted")


def main():
    opts = parse_args()
    log(f"options {opts}")
    ensure_dir(FOLDER)
    if opts["character"] and not opts["fixture"]:
        fbx = os.path.abspath(opts["character"])
        mask = os.path.abspath(opts["mask"]) if opts["mask"] else os.path.join(REPO, "ArtSource", "Textures", "T_Velox_Mask.png")
        for p in (fbx, mask):
            if not os.path.exists(p):
                fail("missing input " + p)
        if not ERRORS:
            import_character(fbx, mask, FOLDER)
            runpy.run_path(os.path.join(SCRIPT_DIR, "create_cosmetics_content.py"), run_name="__velox_import__")
    if opts["animations"] is not None:
        directory = os.path.abspath(opts["animations"] or os.path.join(REPO, "ArtSource", "Export"))
        if opts["fixture"]:
            run_animation_fixture(os.path.join(directory, "A_Velox_Idle.fbx"))
        else:
            import_animations(directory)
            if not ERRORS:
                runpy.run_path(os.path.join(SCRIPT_DIR, "create_cosmetics_content.py"), run_name="__velox_import__")
            if opts["verify"]:
                verify_animations()
    if opts["kit"]:
        sys.path.insert(0, SCRIPT_DIR)
        import import_kit
        import_kit.run(os.path.abspath(opts["kit"]), opts["verify"], opts["fixture"])
    if opts["static"] is not None:
        if opts["fixture"]:
            run_static_fixture()
        elif opts["static"]:
            import_static(opts["static"], opts["verify"])
        else:
            fail("--static needs a directory (ArtSource/Export)")
    if opts["fixture"] and not opts["kit"] and opts["static"] is None and opts["animations"] is None:
        run_fixture(os.path.abspath(opts["character"]) if opts["character"] else os.path.join(REPO, "ArtSource", "Export", "SK_Velox.fbx"))
    if opts["verify"] and not opts["fixture"] and not opts["kit"] and opts["static"] is None and opts["animations"] is None:
        verify_character(FOLDER)
    if ERRORS:
        raise RuntimeError(f"import_art.py: {len(ERRORS)} error(s): {ERRORS}")
    log("done")


main()
