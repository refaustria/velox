"""Velox audio content (ADR 0006 section 9, task M5-10).

Imports the WAVs of ArtSource/Audio/placeholders (Tools/audio/synth_placeholders.py) into /Game/Audio/SFX/<Group>/SW_<name>,
creates the attenuation assets SA_*, the concurrency groups SCon_*, the sound classes SC_Announcer / SC_UI, the announcer duck
mix SM_Announcer, fills PickupSound / RespawnSound of the DA_Item_* definitions and the fire / impact / beam sounds and muzzle light of the DA_WeaponCosmetics_* assets (the single source of a weapon's sound, review 0006 D4). UVeloxAudioSettings (Config/DefaultGame.ini
[/Script/Velox.VeloxAudioSettings]) names the assets by path; this script verifies the ini block matches its table. Run headless:
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=<abs path>/Tools/editor/create_audio_content.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
Run Tools/editor/create_bootstrap_content.py and create_item_content.py first (sound classes, item definitions).

Outside the editor, `python3 -I Tools/editor/create_audio_content.py --print-ini` prints the ini block from the table.
Idempotent: existing sound waves are not re-imported (set VELOX_AUDIO_REIMPORT=1 after replacing a WAV); every other asset is
loaded and its properties re-applied. Raises if anything failed. Placeholder WAVs: replace by a CC0 pack under the same names.
"""
import os
import sys

try:
    import unreal
except ImportError:  # --print-ini outside the editor
    unreal = None

AUDIO_DIR = "/Game/Audio"
SFX_DIR = AUDIO_DIR + "/SFX"
ATTEN_DIR = AUDIO_DIR + "/Attenuation"
CONC_DIR = AUDIO_DIR + "/Concurrency"
ERRORS = []


def log(msg):
    if unreal:
        unreal.log("[VeloxAudio] " + msg)
    else:
        print(msg)


def fail(msg):
    if unreal:
        unreal.log_error("[VeloxAudio] " + msg)
    else:
        print("ERROR " + msg, file=sys.stderr)
    ERRORS.append(msg)


# ---------------------------------------------------------------------------------------------- the table

# Attenuation: name -> (inner radius cm, falloff distance cm, air absorption LPF start cm or None). ADR 0006 section 9.
ATTENUATION = {
    "SA_WeaponFire": (300.0, 6000.0, None),
    "SA_Explosion": (500.0, 8000.0, 3000.0),
    "SA_Impact": (200.0, 3000.0, None),
    "SA_Footstep": (100.0, 2000.0, None),
    "SA_Player": (150.0, 3000.0, None),
    "SA_Pickup": (200.0, 2500.0, None),
    "SA_World": (200.0, 3000.0, None),
}

# Concurrency: name -> max voices (resolution rule: stop oldest).
CONCURRENCY = {"SCon_Weapons": 12, "SCon_Footsteps": 8, "SCon_Impacts": 8}

# Sound class per file prefix; everything else is SC_Effects.
CLASS_OF = {"ann_": "SC_Announcer", "ui_": "SC_UI", "hit_confirm": "SC_UI"}
LOOPING = {"wpn_lightninggun_loop"}


def group_of(stem):
    for prefix, group in (("wpn_", "Weapons"), ("imp_", "Impacts"), ("hit_", "Weapons"), ("ply_", "Player"), ("pickup_", "Pickups"),
                          ("world_", "World"), ("ui_", "UI"), ("ann_", "Announcer")):
        if stem.startswith(prefix):
            return group
    raise ValueError(stem)


def sound_path(stem):
    return "%s/%s/SW_%s" % (SFX_DIR, group_of(stem), stem)


def sound_ref(stem):
    p = sound_path(stem)
    return "%s.%s" % (p, p.rsplit("/", 1)[1])


