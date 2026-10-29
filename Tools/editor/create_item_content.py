"""Velox item content (ADR 0004 section 1): one UVeloxItemDefinition per Q3 item in /Game/Items/DA_Item_<Type>_<Name>.

Run headless after create_bootstrap_content.py (the weapon items reference /Game/Weapons/DA_Weapon_*):
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=<abs path>/Tools/editor/create_item_content.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: existing assets are loaded and every property is re-applied. Fails (exception) if anything could not be
created, set or saved, or if a re-read asset does not carry the table values. UVeloxCombatSettings is not touched.
Q3 1.32 numbers (bg_misc.c bg_itemlist, g_items.c). Also builds the game mode definitions (DA_GameMode_*, M3-05, ADR 0004 section 5) in /Game/Data.
"""
import unreal

ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
ITEM_DIR = "/Game/Items"
WEAPON_DIR = "/Game/Weapons"

ERRORS = []


def log(msg):
    unreal.log("[VeloxItems] " + msg)


def fail(msg):
    unreal.log_error("[VeloxItems] " + msg)
    ERRORS.append(msg)


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


def get_or_create(folder, name, asset_class):
    path = f"{folder}/{name}"
    if EAL.does_asset_exist(path):
        return EAL.load_asset(path)
    asset = ATOOLS.create_asset(name, folder, asset_class, None)
    if asset is None:
        fail(f"could not create {path}")
    return asset


def save(asset):
    if asset is not None and not EAL.save_loaded_asset(asset, False):
        fail(f"could not save {asset.get_path_name()}")


def tag(name):
    t = unreal.GameplayTag()
    t.import_text('(TagName="%s")' % name)
    return t


def color(r, g, b):
    return unreal.Color(r=r, g=g, b=b, a=255)


# Respawn seconds (Q3 g_items.c RESPAWN_*): health 35, armor 25, ammo 40, powerups 120; weapon pickups use the game mode's
# WeaponRespawnSeconds (5), the value here is informational. Powerups first appear 30-60 s after the level start
# (Q3 45 + crandom() * 15). BotBaseValue: Q3 item weights by tier (ADR 0004 section 9).
HEALTH, ARMOR, AMMO, WEAPON, POWERUP = "HEALTH", "ARMOR", "AMMO", "WEAPON", "POWERUP"
CAP_MAX, CAP_HARD = "MAX_HEALTH", "HARD_CAP"

