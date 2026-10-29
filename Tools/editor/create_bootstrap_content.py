"""Velox bootstrap content: L_Empty map, Enhanced Input assets, pawn/movement/input/combat data assets, weapons.

Run headless (see docs/content-bootstrap.md):
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_bootstrap_content.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: existing assets are loaded and their properties re-applied; the map is rebuilt only if missing.
Fails (non-zero exit via exception) if anything could not be created or saved.
"""
import math

import unreal

ATOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

ERRORS = []


def log(msg):
    unreal.log("[VeloxBootstrap] " + msg)


def fail(msg):
    unreal.log_error("[VeloxBootstrap] " + msg)
    ERRORS.append(msg)


def ensure_dir(path):
    if not EAL.does_directory_exist(path):
        EAL.make_directory(path)


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


# ---------------------------------------------------------------- Input actions / mapping context
def make_action(name, value_type):
    asset, _ = get_or_create("/Game/Input", name, unreal.InputAction, unreal.InputAction_Factory()
                             if hasattr(unreal, "InputAction_Factory") else None)
    if asset is None:
        return None
    asset.set_editor_property("value_type", value_type)
    save(asset)
    return asset


def key(name):
    k = unreal.Key()
    k.set_editor_property("key_name", name)
    return k


def build_input():
    ensure_dir("/Game/Input")
    VT = unreal.InputActionValueType
    actions = {
        "IA_Move": make_action("IA_Move", VT.AXIS2D),
        "IA_Look": make_action("IA_Look", VT.AXIS2D),
        "IA_Jump": make_action("IA_Jump", VT.BOOLEAN),
        "IA_Crouch": make_action("IA_Crouch", VT.BOOLEAN),
        "IA_Fire": make_action("IA_Fire", VT.BOOLEAN),
    }
    # ADR 0003 section 4: Q3 weapon select (keys 1-9 = slot, mouse wheel = weapnext/weapprev).
    for slot in range(1, 10):
        actions[f"IA_Weapon_Slot{slot}"] = make_action(f"IA_Weapon_Slot{slot}", VT.BOOLEAN)
    actions["IA_Weapon_Next"] = make_action("IA_Weapon_Next", VT.BOOLEAN)
    actions["IA_Weapon_Prev"] = make_action("IA_Weapon_Prev", VT.BOOLEAN)
    imc, _ = get_or_create("/Game/Input", "IMC_Default", unreal.InputMappingContext,
                           unreal.InputMappingContext_Factory() if hasattr(unreal, "InputMappingContext_Factory") else None)
    if imc is None or any(a is None for a in actions.values()):
        return actions, imc

    imc.unmap_all()
    # WASD -> IA_Move (X = right/left, Y = forward/back). Modifiers: Swizzle (YXZ) for W/S, Negate for A/S.
    # map_key returns the mapping BY VALUE, so modifiers are attached afterwards to the stored array and written back.
    modifier_specs = {}  # key name -> (swizzle, negate)

    def add(action, key_name, negate=False, swizzle=False):
        imc.map_key(action, key(key_name))
        if negate or swizzle:
            modifier_specs[key_name] = (swizzle, negate)

    add(actions["IA_Move"], "D")
    add(actions["IA_Move"], "A", negate=True)
    add(actions["IA_Move"], "W", swizzle=True)
    add(actions["IA_Move"], "S", swizzle=True, negate=True)
    # Raw mouse delta, no modifiers (ADR 0001 section 1). Mouse Y is up-positive in UE, so look pitch needs no negate here;
    # AVeloxCharacter applies AddControllerPitchInput(-Y) if its convention differs.
    imc.map_key(actions["IA_Look"], key("Mouse2D"))
    imc.map_key(actions["IA_Jump"], key("SpaceBar"))
    imc.map_key(actions["IA_Crouch"], key("LeftControl"))
    imc.map_key(actions["IA_Fire"], key("LeftMouseButton"))
    slot_keys = ["One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"]
    for slot, key_name in enumerate(slot_keys, start=1):
        imc.map_key(actions[f"IA_Weapon_Slot{slot}"], key(key_name))
    imc.map_key(actions["IA_Weapon_Next"], key("MouseScrollUp"))
    imc.map_key(actions["IA_Weapon_Prev"], key("MouseScrollDown"))

    dkm = imc.get_editor_property("default_key_mappings")
    mappings = list(dkm.get_editor_property("mappings"))
    for m in mappings:
        spec = modifier_specs.get(m.get_editor_property("key").export_text())
        if not spec:
            continue
        mods = []
        if spec[0]:
            sw = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=imc)
            sw.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
            mods.append(sw)
        if spec[1]:
            mods.append(unreal.new_object(unreal.InputModifierNegate, outer=imc))
        m.set_editor_property("modifiers", mods)
    dkm.set_editor_property("mappings", mappings)
    imc.set_editor_property("default_key_mappings", dkm)
    save(imc)
    return actions, imc


