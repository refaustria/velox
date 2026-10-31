"""Velox cosmetics content (ADR 0006 sections 4.3-4.5): DA_Cosmetics_Velox, wired into DA_PawnData_Default.

Run headless after create_bootstrap_content.py (it needs DA_PawnData_Default):
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=$PWD/Tools/editor/create_cosmetics_content.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: an existing asset is loaded and its properties re-applied. SK_Velox and MI_Skin_* are linked when task 11's import_art.py has
imported them; ABP_Velox and A_Velox_* stay empty until task 12; the pawn then keeps its PlaceholderBody.
Skins are in the old PlayerColorPalette order (UVeloxCombatSettings), so SkinIndex still names the HUD colour; their
BodyColor is that palette entry converted to linear. Fails (exception) if anything could not be created or saved.
"""
import unreal

EAL = unreal.EditorAssetLibrary
ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
ERRORS = []

COSMETICS_FOLDER = "/Game/Characters/Velox"
COSMETICS_NAME = "DA_Cosmetics_Velox"
PAWN_DATA_PATH = "/Game/Data/DA_PawnData_Default"
# Created by create_bootstrap_content.py (task 09); only referenced here, never created.
VIEW_MODEL_SETTINGS_PATH = "/Game/Characters/Velox/DA_ViewModelSettings_Default"

# ---- task 08: character cosmetics
# Names of the first six palette entries (UVeloxCombatSettings::PlayerColorPalette order). ADR 0006 section 4.3 names an
# Orange skin; palette index 4 is magenta, and keeping the HUD colours stable wins (task 08 report).
SKIN_NAMES = ["Red", "Blue", "Green", "Yellow", "Magenta", "Cyan"]


def log(msg):
    unreal.log("[VeloxCosmetics] " + msg)


def fail(msg):
    unreal.log_error("[VeloxCosmetics] " + msg)
    ERRORS.append(msg)


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def get_or_create(folder, name, asset_class, factory):
    path = f"{folder}/{name}"
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path), False
    asset = ATOOLS.create_asset(name, folder, asset_class, factory)
    if asset is None:
        fail(f"could not create {path}")
    return asset, True


def save(asset):
    if asset is not None and not EAL.save_loaded_asset(asset, False):
        fail(f"could not save {asset.get_path_name()}")


def srgb_to_linear(channel):
    c = channel / 255.0
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def to_linear(color):
    """FColor (sRGB) -> FLinearColor, as FLinearColor::FromSRGBColor."""
    if hasattr(unreal.MathLibrary, "conv_color_to_linear_color"):
        return unreal.MathLibrary.conv_color_to_linear_color(color)
    return unreal.LinearColor(srgb_to_linear(color.r), srgb_to_linear(color.g), srgb_to_linear(color.b), 1.0)


def build_character_cosmetics():
    palette = unreal.VeloxCombatSettings.get_default_object().get_editor_property("player_color_palette")
    if len(palette) < len(SKIN_NAMES):
        fail(f"PlayerColorPalette has {len(palette)} entries, need {len(SKIN_NAMES)}")
        return None
    ensure_dir(COSMETICS_FOLDER)
    cosmetics, created = get_or_create(COSMETICS_FOLDER, COSMETICS_NAME, unreal.VeloxCharacterCosmetics, None)
    if cosmetics is None:
        return None
    skins = []
    for index, name in enumerate(SKIN_NAMES):
        # Struct members are set through the constructor (Tools/editor/README.md).
        # MI_Skin_<name> (task 11, import_art.py) is linked when it exists.
        mi_path = f"{COSMETICS_FOLDER}/MI_Skin_{name}"
        material = EAL.load_asset(mi_path) if EAL.does_asset_exist(mi_path) else None
        if material is not None:
            skins.append(unreal.VeloxSkin(name=name, material=material, body_color=to_linear(palette[index])))
        else:
            skins.append(unreal.VeloxSkin(name=name, body_color=to_linear(palette[index])))
    cosmetics.set_editor_property("skins", skins)
    cosmetics.set_editor_property("body_color_parameter", "BodyColor")
    mesh_path = f"{COSMETICS_FOLDER}/SK_Velox"
    if EAL.does_asset_exist(mesh_path):
        cosmetics.set_editor_property("skeletal_mesh", EAL.load_asset(mesh_path))
    cosmetics.set_editor_property("mesh_offset_z", -70.0)
    cosmetics.set_editor_property("mesh_yaw_deg", -90.0)
    cosmetics.set_editor_property("use_sequence_playback", True)
    if EAL.does_asset_exist(VIEW_MODEL_SETTINGS_PATH):
        cosmetics.set_editor_property("view_model_settings", EAL.load_asset(VIEW_MODEL_SETTINGS_PATH))
    else:
        fail(f"{VIEW_MODEL_SETTINGS_PATH} missing: run create_bootstrap_content.py first")
    save(cosmetics)
    log(f"{'created' if created else 'updated'} {cosmetics.get_path_name()} with {len(skins)} skins")

    if not EAL.does_asset_exist(PAWN_DATA_PATH):
        fail(f"{PAWN_DATA_PATH} missing: run create_bootstrap_content.py first")
        return cosmetics
    pawn_data = EAL.load_asset(PAWN_DATA_PATH)
    pawn_data.set_editor_property("cosmetics", cosmetics)
    save(pawn_data)
    log(f"{PAWN_DATA_PATH}.cosmetics -> {cosmetics.get_path_name()}")
    return cosmetics


