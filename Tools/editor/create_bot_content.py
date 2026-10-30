"""Velox bot content (ADR 0005 section 11): one UVeloxBotDifficulty per preset in /Game/Bots/DA_Bot_<Preset>.

Run headless after building the editor target:
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=<abs path>/Tools/editor/create_bot_content.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: existing assets are loaded and every property is re-applied (re-saved only when a value changed). Fails
(exception) if anything could not be created, set or saved, or if a re-read asset does not carry the table values.
Values are the ADR 0005 section 11 table (Q3 skill 1/3/4/5 shape); the weapon preferences are the section 9 defaults with
RG/LG weights scaled for Easy (0.5) and Medium (0.8).
"""
import unreal

ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
BOT_DIR = "/Game/Bots"

ERRORS = []


def log(msg):
    unreal.log("[VeloxBots] " + msg)


def fail(msg):
    unreal.log_error("[VeloxBots] " + msg)
    ERRORS.append(msg)


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def tag(name):
    t = unreal.GameplayTag()
    t.import_text('(TagName="%s")' % name)
    return t


# ADR 0005 section 9 defaults (cm): weapon, weight, MinRange, BestMin, BestMax, MaxRange.
WEAPON_PREFERENCES = [
    ("Gauntlet", 10.0, 0.0, 0.0, 100.0, 120.0),
    ("MachineGun", 20.0, 0.0, 0.0, 20480.0, 20480.0),
    ("Shotgun", 60.0, 0.0, 0.0, 500.0, 1000.0),
    ("PlasmaGun", 50.0, 150.0, 300.0, 1500.0, 2500.0),
    ("RocketLauncher", 90.0, 150.0, 300.0, 1500.0, 3000.0),
    ("LightningGun", 85.0, 0.0, 200.0, 1500.0, 1900.0),
    ("Railgun", 100.0, 400.0, 1200.0, 20480.0, 20480.0),
]
AIM_DEPENDENT = ("LightningGun", "Railgun")

# Section 11 table, one dict per preset. Keys are the python names of the UVeloxBotDifficulty properties.
PRESETS = {
    "Easy": dict(
        reaction_seconds=0.50, aim_error_deg=8.0, error_correlation_seconds=0.4, tracking_smoothing_seconds=0.25,
        max_turn_rate_deg_per_sec=240.0, lead_projectiles=False, lead_scale=0.0, fire_cone_deg=12.0,
        perception_fov_deg=110.0, perception_range_cm=4000.0, perception_interval_seconds=0.15, enemy_memory_seconds=2.0,
        bunny_hop=False, strafe_jump=False, rocket_jumps=False, estimated_travel_speed=700.0,
        item_timing_skill=0.0, max_wait_seconds=0.0, goal_reevaluate_seconds=1.5, goal_switch_hysteresis=1.5,
        fight_item_bias=2.0, strafe_amplitude=0.4, strafe_flip_seconds_min=0.8, strafe_flip_seconds_max=1.6,
        dodge_jump_chance_per_sec=0.1, dodge_projectiles=False, projectile_dodge_reaction_seconds=0.25,
        preferred_range=600.0, aim_weapon_scale=0.5,
    ),
    "Medium": dict(
        reaction_seconds=0.30, aim_error_deg=4.0, error_correlation_seconds=0.3, tracking_smoothing_seconds=0.15,
        max_turn_rate_deg_per_sec=420.0, lead_projectiles=True, lead_scale=0.7, fire_cone_deg=7.0,
        perception_fov_deg=130.0, perception_range_cm=8000.0, perception_interval_seconds=0.1, enemy_memory_seconds=3.0,
        bunny_hop=True, strafe_jump=False, rocket_jumps=False, estimated_travel_speed=800.0,
        item_timing_skill=0.5, max_wait_seconds=4.0, goal_reevaluate_seconds=1.0, goal_switch_hysteresis=1.3,
        fight_item_bias=1.5, strafe_amplitude=0.7, strafe_flip_seconds_min=0.5, strafe_flip_seconds_max=1.2,
        dodge_jump_chance_per_sec=0.3, dodge_projectiles=False, projectile_dodge_reaction_seconds=0.25,
        preferred_range=800.0, aim_weapon_scale=0.8,
    ),
    "Hard": dict(
        reaction_seconds=0.18, aim_error_deg=1.8, error_correlation_seconds=0.25, tracking_smoothing_seconds=0.08,
        max_turn_rate_deg_per_sec=720.0, lead_projectiles=True, lead_scale=0.9, fire_cone_deg=4.0,
        perception_fov_deg=160.0, perception_range_cm=20480.0, perception_interval_seconds=0.1, enemy_memory_seconds=4.0,
        bunny_hop=True, strafe_jump=True, rocket_jumps=True, estimated_travel_speed=1000.0,
        item_timing_skill=1.0, max_wait_seconds=8.0, goal_reevaluate_seconds=1.0, goal_switch_hysteresis=1.3,
        fight_item_bias=1.2, strafe_amplitude=0.9, strafe_flip_seconds_min=0.4, strafe_flip_seconds_max=1.0,
        dodge_jump_chance_per_sec=0.6, dodge_projectiles=True, projectile_dodge_reaction_seconds=0.25,
        preferred_range=1000.0, aim_weapon_scale=1.0,
    ),
    "Expert": dict(
        reaction_seconds=0.10, aim_error_deg=0.7, error_correlation_seconds=0.2, tracking_smoothing_seconds=0.04,
        max_turn_rate_deg_per_sec=1080.0, lead_projectiles=True, lead_scale=1.0, fire_cone_deg=3.0,
        perception_fov_deg=180.0, perception_range_cm=20480.0, perception_interval_seconds=0.1, enemy_memory_seconds=5.0,
        bunny_hop=True, strafe_jump=True, rocket_jumps=True, estimated_travel_speed=1100.0,
        item_timing_skill=1.0, max_wait_seconds=10.0, goal_reevaluate_seconds=0.75, goal_switch_hysteresis=1.2,
        fight_item_bias=1.0, strafe_amplitude=1.0, strafe_flip_seconds_min=0.3, strafe_flip_seconds_max=0.8,
        dodge_jump_chance_per_sec=0.8, dodge_projectiles=True, projectile_dodge_reaction_seconds=0.12,
        preferred_range=1200.0, aim_weapon_scale=1.0,
    ),
}
# python property name -> C++ field name differences (UE strips the b prefix of bools).
BOOL_KEYS = ("lead_projectiles", "bunny_hop", "strafe_jump", "rocket_jumps", "dodge_projectiles")