ITEMS = [
    # type, name, display, quantity, cap, respawn, bot value, major, colour, scale, extra
    dict(type=HEALTH, name="Small", display="5 Health", qty=5, cap=CAP_HARD, respawn=35, bot=5, color=color(40, 220, 40), scale=(0.25, 0.25, 0.25)),
    dict(type=HEALTH, name="Medium", display="25 Health", qty=25, cap=CAP_MAX, respawn=35, bot=25, color=color(255, 220, 40), scale=(0.35, 0.35, 0.35)),
    dict(type=HEALTH, name="Large", display="50 Health", qty=50, cap=CAP_MAX, respawn=35, bot=40, color=color(255, 150, 20), scale=(0.45, 0.45, 0.45)),
    dict(type=HEALTH, name="Mega", display="Mega Health", qty=100, cap=CAP_HARD, respawn=35, bot=100, major=True, color=color(60, 120, 255), scale=(0.55, 0.55, 0.55)),
    dict(type=ARMOR, name="Shard", display="Armor Shard", qty=5, respawn=25, bot=5, color=color(40, 220, 40), scale=(0.2, 0.2, 0.3)),
    dict(type=ARMOR, name="Yellow", display="Yellow Armor", qty=50, respawn=25, bot=70, major=True, color=color(255, 220, 40), scale=(0.5, 0.5, 0.5)),
    dict(type=ARMOR, name="Red", display="Red Armor", qty=100, respawn=25, bot=90, major=True, color=color(240, 30, 30), scale=(0.55, 0.55, 0.55)),
    dict(type=AMMO, name="Bullets", display="Bullets", qty=50, respawn=40, bot=10, ammo="Bullets", color=color(240, 240, 120), scale=(0.3, 0.3, 0.3)),
    dict(type=AMMO, name="Shells", display="Shells", qty=10, respawn=40, bot=10, ammo="Shells", color=color(255, 160, 60), scale=(0.3, 0.3, 0.3)),
    dict(type=AMMO, name="Rockets", display="Rockets", qty=5, respawn=40, bot=10, ammo="Rockets", color=color(240, 60, 60), scale=(0.3, 0.3, 0.3)),
    dict(type=AMMO, name="Cells", display="Cells", qty=30, respawn=40, bot=10, ammo="Cells", color=color(160, 80, 255), scale=(0.3, 0.3, 0.3)),
    dict(type=AMMO, name="Lightning", display="Lightning", qty=60, respawn=40, bot=10, ammo="Lightning", color=color(220, 240, 255), scale=(0.3, 0.3, 0.3)),
    dict(type=AMMO, name="Slugs", display="Slugs", qty=10, respawn=40, bot=10, ammo="Slugs", color=color(60, 240, 160), scale=(0.3, 0.3, 0.3)),
    dict(type=WEAPON, name="MachineGun", display="Machinegun", respawn=5, bot=30, weapon="MachineGun", color=color(240, 240, 120), scale=(0.6, 0.2, 0.2)),
    dict(type=WEAPON, name="Shotgun", display="Shotgun", respawn=5, bot=40, weapon="Shotgun", color=color(255, 160, 60), scale=(0.6, 0.2, 0.2)),
    dict(type=WEAPON, name="RocketLauncher", display="Rocket Launcher", respawn=5, bot=60, weapon="RocketLauncher", color=color(240, 60, 60), scale=(0.6, 0.2, 0.2)),
    dict(type=WEAPON, name="LightningGun", display="Lightning Gun", respawn=5, bot=55, weapon="LightningGun", color=color(220, 240, 255), scale=(0.6, 0.2, 0.2)),
    dict(type=WEAPON, name="Railgun", display="Railgun", respawn=5, bot=60, weapon="Railgun", color=color(60, 240, 160), scale=(0.6, 0.2, 0.2)),
    dict(type=WEAPON, name="PlasmaGun", display="Plasma Gun", respawn=5, bot=50, weapon="PlasmaGun", color=color(160, 80, 255), scale=(0.6, 0.2, 0.2)),
    dict(type=POWERUP, name="Quad", display="Quad Damage", respawn=120, bot=110, major=True, powerup_s=30, color=color(60, 120, 255), scale=(0.5, 0.5, 0.5)),
    dict(type=POWERUP, name="Haste", display="Haste", respawn=120, bot=110, major=True, powerup_s=30, color=color(255, 220, 40), scale=(0.5, 0.5, 0.5)),
    dict(type=POWERUP, name="Regen", display="Regeneration", respawn=120, bot=110, major=True, powerup_s=30, color=color(240, 30, 30), scale=(0.5, 0.5, 0.5)),
    dict(type=POWERUP, name="BattleSuit", display="Battle Suit", respawn=120, bot=110, major=True, powerup_s=30, color=color(40, 220, 40), scale=(0.5, 0.5, 0.5)),
]
POWERUP_INITIAL_DELAY = (30.0, 60.0)
TYPE_NAMES = {HEALTH: "Health", ARMOR: "Armor", AMMO: "Ammo", WEAPON: "Weapon", POWERUP: "Powerup"}


def asset_name(item):
    return f"DA_Item_{TYPE_NAMES[item['type']]}_{item['name']}"


def powerup_effect_class(item):
    """UVeloxGE_Powerup_<Name> for powerup items (ADR 0004 section 3), None otherwise."""
    if item["type"] != POWERUP:
        return None
    path = f"/Script/Velox.VeloxGE_Powerup_{item['name']}"
    cls = unreal.load_class(None, path)
    if cls is None:
        fail(f"{asset_name(item)}: {path} not found (build the editor first)")
    return cls


def item_values(item):
    type_name = TYPE_NAMES[item["type"]]
    initial = POWERUP_INITIAL_DELAY if item["type"] == POWERUP else (0.0, 0.0)
    values = {
        "item_tag": tag(f"Velox.Item.{type_name}.{item['name']}"),
        "display_name": unreal.Text(item["display"]),
        "type": getattr(unreal.VeloxItemType, item["type"]),
        "quantity": item.get("qty", 0),
        "health_cap": getattr(unreal.VeloxHealthCap, item.get("cap", CAP_MAX)),
        "respawn_seconds": float(item["respawn"]),
        "initial_spawn_delay": unreal.FloatInterval(min=initial[0], max=initial[1]),
        "global_pickup_sound": item["type"] == POWERUP,
        "major_item": item.get("major", False),
        "bot_base_value": float(item["bot"]),
        "weapon": None,
        "ammo_tag": tag("Velox.Ammo." + item["ammo"]) if "ammo" in item else unreal.GameplayTag(),
        "powerup_effect": powerup_effect_class(item),
        "powerup_seconds": float(item.get("powerup_s", 0.0)),
        "pickup_cue_tag": unreal.GameplayTag(),
        "placeholder_color": item["color"],
        "placeholder_scale": unreal.Vector(*item["scale"]),
    }
    if "weapon" in item:
        weapon_path = f"{WEAPON_DIR}/DA_Weapon_{item['weapon']}"
        weapon = EAL.load_asset(weapon_path) if EAL.does_asset_exist(weapon_path) else None
        if weapon is None:
            fail(f"{asset_name(item)}: {weapon_path} missing (run create_bootstrap_content.py first)")
        values["weapon"] = weapon
    return values