# ---------------------------------------------------------------- Data assets
def tag(name):
    # FGameplayTag has no python constructor args and its TagName is not settable; use struct text import.
    t = unreal.GameplayTag()
    t.import_text('(TagName="%s")' % name)
    return t


def build_data(actions):
    ensure_dir("/Game/Data")

    ms, _ = get_or_create("/Game/Data", "DA_MovementSettings_Default", unreal.VeloxMovementSettings, None)
    if ms is not None:
        # ADR 0002 section 1 initial tuning table (UE units). Python names drop the "b" of bool properties.
        values = {
            "unit_scale": 2.5,
            "max_ground_speed": 800.0,
            "ground_acceleration": 10.0,
            "ground_friction": 6.0,
            "stop_speed": 250.0,
            "crouch_speed_scale": 0.25,
            "step_height": 45.0,
            "walkable_floor_angle": 45.5,
            "edge_perch_radius": 15.0,
            "air_acceleration": 1.0,
            "air_max_wish_speed": 800.0,
            "air_control_enabled": False,  # bAirControl (CPM toggle), ScriptName avoids clash with AirControl
            "air_stop_acceleration": 2.5,
            "air_strafe_acceleration": 70.0,
            "air_strafe_max_wish_speed": 75.0,
            "air_control": 150.0,
            "air_control_scale": 80.0,
            "jump_velocity": 675.0,
            "auto_hop": True,
            "gravity_z": -2000.0,
            "ramp_slide_min_normal_speed": 25.0,
            "ramp_exit_min_angle": 5.0,
            "ramp_exit_catch_air": True,
            "knockback_grace_min": 0.05,
            "knockback_grace_max": 0.2,
            "knockback_grace_per_speed": 0.00016,
            "fixed_step": 0.008,
            "fixed_step_integration": True,
            "combine_air_moves": False,
            "haste_speed_scale": 1.3,  # ADR 0004 section 3 (M3 task 03)
        }
        for k, v in values.items():
            try:
                ms.set_editor_property(k, v)
            except Exception as e:  # noqa: BLE001
                fail(f"DA_MovementSettings_Default.{k}: {e}")
        save(ms)

    ic, _ = get_or_create("/Game/Data", "DA_InputConfig_Default", unreal.VeloxInputConfig, None)
    if ic is not None and all(a is not None for a in actions.values()):
        def entry(action, tag_name):
            return unreal.VeloxInputAction(input_action=action, input_tag=tag(tag_name))
        ic.set_editor_property("native_input_actions", [
            entry(actions["IA_Move"], "InputTag.Move"),
            entry(actions["IA_Look"], "InputTag.Look.Mouse"),
            entry(actions["IA_Jump"], "InputTag.Jump"),
            entry(actions["IA_Crouch"], "InputTag.Crouch"),
        ])
        ability = [entry(actions["IA_Fire"], "InputTag.Fire")]
        ability += [entry(actions[f"IA_Weapon_Slot{slot}"], f"InputTag.Weapon.Slot{slot}") for slot in range(1, 10)]
        ability += [entry(actions["IA_Weapon_Next"], "InputTag.Weapon.Next"),
                    entry(actions["IA_Weapon_Prev"], "InputTag.Weapon.Prev")]
        ic.set_editor_property("ability_input_actions", ability)
        save(ic)

    pd, _ = get_or_create("/Game/Data", "DA_PawnData_Default", unreal.VeloxPawnData, None)
    if pd is not None:
        pd.set_editor_property("pawn_class", unreal.VeloxCharacter.static_class())
        pd.set_editor_property("movement_settings", ms)
        pd.set_editor_property("input_config", ic)
        # Pawn geometry (capsule, crouch, eye heights) is a class default of the pawn class (ADR 0003 section 6).
        save(pd)