def differs(key, got, want):
    """Bools compare exactly, floats with a tolerance (properties are 32-bit floats)."""
    return got != want if key in BOOL_KEYS else abs(got - want) > 1e-4


def asset_name(preset):
    return f"DA_Bot_{preset}"


def make_preferences(scale):
    prefs = []
    for weapon, weight, min_r, best_min, best_max, max_r in WEAPON_PREFERENCES:
        w = weight * scale if weapon in AIM_DEPENDENT else weight
        prefs.append(unreal.VeloxBotWeaponPreference(
            weapon=tag("Velox.Weapon." + weapon), weight=w, min_range=min_r, best_min=best_min, best_max=best_max, max_range=max_r))
    return prefs


def get_or_create(name):
    path = f"{BOT_DIR}/{name}"
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path), False
    asset = ATOOLS.create_asset(name, BOT_DIR, unreal.VeloxBotDifficulty, None)
    if asset is None:
        fail(f"could not create {path}")
    return asset, True


def prefs_equal(a, b):
    if len(a) != len(b):
        return False
    for x, y in zip(a, b):
        if x.weapon.export_text() != y.weapon.export_text():
            return False
        for f in ("weight", "min_range", "best_min", "best_max", "max_range"):
            if abs(x.get_editor_property(f) - y.get_editor_property(f)) > 1e-4:
                return False
    return True


def build_presets():
    ensure_dir(BOT_DIR)
    for preset, table in PRESETS.items():
        name = asset_name(preset)
        da, created = get_or_create(name)
        if da is None:
            continue
        values = {k: v for k, v in table.items() if k != "aim_weapon_scale"}
        changed = created
        for k, v in values.items():
            try:
                if differs(k, da.get_editor_property(k), v):
                    da.set_editor_property(k, v)
                    changed = True
            except Exception as e:  # noqa: BLE001 - report every bad field, then fail
                fail(f"{name}.{k}: {e}")
        prefs = make_preferences(table["aim_weapon_scale"])
        try:
            if not prefs_equal(list(da.get_editor_property("weapon_preferences")), prefs):
                da.set_editor_property("weapon_preferences", prefs)
                changed = True
        except Exception as e:  # noqa: BLE001
            fail(f"{name}.weapon_preferences: {e}")
        if changed:
            if not EAL.save_loaded_asset(da, False):
                fail(f"could not save {name}")
            log(f"{name} {'created' if created else 'updated'}")
        else:
            log(f"{name} unchanged")


def verify_presets():
    for preset, table in PRESETS.items():
        path = f"{BOT_DIR}/{asset_name(preset)}"
        da = EAL.load_asset(path)
        if da is None:
            fail(f"{path} not loadable")
            continue
        for k, want in table.items():
            if k == "aim_weapon_scale":
                continue
            got = da.get_editor_property(k)
            if differs(k, got, want):
                fail(f"{path}.{k} = {got}, expected {want}")
        if not prefs_equal(list(da.get_editor_property("weapon_preferences")), make_preferences(table["aim_weapon_scale"])):
            fail(f"{path}.weapon_preferences differ from the section 9 defaults")
    log(f"verified {len(PRESETS)} presets")


def main():
    build_presets()
    verify_presets()
    if ERRORS:
        raise RuntimeError("bot content failed: " + "; ".join(ERRORS))
    log("done")


main()
