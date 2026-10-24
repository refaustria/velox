"""Velox bootstrap content: L_Empty map, Enhanced Input assets, pawn/movement/input/combat data assets.

Run headless (see docs/content-bootstrap.md):
  UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_bootstrap_content.py \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput

Idempotent: existing assets are loaded and their properties re-applied; the map is rebuilt only if missing.
Fails (non-zero exit via exception) if anything could not be created or saved.
"""
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
        "IA_Fire": make_action("IA_Fire", VT.BOOLEAN),
    }
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
    imc.map_key(actions["IA_Fire"], key("LeftMouseButton"))

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
        ])
        ic.set_editor_property("ability_input_actions", [entry(actions["IA_Fire"], "InputTag.Fire")])
        save(ic)

    pd, _ = get_or_create("/Game/Data", "DA_PawnData_Default", unreal.VeloxPawnData, None)
    if pd is not None:
        pd.set_editor_property("pawn_class", unreal.VeloxCharacter.static_class())
        pd.set_editor_property("movement_settings", ms)
        pd.set_editor_property("input_config", ic)
        # ADR 0002 section 1 pawn geometry (cm): Q3 box 30 x 30 x 56 u, eye 50 u above the feet.
        pd.set_editor_property("capsule_radius", 37.5)
        pd.set_editor_property("capsule_half_height", 70.0)
        pd.set_editor_property("eye_height", 55.0)
        save(pd)


def build_combat_settings():
    """ADR 0003 section 1: DA_CombatSettings_Default. AmmoTypes keep the C++ defaults (six rows, cap 200, 100 bullets)."""
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
        "armor_protection": 0.6667,
        "self_damage_scale": 0.5,
        "self_knockback_scale": 1.0,
        "knockback_speed_per_damage": 12.5,
        "max_knockback_damage": 200.0,
        "radius_knockback_z_offset": 60.0,
        "weapon_drop_time": 0.2,
        "weapon_raise_time": 0.25,
        "switch_waits_for_refire": True,
        "max_rewind_seconds": 0.25,
        "history_window_seconds": 1.0,
        "max_origin_error_cm": 75.0,
        "fire_rate_burst_allowance": 3.0,
        "projectile_forward_prediction_max": 0.1,
        "min_respawn_delay": 1.7,
        "force_respawn_delay": 20.0,
        "spawn_protection_seconds": 0.0,
        "spawn_protection_ends_on_fire": True,
        "corpse_lifetime": 3.0,
        "spawn_avoid_radius_cm": 500.0,
    }
    for k, v in values.items():
        try:
            cs.set_editor_property(k, v)
        except Exception as e:  # noqa: BLE001
            fail(f"DA_CombatSettings_Default.{k}: {e}")
    if len(cs.get_editor_property("ammo_types")) != 6:
        fail("DA_CombatSettings_Default.ammo_types: expected the six C++ default rows")
    save(cs)


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
    build_combat_settings()
    build_map()
    if ERRORS:
        raise RuntimeError("bootstrap failed: " + "; ".join(ERRORS))
    log("done")


main()