# ---------------------------------------------------------------- Weapons (ADR 0003 section 4)
Q3_UNIT_CM = 2.5


def q3_spread_deg(spread_units):
    """Q3 spreads are offsets at 8192 units: MACHINEGUN_SPREAD 200, DEFAULT_SHOTGUN_SPREAD 700."""
    return math.degrees(math.atan(spread_units / 8192.0))


# Q3 vanilla g_weapon.c / bg_misc.c numbers at UnitScale 2.5. Table order = slot order (MG 2, SG 3, RL 5, LG 6, RG 7, PG 8).
# fire: "hitscan" (UVeloxGameplayAbility_FireHitscan) or "projectile" (UVeloxGameplayAbility_FireProjectile + AVeloxProjectile,
# task 05). pickup = Q3 weapon pickup quantity. Projectile collision radius 2 cm (ADR 0003 section 7).
WEAPONS = [
    dict(name="MachineGun", slot=2, fire="hitscan", interval=0.1, damage=7, pellets=1, spread=q3_spread_deg(200),
         range_cm=8192 * Q3_UNIT_CM, ammo="Bullets", pickup=40, damage_tag="MachineGun"),
    dict(name="Shotgun", slot=3, fire="hitscan", interval=1.0, damage=10, pellets=11, spread=q3_spread_deg(700),
         range_cm=8192 * Q3_UNIT_CM, ammo="Shells", pickup=10, damage_tag="Shotgun"),
    dict(name="RocketLauncher", slot=5, fire="projectile", interval=0.8, damage=100, speed=900 * Q3_UNIT_CM, lifetime=15.0,
         splash=100, splash_radius=120 * Q3_UNIT_CM, ammo="Rockets", pickup=10, damage_tag="Rocket"),
    dict(name="LightningGun", slot=6, fire="hitscan", interval=0.05, damage=8, range_cm=768 * Q3_UNIT_CM, beam=True,
         ammo="Lightning", pickup=100, damage_tag="Lightning"),
    dict(name="Railgun", slot=7, fire="hitscan", interval=1.5, damage=100, range_cm=8192 * Q3_UNIT_CM, penetrate=True,
         max_pen=4, ammo="Slugs", pickup=10, damage_tag="Rail"),
    dict(name="PlasmaGun", slot=8, fire="projectile", interval=0.1, damage=20, speed=2000 * Q3_UNIT_CM, lifetime=10.0,
         splash=15, splash_radius=20 * Q3_UNIT_CM, ammo="Cells", pickup=50, damage_tag="Plasma"),
]


PROJECTILE_RADIUS_CM = 2.0
FIRE_ABILITIES = {
    "hitscan": lambda: unreal.VeloxGameplayAbility_FireHitscan.static_class(),
    "projectile": lambda: unreal.VeloxGameplayAbility_FireProjectile.static_class(),
}


