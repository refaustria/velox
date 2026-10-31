"""Velox cosmetics content (ADR 0006 sections 4.3-4.5): DA_Cosmetics_Velox, wired into DA_PawnData_Default.

Run headless after create_bootstrap_content.py (it needs DA_PawnData_Default):
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=$PWD/Tools/editor/create_cosmetics_content.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: an existing asset is loaded and its properties re-applied. The soft references (SK_Velox, ABP_Velox,
MI_Skin_*, A_Velox_*) stay empty until the art tasks (11, 12) import them; the pawn then keeps its PlaceholderBody.
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
        # Struct members are set through the constructor (Tools/editor/README.md); Material stays empty until task 11.
        skins.append(unreal.VeloxSkin(name=name, body_color=to_linear(palette[index])))
    cosmetics.set_editor_property("skins", skins)
    cosmetics.set_editor_property("body_color_parameter", "BodyColor")
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


def main():
    build_character_cosmetics()
    verify_character_cosmetics()
    if ERRORS:
        raise RuntimeError(f"create_cosmetics_content.py: {len(ERRORS)} error(s): {ERRORS}")
    log("done")


main()
