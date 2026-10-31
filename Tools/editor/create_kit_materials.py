"""Velox modular kit content (ADR 0006 sections 2, 5; task 14): kit textures, M_Kit_Master, MI_Kit_*, TC_Sky_DM1.

Run headless after `blender -b --factory-startup --python Tools/blender/kit.py` (it writes ArtSource/Textures and
ArtSource/Export/kit_manifest.json, the single source of the material table):
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=$PWD/Tools/editor/create_kit_materials.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Creates /Game/Environment/Kit/Textures/T_Kit_*, /Game/Environment/Kit/Materials/M_Kit_Master and MI_Kit_* (parameters
Tint, BaseTex, Roughness, EmissiveStrength), and /Game/Environment/Lighting/TC_Sky_DM1 (128 px TextureCube from the
2:1 HDR). Idempotent. Asserts the style-guide palette on every instance except the emissive fixture (sRGB V <= 0.5,
S <= 0.25) and fails (exception) on any error.
"""
import colorsys
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ERRORS = []

KIT = "/Game/Environment/Kit"
TEX_DIR = KIT + "/Textures"
MAT_DIR = KIT + "/Materials"
LIGHT_DIR = "/Game/Environment/Lighting"
MASTER = "M_Kit_Master"
WHITE = "/Engine/EngineResources/WhiteSquareTexture"
MAX_V, MAX_S = 0.5, 0.25
PALETTE_EXEMPT = ("Light",)
SKY_NAME = "TC_Sky_DM1"


def log(msg):
    unreal.log("[VeloxKit] " + msg)


def fail(msg):
    unreal.log_error("[VeloxKit] " + msg)
    ERRORS.append(msg)


def project_dir():
    return os.path.normpath(unreal.Paths.project_dir())


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def linear_to_srgb(c):
    return c * 12.92 if c <= 0.0031308 else 1.055 * (c ** (1 / 2.4)) - 0.055


def import_file(filename, dest_dir, name):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", dest_dir)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("replace_existing_settings", True)
    task.set_editor_property("automated", True)
    task.set_editor_property("save", True)
    ATOOLS.import_asset_tasks([task])
    asset = EAL.load_asset(f"{dest_dir}/{name}")
    if asset is None:
        fail(f"import failed: {filename}")
    return asset


def import_textures(manifest):
    out = {}
    ensure_dir(TEX_DIR)
    for name in sorted(manifest["textures"]):
        src = os.path.join(project_dir(), "ArtSource", "Textures", name + ".png")
        tex = import_file(src, TEX_DIR, name)
        if tex is None:
            continue
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        tex.set_editor_property("address_x", unreal.TextureAddress.TA_WRAP)
        tex.set_editor_property("address_y", unreal.TextureAddress.TA_WRAP)
        tex.set_editor_property("never_stream", False)
        w, h = tex.blueprint_get_size_x(), tex.blueprint_get_size_y()
        if max(w, h) > 1024:
            fail(f"{name} is {w}x{h}, budget is 1024")
        if not EAL.save_loaded_asset(tex, False):
            fail(f"could not save {name}")
        out[name] = tex
        log(f"texture {name} {w}x{h}")
    return out