def verify_character_cosmetics():
    cosmetics = EAL.load_asset(f"{COSMETICS_FOLDER}/{COSMETICS_NAME}")
    if cosmetics is None:
        fail("verify: cosmetics asset missing")
        return
    names = [str(s.get_editor_property("name")) for s in cosmetics.get_editor_property("skins")]
    if names != SKIN_NAMES:
        fail(f"verify: skin names {names} != {SKIN_NAMES}")
    pawn_data = EAL.load_asset(PAWN_DATA_PATH)
    linked = pawn_data.get_editor_property("cosmetics") if pawn_data is not None else None
    if linked is None or linked.get_path_name() != cosmetics.get_path_name():
        fail(f"verify: {PAWN_DATA_PATH}.cosmetics is {linked}")
# ---- end task 08


# ---- task 13: weapon meshes, view offsets, rocket light (the meshes come from import_art.py --static)
WEAPON_MESH_DIR = "/Game/Weapons/Meshes"
WEAPON_COSMETICS_DIR = "/Game/Weapons"
# Camera-space placement of the view model (x forward, y right, z up, cm), grip at the mesh origin. FirstPersonScale 0.6 shrinks
# the placement toward the camera, so the muzzle is seen at 0.6 * (x + muzzle_x). Rule (ADR 0006 section 4.6, task 13): visible
# lower-right (y 15, z -15), muzzle at ~45 cm forward => x = 75 - muzzle_x, but never so far back that the rear of the mesh sits
# closer than 2 cm to the eye plane => x >= 2 - rear_x. muzzle_x / rear_x from weapons-spec.md (MG 47/-20, SG 58/-26, PG 49.5/-10,
# RL 58/-12, LG 46/-16, RG 74/-12). Human tuning replaces these numbers (STATUS question: Mac playtest).
VIEW_OFFSETS = {
    "MachineGun": (28.0, 15.0, -15.0),
    "Shotgun": (28.0, 15.0, -15.0),
    "PlasmaGun": (26.0, 15.0, -15.0),
    "RocketLauncher": (17.0, 15.0, -15.0),
    "LightningGun": (29.0, 15.0, -15.0),
    "Railgun": (14.0, 15.0, -15.0),
}
# The rocket's light in flight (ADR 0006 E1.3): colour, candela, radius. Other weapons keep ProjectileLightIntensity 0.
ROCKET_LIGHT = {"RocketLauncher": (unreal.LinearColor(1.0, 0.55, 0.2, 1.0), 3000.0, 450.0)}


def build_weapon_meshes():
    for name, (x, y, z) in VIEW_OFFSETS.items():
        path = f"{WEAPON_COSMETICS_DIR}/DA_WeaponCosmetics_{name}"
        if not EAL.does_asset_exist(path):
            fail(f"{path} missing: run create_bootstrap_content.py first")
            continue
        da = EAL.load_asset(path)
        mesh_path = f"{WEAPON_MESH_DIR}/SM_Weapon_{name}"
        if EAL.does_asset_exist(mesh_path):
            mesh = EAL.load_asset(mesh_path)
            da.set_editor_property("world_mesh", mesh)
            da.set_editor_property("view_mesh", mesh)
            log(f"{path}: WorldMesh = ViewMesh = {mesh_path}")
        else:
            log(f"{mesh_path} not imported yet: {path} keeps its meshes")
        da.set_editor_property("view_offset", unreal.Transform(location=[x, y, z]))
        da.set_editor_property("view_scale", 1.0)
        if name in ROCKET_LIGHT:
            color, intensity, radius = ROCKET_LIGHT[name]
            da.set_editor_property("projectile_light_color", color)
            da.set_editor_property("projectile_light_intensity", intensity)
            da.set_editor_property("projectile_light_radius", radius)
        save(da)


def verify_weapon_meshes():
    for name, (x, y, z) in VIEW_OFFSETS.items():
        da = EAL.load_asset(f"{WEAPON_COSMETICS_DIR}/DA_WeaponCosmetics_{name}")
        if da is None:
            continue
        mesh_path = f"{WEAPON_MESH_DIR}/SM_Weapon_{name}"
        if EAL.does_asset_exist(mesh_path):
            wm, vm = da.get_editor_property("world_mesh"), da.get_editor_property("view_mesh")
            if wm is None or vm is None or wm.get_path_name() != vm.get_path_name() or wm.get_name() != f"SM_Weapon_{name}":
                fail(f"verify: {name} WorldMesh/ViewMesh {wm} / {vm}")
            elif wm.find_socket(str(da.get_editor_property("muzzle_socket"))) is None:
                fail(f"verify: {wm.get_name()} has no {da.get_editor_property('muzzle_socket')} socket")
        loc = da.get_editor_property("view_offset").translation
        if abs(loc.x - x) > 0.01 or abs(loc.y - y) > 0.01 or abs(loc.z - z) > 0.01:
            fail(f"verify: {name} ViewOffset {loc}")
        if abs(da.get_editor_property("view_scale") - 1.0) > 1e-6:
            fail(f"verify: {name} ViewScale")
    rocket = EAL.load_asset(f"{WEAPON_COSMETICS_DIR}/DA_WeaponCosmetics_RocketLauncher")
    if rocket is not None and rocket.get_editor_property("projectile_light_intensity") <= 0.0:
        fail("verify: RocketLauncher has no ProjectileLightIntensity")
# ---- end task 13


def main():
    build_character_cosmetics()
    verify_character_cosmetics()
    build_weapon_meshes()
    verify_weapon_meshes()
    if ERRORS:
        raise RuntimeError(f"create_cosmetics_content.py: {len(ERRORS)} error(s): {ERRORS}")
    log("done")


main()
