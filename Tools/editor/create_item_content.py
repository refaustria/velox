"""Velox M3 content (ADR 0004): game mode definitions (task 05). Task 01 adds the item definitions (DA_Item_*) to this
script; each section is a build_* function called from main().

Run headless:
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=<abs path>/Tools/editor/create_item_content.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: existing assets are loaded and their properties re-applied. Fails (exception) if anything could not be
created or saved.
"""
import unreal

ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

ERRORS = []


def log(msg):
    unreal.log("[VeloxItemContent] " + msg)


def fail(msg):
    unreal.log_error("[VeloxItemContent] " + msg)
    ERRORS.append(msg)


def get_or_create(folder, name, asset_class, factory):
    """Return (asset, created)."""
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


def tag(name):
    t = unreal.GameplayTag()
    t.import_text(f'(TagName="{name}")')
    return t


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
        da, created = get_or_create("/Game/Data", mode["name"], unreal.VeloxGameModeDefinition, None)
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
    build_game_modes()
    if ERRORS:
        raise RuntimeError("item content failed: " + "; ".join(ERRORS))
    log("done")


main()