# Weapon sounds live in the DA_WeaponCosmetics_<Name> assets (review 0006 D4), not in the ini:
# name -> (fire wav, impact wav, impact attenuation, (beam start, loop, stop) wavs or None, muzzle light intensity; colour below).
MUZZLE_LIGHT_COLOR = (1.0, 0.7, 0.3)
WEAPON_COSMETICS = {
    "MachineGun": ("wpn_machinegun_fire", "imp_bullet", "IMPACT", None, 5000.0),
    "Shotgun": ("wpn_shotgun_fire", "imp_bullet", "IMPACT", None, 5000.0),
    "PlasmaGun": ("wpn_plasmagun_fire", "imp_plasma", "EXPLOSION", None, 5000.0),
    "RocketLauncher": ("wpn_rocketlauncher_fire", "imp_rocket_explosion", "EXPLOSION", None, 5000.0),
    "Railgun": ("wpn_railgun_fire", "imp_rail_hit", "IMPACT", None, 5000.0),
    "LightningGun": (None, "imp_lightning_hit", "IMPACT", ("wpn_lightninggun_start", "wpn_lightninggun_loop", "wpn_lightninggun_stop"), 0.0),
}

ANNOUNCER_LINES = ["three", "two", "one", "fight", "quad_damage", "excellent", "impressive", "lead_taken", "lead_lost", "lead_tied", "one_minute",
                   "overtime", "match_over"]
FOOTSTEPS = ["ply_step_1", "ply_step_2", "ply_step_3", "ply_step_4"]

# UVeloxAudioSettings property -> wav (ini key = the C++ property name).
GENERIC = {
    "HitConfirmSound": "hit_confirm",
    "DryFireSound": "wpn_dryfire",
    "WeaponSwitchSound": "wpn_switch",
    "JumpSound": "ply_jump",
    "LandSoftSound": "ply_land_soft",
    "LandHardSound": "ply_land_hard",
    "PainLightSound": "ply_pain_25",
    "PainMediumSound": "ply_pain_50",
    "PainHeavySound": "ply_pain_100",
    "DeathSound": "ply_death",
    "JumpPadSound": "world_jumppad",
    "ItemRespawnSound": "world_item_respawn",
    "UIClickSound": "ui_click",
}

# DA_Item_<Type>_<Name> -> pickup wav.
ITEM_PICKUPS = {
    "Health_Small": "pickup_health_small",
    "Health_Medium": "pickup_health_medium",
    "Health_Large": "pickup_health_large",
    "Health_Mega": "pickup_health_mega",
    "Armor_Shard": "pickup_armor_shard",
    "Armor_Yellow": "pickup_armor",
    "Armor_Red": "pickup_armor",
}
for _n in ("Bullets", "Shells", "Rockets", "Cells", "Lightning", "Slugs"):
    ITEM_PICKUPS["Ammo_" + _n] = "pickup_ammo"
for _n in ("MachineGun", "Shotgun", "RocketLauncher", "LightningGun", "Railgun", "PlasmaGun"):
    ITEM_PICKUPS["Weapon_" + _n] = "pickup_weapon"
for _n in ("Quad", "Haste", "Regen", "BattleSuit"):
    ITEM_PICKUPS["Powerup_" + _n] = "pickup_powerup"

ATTEN_PROPS = {
    "AttenuationWeaponFire": "SA_WeaponFire", "AttenuationExplosion": "SA_Explosion", "AttenuationImpact": "SA_Impact",
    "AttenuationFootstep": "SA_Footstep", "AttenuationPlayer": "SA_Player", "AttenuationPickup": "SA_Pickup", "AttenuationWorld": "SA_World",
}
CONC_PROPS = {"ConcurrencyWeapons": "SCon_Weapons", "ConcurrencyFootsteps": "SCon_Footsteps", "ConcurrencyImpacts": "SCon_Impacts"}


def weapon_wavs():
    """Every wav the weapon cosmetics name."""
    out = []
    for fire, impact, _atten, beam, _light in WEAPON_COSMETICS.values():
        out += [w for w in (fire, impact) if w] + list(beam or ())
    return out


def asset_ref(folder, name):
    return "%s/%s.%s" % (folder, name, name)