def build_weapons():
    ensure_dir("/Game/Weapons")
    defs = {}
    for w in WEAPONS:
        da, _ = get_or_create("/Game/Weapons", "DA_Weapon_" + w["name"], unreal.VeloxWeaponDefinition, None)
        if da is None:
            continue
        cue = "GameplayCue.Weapon." + w["name"]
        values = {
            "weapon_tag": tag("Velox.Weapon." + w["name"]),
            "slot": w["slot"],
            "display_name": unreal.Text(w["name"]),
            "fire_ability": FIRE_ABILITIES[w["fire"]](),
            "projectile_class": unreal.VeloxProjectile.static_class() if w["fire"] == "projectile" else None,
            "projectile_radius_cm": PROJECTILE_RADIUS_CM if w["fire"] == "projectile" else 0.0,
            "fire_interval": w["interval"],
            "damage": w["damage"],
            "pellet_count": w.get("pellets", 1),
            "spread_angle_deg": w.get("spread", 0.0),
            "range_cm": w.get("range_cm", 8192 * Q3_UNIT_CM),
            "penetrates_pawns": w.get("penetrate", False),
            "max_penetrations": w.get("max_pen", 1),
            "beam": w.get("beam", False),
            "projectile_speed": w.get("speed", 0.0),
            "projectile_lifetime": w.get("lifetime", 0.0),
            "splash_damage": w.get("splash", 0),
            "splash_radius_cm": w.get("splash_radius", 0.0),
            "knockback_scale": 1.0,
            "damage_tag": tag("Velox.Damage." + w["damage_tag"]),
            "ammo_tag": tag("Velox.Ammo." + w["ammo"]),
            "ammo_per_shot": 1,
            "pickup_ammo": w["pickup"],
            "fire_cue_tag": tag(cue + ".Fire"),
            "impact_cue_tag": tag(cue + ".Impact"),
            "beam_cue_tag": tag(cue + ".Beam") if w.get("beam") else unreal.GameplayTag(),
        }
        for k, v in values.items():
            try:
                da.set_editor_property(k, v)
            except Exception as e:  # noqa: BLE001
                fail(f"DA_Weapon_{w['name']}.{k}: {e}")
        save(da)
        defs[w["name"]] = da

    table, _ = get_or_create("/Game/Weapons", "DA_WeaponTable", unreal.VeloxWeaponTable, None)
    if table is not None:
        if len(defs) != len(WEAPONS):
            fail("weapon definitions missing, DA_WeaponTable not written")
        else:
            table.set_editor_property("weapons", [defs[w["name"]] for w in WEAPONS])
            save(table)
    return table, defs