def create_master():
    ensure_dir(MAT_DIR)
    path = f"{MAT_DIR}/{MASTER}"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)  # rebuilt every run: the graph is code
    mat = ATOOLS.create_asset(MASTER, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        fail("could not create " + MASTER)
        return None
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)

    def node(cls, x, y):
        return MEL.create_material_expression(mat, cls, x, y)

    tex = node(unreal.MaterialExpressionTextureSampleParameter2D, -700, -200)
    tex.set_editor_property("parameter_name", "BaseTex")
    tex.set_editor_property("texture", unreal.load_asset(WHITE))
    tint = node(unreal.MaterialExpressionVectorParameter, -700, 0)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(0.2, 0.2, 0.2, 1))
    rough = node(unreal.MaterialExpressionScalarParameter, -700, 200)
    rough.set_editor_property("parameter_name", "Roughness")
    rough.set_editor_property("default_value", 0.8)
    emis = node(unreal.MaterialExpressionScalarParameter, -700, 350)
    emis.set_editor_property("parameter_name", "EmissiveStrength")
    emis.set_editor_property("default_value", 0.0)
    mul_base = node(unreal.MaterialExpressionMultiply, -400, -100)
    mul_emis = node(unreal.MaterialExpressionMultiply, -400, 300)
    MEL.connect_material_expressions(tex, "RGB", mul_base, "A")
    MEL.connect_material_expressions(tint, "", mul_base, "B")
    MEL.connect_material_expressions(tint, "", mul_emis, "A")
    MEL.connect_material_expressions(emis, "", mul_emis, "B")
    MEL.connect_material_property(mul_base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    MEL.connect_material_property(mul_emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.recompile_material(mat)
    if not EAL.save_loaded_asset(mat, False):
        fail("could not save " + MASTER)
    return mat


def create_instances(manifest, master, textures):
    result = {}
    for name, spec in sorted(manifest["materials"].items()):
        iname = "MI_Kit_" + name
        path = f"{MAT_DIR}/{iname}"
        if EAL.does_asset_exist(path):
            mi = EAL.load_asset(path)
        else:
            mi = ATOOLS.create_asset(iname, MAT_DIR, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        if mi is None:
            fail("could not create " + iname)
            continue
        mi.set_editor_property("parent", master)
        r, g, b = (srgb_to_linear(c) for c in spec["tint"])
        MEL.set_material_instance_vector_parameter_value(mi, "Tint", unreal.LinearColor(r, g, b, 1.0))
        MEL.set_material_instance_scalar_parameter_value(mi, "Roughness", spec["roughness"])
        MEL.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", spec["emissive"])
        if spec["texture"]:
            MEL.set_material_instance_texture_parameter_value(mi, "BaseTex", textures[spec["texture"]])
        MEL.update_material_instance(mi)
        if not EAL.save_loaded_asset(mi, False):
            fail("could not save " + iname)
        result[name] = mi
    return result


def assert_palette(instances):
    """Style guide: environment albedo sRGB V <= 0.5, S <= 0.25. Read back from the saved instances; the emissive
    fixture is exempt."""
    for name, mi in instances.items():
        if name in PALETTE_EXEMPT:
            continue
        lin = MEL.get_material_instance_vector_parameter_value(mi, "Tint")
        rgb = [linear_to_srgb(c) for c in (lin.r, lin.g, lin.b)]
        _h, s, v = colorsys.rgb_to_hsv(*rgb)
        if v > MAX_V + 1e-3 or s > MAX_S + 1e-3:
            fail(f"palette: MI_Kit_{name} V={v:.3f} S={s:.3f} (limit V<={MAX_V}, S<={MAX_S})")
        else:
            log(f"palette ok MI_Kit_{name} V={v:.2f} S={s:.2f}")
    if len(instances) > 8:
        fail(f"{len(instances)} material instances, budget is 8")


def create_sky():
    ensure_dir(LIGHT_DIR)
    src = os.path.join(project_dir(), "ArtSource", "Textures", SKY_NAME + ".hdr")
    path = f"{LIGHT_DIR}/{SKY_NAME}"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    asset = import_file(src, LIGHT_DIR, SKY_NAME)
    if asset is None:
        return
    if not isinstance(asset, unreal.TextureCube):
        fail(f"{SKY_NAME} imported as {asset.get_class().get_name()}, expected TextureCube")
        return
    asset.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_HDR_COMPRESSED)
    asset.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    asset.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_SKYBOX)
    asset.set_editor_property("srgb", False)
    if not EAL.save_loaded_asset(asset, False):
        fail("could not save " + SKY_NAME)
    size = asset.blueprint_get_built_texture_size()
    log(f"{SKY_NAME}: TextureCube built size {size}")
    if int(size.x) != 128 or int(size.y) != 128:
        fail(f"{SKY_NAME} is {size}, expected 128 px faces")


def main():
    mpath = os.path.join(project_dir(), "ArtSource", "Export", "kit_manifest.json")
    if not os.path.exists(mpath):
        raise RuntimeError("missing " + mpath + " - run Tools/blender/kit.py first")
    with open(mpath) as fh:
        manifest = json.load(fh)
    ensure_dir(KIT)
    textures = import_textures(manifest)
    master = create_master()
    instances = create_instances(manifest, master, textures) if master else {}
    assert_palette(instances)
    create_sky()
    if ERRORS:
        raise RuntimeError("create_kit_materials failed: " + "; ".join(ERRORS))
    log(f"done: {len(textures)} textures, {len(instances)} instances, {MASTER}, {SKY_NAME}")


main()