def ini_lines():
    """The [/Script/Velox.VeloxAudioSettings] block for Config/DefaultGame.ini, from the table."""
    out = ["[/Script/Velox.VeloxAudioSettings]"]
    for prop, name in ATTEN_PROPS.items():
        out.append('%s="%s"' % (prop, asset_ref(ATTEN_DIR, name)))
    for prop, name in CONC_PROPS.items():
        out.append('%s="%s"' % (prop, asset_ref(CONC_DIR, name)))
    out.append('AnnouncerMix="%s"' % asset_ref(AUDIO_DIR, "SM_Announcer"))
    for prop, wav in GENERIC.items():
        out.append('%s="%s"' % (prop, sound_ref(wav)))
    for wav in FOOTSTEPS:
        out.append('+FootstepSounds="%s"' % sound_ref(wav))
    for line in ANNOUNCER_LINES:
        out.append('+AnnouncerLines=(Name="%s",Sound="%s")' % (line, sound_ref("ann_" + line)))
    return out


# ---------------------------------------------------------------------------------------------- editor helpers

def ensure_dir(path):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def get_or_create(folder, name, asset_class, factory):
    path = "%s/%s" % (folder, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.EditorAssetLibrary.load_asset(path)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, asset_class, factory)
    if asset is None:
        fail("could not create " + path)
    return asset


def save(asset):
    if asset is not None and not unreal.EditorAssetLibrary.save_loaded_asset(asset, False):
        fail("could not save " + asset.get_path_name())


def set_props(obj, values):
    for key, value in values.items():
        try:
            obj.set_editor_property(key, value)
        except Exception as exc:  # noqa: BLE001 - report which property, keep going
            fail("%s.%s: %s" % (obj.get_name(), key, exc))