def build_items():
    ensure_dir(ITEM_DIR)
    for item in ITEMS:
        name = asset_name(item)
        da = get_or_create(ITEM_DIR, name, unreal.VeloxItemDefinition)
        if da is None:
            continue
        for k, v in item_values(item).items():
            try:
                da.set_editor_property(k, v)
            except Exception as e:  # noqa: BLE001
                fail(f"{name}.{k}: {e}")
        save(da)


def verify_items():
    for item in ITEMS:
        path = f"{ITEM_DIR}/{asset_name(item)}"
        da = EAL.load_asset(path)
        if da is None:
            fail(f"{path} not loadable")
            continue
        checks = {
            "quantity": item.get("qty", 0),
            "respawn_seconds": float(item["respawn"]),
            "major_item": item.get("major", False),
            "powerup_seconds": float(item.get("powerup_s", 0.0)),
        }
        for k, want in checks.items():
            got = da.get_editor_property(k)
            if got != want:
                fail(f"{path}.{k} = {got}, expected {want}")
        if ("weapon" in item) != (da.get_editor_property("weapon") is not None):
            fail(f"{path}.weapon mismatch")
        effect = da.get_editor_property("powerup_effect")
        want_effect = f"VeloxGE_Powerup_{item['name']}" if item["type"] == POWERUP else None
        got_effect = effect.get_name() if effect is not None else None
        if got_effect != want_effect:
            fail(f"{path}.powerup_effect = {got_effect}, expected {want_effect}")
    log(f"verified {len(ITEMS)} item definitions")


# ---------------------------------------------------------------- Game modes (task 05, ADR 0004 section 5)
# ADR 0004 defaults; open question 3 (STATUS question 21) may change them. Q3: g_warmup 20, intermission 15 s.
GAME_MODES = [
    {
        "name": "DA_GameMode_FFA",
        "mode_tag": "Velox.Mode.FFA",
        "display_name": "Free For All",
        "frag_limit": 20,
        "time_limit_seconds": 600,
        "min_players_to_start": 2,
        "max_active_players": 16,
        "require_ready": False,
        "overtime_rule": unreal.VeloxOvertimeRule.NONE,
        "allow_join_during_match": True,
        "leaver_forfeits": False,
    },
    {
        "name": "DA_GameMode_Duel",
        "mode_tag": "Velox.Mode.Duel",
        "display_name": "Duel",
        "frag_limit": 0,
        "time_limit_seconds": 600,
        "min_players_to_start": 2,
        "max_active_players": 2,
        "require_ready": True,
        "overtime_rule": unreal.VeloxOvertimeRule.SUDDEN_DEATH,
        "allow_join_during_match": False,
        "leaver_forfeits": True,
    },
]

GAME_MODE_SHARED = {
    "warmup_countdown_seconds": 20.0,
    "overtime_seconds": 120.0,
    "post_match_seconds": 15.0,
    "weapon_stay": False,
    "weapon_respawn_seconds": 5.0,
    "item_respawn_scale": 1.0,
    "item_timers": unreal.VeloxItemTimerVisibility.SPECTATORS,
}


def build_game_modes():
    for mode in GAME_MODES:
        path = "/Game/Data/" + mode["name"]
        created = not EAL.does_asset_exist(path)
        da = get_or_create("/Game/Data", mode["name"], unreal.VeloxGameModeDefinition)
        if da is None:
            continue
        values = dict(GAME_MODE_SHARED)
        values.update({
            "mode_tag": tag(mode["mode_tag"]),
            "display_name": mode["display_name"],
            "frag_limit": mode["frag_limit"],
            "time_limit_seconds": mode["time_limit_seconds"],
            "min_players_to_start": mode["min_players_to_start"],
            "max_active_players": mode["max_active_players"],
            "require_ready": mode["require_ready"],
            "overtime_rule": mode["overtime_rule"],
            "allow_join_during_match": mode["allow_join_during_match"],
            "leaver_forfeits": mode["leaver_forfeits"],
        })
        for k, v in values.items():
            try:
                da.set_editor_property(k, v)
            except Exception as e:  # noqa: BLE001 - report every bad field, then fail
                fail(f"{mode['name']}.{k}: {e}")
        if mode["mode_tag"] not in da.get_editor_property("mode_tag").export_text():
            fail(f"{mode['name']}.mode_tag: native tag {mode['mode_tag']} not registered")
        save(da)
        log(f"{mode['name']} {'created' if created else 'updated'}")


def main():
    build_items()
    verify_items()
    build_game_modes()
    if ERRORS:
        raise RuntimeError("item content failed: " + "; ".join(ERRORS))
    log("done")


main()