def build_combat_settings(table, defs):
    """ADR 0003 section 1: DA_CombatSettings_Default. AmmoTypes keep the C++ defaults (six rows, cap 200)."""
    ensure_dir("/Game/Data")
    cs, _ = get_or_create("/Game/Data", "DA_CombatSettings_Default", unreal.VeloxCombatSettings, None)
    if cs is None:
        return
    values = {
        "spawn_health": 125.0,
        "max_health": 100.0,
        "health_hard_cap": 200.0,
        "spawn_armor": 0.0,
        "max_armor": 200.0,
        "armor_protection_percent": 66,
        "self_damage_scale": 0.5,
        "self_knockback_scale": 1.0,
        "knockback_speed_per_damage": 12.5,
        "max_knockback_damage": 200.0,
        "radius_knockback_z_offset": 60.0,
        "weapon_drop_time": 0.2,
        "weapon_raise_time": 0.25,
        "switch_waits_for_refire": True,
        "switch_timing_tolerance_seconds": 0.25,  # ADR 0003 B18
        "beam_keep_alive_seconds": 0.25,  # B23
        "max_rewind_seconds": 0.25,
        "history_window_seconds": 1.0,
        "max_origin_error_cm": 75.0,
        "hitbox_teleport_distance_cm": 150.0,
        "fire_rate_burst_allowance": 3.0,
        "fire_rate_spacing_tolerance": 0.25,  # B19
        "fire_rate_jitter_tolerance_seconds": 0.5,  # task 08b D3
        "projectile_forward_prediction_max": 0.1,
        "min_respawn_delay": 1.7,
        "force_respawn_delay": 20.0,
        "spawn_protection_seconds": 0.0,
        "spawn_protection_ends_on_fire": True,
        "corpse_lifetime": 3.0,
        "spawn_avoid_radius_cm": 500.0,
        "telefrag_damage": 1000,
        "environment_kill_damage": 1000,
        "respawn_request_interval_seconds": 0.25,
        "death_cam_distance_cm": 220.0,
        "death_cam_pitch_degrees": 25.0,
        "death_cam_wall_offset_cm": 12.0,
        "corpse_drop_max_cm": 3000.0,
        "corpse_color_scale": 0.3,
        "hit_marker_seconds": 0.25,
        "hit_marker_size": 14.0,
    }
    for k, v in values.items():
        try:
            cs.set_editor_property(k, v)
        except Exception as e:  # noqa: BLE001
            fail(f"DA_CombatSettings_Default.{k}: {e}")
    if len(cs.get_editor_property("ammo_types")) != 6:
        fail("DA_CombatSettings_Default.ammo_types: expected the six C++ default rows")
    # ADR 0003 B5: the spawn loadout is the only source of spawn ammo (Q3: machine gun with 100 bullets).
    if table is not None and "MachineGun" in defs:
        cs.set_editor_property("weapon_table", table)
        cs.set_editor_property("spawn_loadout", [unreal.VeloxSpawnWeapon(definition=defs["MachineGun"], ammo=100)])
    else:
        fail("DA_CombatSettings_Default: weapon table / machine gun missing")
    save(cs)


# ---------------------------------------------------------------- Player body material
def build_player_material():
    """ADR 0003 section 10: /Game/Materials/M_PlayerBody, unlit, emissive = vector parameter "Color" (set per player at runtime)."""
    ensure_dir("/Game/Materials")
    path = "/Game/Materials/M_PlayerBody"
    atools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
    else:
        mat = atools.create_asset("M_PlayerBody", "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if mat is None:
        fail("could not create " + path)
        return
    mel.delete_all_material_expressions(mat)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    expr = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -300, 0)
    expr.set_editor_property("parameter_name", "Color")
    expr.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    mel.connect_material_property(expr, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(mat)
    if not EAL.save_loaded_asset(mat, False):
        fail("could not save " + path)


# ---------------------------------------------------------------- Map
def build_map():
    ensure_dir("/Game/Maps")
    path = "/Game/Maps/L_Empty"
    if EAL.does_asset_exist(path):
        log("L_Empty exists, skipping map creation")
        return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not les.new_level(path, False):
        fail("new_level failed for " + path)
        return

    static = unreal.ComponentMobility.STATIC
    # Floor: 100 m x 100 m x 1 m engine cube, top surface at Z = 0.
    floor = eas.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
    floor.set_actor_label("Floor")
    comp = floor.static_mesh_component
    comp.set_editor_property("static_mesh", unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
    floor.set_actor_scale3d(unreal.Vector(100, 100, 1))
    comp.set_mobility(static)

    ps = eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 100))
    ps.set_actor_label("PlayerStart")

    sun = eas.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(0, -45, 30))
    sun.set_actor_label("Sun")
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_mobility(static)

    sky = eas.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 600))
    sky.set_actor_label("SkyLight")
    sky.get_component_by_class(unreal.SkyLightComponent).set_mobility(static)

    if not les.save_current_level():
        fail("save_current_level failed")


def main():
    actions, _imc = build_input()
    build_data(actions)
    table, defs = build_weapons()
    build_combat_settings(table, defs)
    build_player_material()
    build_map()
    if ERRORS:
        raise RuntimeError("bootstrap failed: " + "; ".join(ERRORS))
    log("done")


main()