def import_sounds(src_dir):
    reimport = os.environ.get("VELOX_AUDIO_REIMPORT") == "1"
    stems = sorted(f[:-4] for f in os.listdir(src_dir) if f.endswith(".wav"))
    expected = set(weapon_wavs()) | set(GENERIC.values()) | set(FOOTSTEPS) | {"ann_" + l for l in ANNOUNCER_LINES} | set(ITEM_PICKUPS.values())
    missing = sorted(expected - set(stems))
    if missing:
        fail("missing WAVs in %s: %s" % (src_dir, ", ".join(missing)))
    tasks = []
    for stem in stems:
        ensure_dir("%s/%s" % (SFX_DIR, group_of(stem)))
        if unreal.EditorAssetLibrary.does_asset_exist(sound_path(stem)) and not reimport:
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(src_dir, stem + ".wav"))
        task.set_editor_property("destination_path", "%s/%s" % (SFX_DIR, group_of(stem)))
        task.set_editor_property("destination_name", "SW_" + stem)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("automated", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    if tasks:
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    log("imported %d of %d WAVs" % (len(tasks), len(stems)))
    return stems


def build_classes():
    master = unreal.EditorAssetLibrary.load_asset(AUDIO_DIR + "/SC_Master")
    effects = unreal.EditorAssetLibrary.load_asset(AUDIO_DIR + "/SC_Effects")
    if master is None or effects is None:
        fail("SC_Master / SC_Effects missing: run create_bootstrap_content.py first")
        return {}
    classes = {"SC_Effects": effects}
    for name in ("SC_Announcer", "SC_UI"):
        sc = get_or_create(AUDIO_DIR, name, unreal.SoundClass, unreal.SoundClassFactory())
        if sc is not None:
            # Children of SC_Master (not of SC_Effects): the announcer duck mix lowers SC_Effects only. The Effects slider drives
            # them through UVeloxGameUserSettings::ApplyAudio.
            sc.set_editor_property("parent_class", master)
            save(sc)
        classes[name] = sc
    mix = get_or_create(AUDIO_DIR, "SM_Announcer", unreal.SoundMix, unreal.SoundMixFactory())
    if mix is not None:
        adjuster = unreal.SoundClassAdjuster(sound_class_object=effects, volume_adjuster=0.5012, pitch_adjuster=1.0, apply_to_children=True)
        mix.set_editor_property("sound_class_effects", [adjuster])
        mix.set_editor_property("fade_in_time", 0.1)
        mix.set_editor_property("fade_out_time", 0.3)
        mix.set_editor_property("initial_delay", 0.0)
        save(mix)
    return classes


def build_attenuation():
    ensure_dir(ATTEN_DIR)
    for name, (inner, falloff, lpf_start) in ATTENUATION.items():
        sa = get_or_create(ATTEN_DIR, name, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
        if sa is None:
            continue
        settings = sa.get_editor_property("attenuation")
        set_props(settings, {
            "attenuate": True,
            "distance_algorithm": unreal.AttenuationDistanceModel.NATURAL_SOUND,
            "attenuation_shape": unreal.AttenuationShape.SPHERE,
            "attenuation_shape_extents": unreal.Vector(inner, 0.0, 0.0),
            "falloff_distance": falloff,
            "spatialize": True,
            "enable_occlusion": False,
            "enable_reverb_send": False,
            "enable_priority_attenuation": False,
            "attenuate_with_lpf": lpf_start is not None,
        })
        if lpf_start is not None:
            set_props(settings, {"lpf_radius_min": lpf_start, "lpf_radius_max": lpf_start + falloff * 0.5})
        sa.set_editor_property("attenuation", settings)
        save(sa)


def build_concurrency():
    ensure_dir(CONC_DIR)
    for name, voices in CONCURRENCY.items():
        sc = get_or_create(CONC_DIR, name, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
        if sc is None:
            continue
        conc = sc.get_editor_property("concurrency")
        set_props(conc, {
            "max_count": voices,
            "limit_to_owner": False,
            "resolution_rule": unreal.MaxConcurrentResolutionRule.STOP_OLDEST,
        })
        sc.set_editor_property("concurrency", conc)
        save(sc)


def configure_waves(stems, classes):
    for stem in stems:
        wave = unreal.EditorAssetLibrary.load_asset(sound_path(stem))
        if wave is None:
            fail("not imported: " + sound_path(stem))
            continue
        class_name = "SC_Effects"
        for prefix, name in CLASS_OF.items():
            if stem.startswith(prefix):
                class_name = name
        values = {"looping": stem in LOOPING}
        if classes.get(class_name) is not None:
            values["sound_class_object"] = classes[class_name]
        set_props(wave, values)
        save(wave)


def fill_items():
    for suffix, wav in ITEM_PICKUPS.items():
        path = "/Game/Items/DA_Item_" + suffix
        item = unreal.EditorAssetLibrary.load_asset(path)
        if item is None:
            fail("missing item definition %s (run create_item_content.py)" % path)
            continue
        wave = unreal.EditorAssetLibrary.load_asset(sound_path(wav))
        set_props(item, {"pickup_sound": wave})
        save(item)


def cosmetics_values(name):
    """DA_WeaponCosmetics_<name> property -> asset (None = empty), from the table."""
    fire, impact, atten, beam, light = WEAPON_COSMETICS[name]

    def wave(stem):
        return unreal.EditorAssetLibrary.load_asset(sound_path(stem)) if stem else None

    return {
        "fire_sound": wave(fire),
        "impact_sound": wave(impact),
        "impact_attenuation": getattr(unreal.VeloxAudioAttenuation, atten),
        "beam_start": wave(beam[0]) if beam else None,
        "beam_loop": wave(beam[1]) if beam else None,
        "beam_stop": wave(beam[2]) if beam else None,
        "muzzle_light_color": unreal.LinearColor(*MUZZLE_LIGHT_COLOR, 1.0),
        "muzzle_light_intensity": light,
    }


def fill_weapon_cosmetics():
    for name in WEAPON_COSMETICS:
        path = "/Game/Weapons/DA_WeaponCosmetics_" + name
        da = unreal.EditorAssetLibrary.load_asset(path)
        if da is None:
            fail("missing %s (run create_bootstrap_content.py)" % path)
            continue
        set_props(da, cosmetics_values(name))
        save(da)


def verify_weapon_cosmetics():
    for name, (fire, impact, _atten, beam, _light) in WEAPON_COSMETICS.items():
        da = unreal.EditorAssetLibrary.load_asset("/Game/Weapons/DA_WeaponCosmetics_" + name)
        if da is None:
            fail("verify: DA_WeaponCosmetics_" + name)
            continue
        if da.get_editor_property("impact_sound") is None:
            fail("verify: %s names no ImpactSound" % name)
        if fire and da.get_editor_property("fire_sound") is None:
            fail("verify: %s names no FireSound" % name)
        if beam and any(da.get_editor_property(p) is None for p in ("beam_start", "beam_loop", "beam_stop")):
            fail("verify: %s names no beam sounds" % name)


def verify(stems):
    for stem in stems:
        if not unreal.EditorAssetLibrary.does_asset_exist(sound_path(stem)):
            fail("verify: " + sound_path(stem))
    loop = unreal.EditorAssetLibrary.load_asset(sound_path("wpn_lightninggun_loop"))
    if loop is None or not loop.get_editor_property("looping"):
        fail("verify: the LG beam loop is not looping")
    line = unreal.EditorAssetLibrary.load_asset(sound_path("ann_fight"))
    if line is None or line.get_editor_property("sound_class_object") is None or line.get_editor_property("sound_class_object").get_name() != "SC_Announcer":
        fail("verify: ann_fight is not in SC_Announcer")
    for name in ATTENUATION:
        sa = unreal.EditorAssetLibrary.load_asset("%s/%s" % (ATTEN_DIR, name))
        want = ATTENUATION[name]
        got = sa.get_editor_property("attenuation") if sa else None
        if got is None:
            fail("verify: " + name)
            continue
        if abs(got.get_editor_property("falloff_distance") - want[1]) > 0.01 or abs(got.get_editor_property("attenuation_shape_extents").x - want[0]) > 0.01:
            fail("verify: %s inner/falloff differ from the table" % name)
        if got.get_editor_property("enable_occlusion"):
            fail("verify: %s has occlusion on" % name)
    for name, voices in CONCURRENCY.items():
        sc = unreal.EditorAssetLibrary.load_asset("%s/%s" % (CONC_DIR, name))
        if sc is None or sc.get_editor_property("concurrency").get_editor_property("max_count") != voices:
            fail("verify: " + name)
    # The ini block must name the assets this script creates.
    ini_path = os.path.join(unreal.Paths.project_config_dir(), "DefaultGame.ini")
    with open(ini_path, encoding="utf-8") as f:
        ini = f.read()
    for line in ini_lines()[1:]:
        if line not in ini:
            fail("DefaultGame.ini lacks: " + line)
    for suffix, wav in ITEM_PICKUPS.items():
        item = unreal.EditorAssetLibrary.load_asset("/Game/Items/DA_Item_" + suffix)
        if item is None or item.get_editor_property("pickup_sound") is None:
            fail("verify: pickup sound of " + suffix)
    verify_weapon_cosmetics()


def main():
    src = os.path.join(unreal.Paths.project_dir(), "ArtSource", "Audio", "placeholders")
    if not os.path.isdir(src):
        fail("no placeholders at %s: run python3 -I Tools/audio/synth_placeholders.py first" % src)
        raise RuntimeError("; ".join(ERRORS))
    ensure_dir(SFX_DIR)
    stems = import_sounds(src)
    classes = build_classes()
    build_attenuation()
    build_concurrency()
    configure_waves(stems, classes)
    fill_items()
    fill_weapon_cosmetics()
    verify(stems)
    log("done: %d sounds, %d errors" % (len(stems), len(ERRORS)))
    if ERRORS:
        raise RuntimeError("create_audio_content failed: " + "; ".join(ERRORS))


if unreal is None:
    if "--print-ini" in sys.argv:
        print("\n".join(ini_lines()))
    else:
        print(__doc__)
else:
    main()
