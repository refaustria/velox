# Content bootstrap

`Tools/editor/create_bootstrap_content.py` generates the minimal content the C++ skeleton references. Re-running it is safe: existing assets are loaded and their properties re-applied; `L_Empty` is only built if missing (delete `Content/Maps/L_Empty.umap` to rebuild it).

## Assets created
- `/Game/Maps/L_Empty` - 100x100 m floor (static), `PlayerStart`, static `DirectionalLight` and `SkyLight`.
- `/Game/Input/IA_Move` (Axis2D, WASD), `IA_Look` (Axis2D, Mouse2D, no modifiers), `IA_Jump` (bool, Space), `IA_Crouch` (bool, Left Ctrl), `IA_Fire` (bool, LMB), `IMC_Default`.
- `/Game/Data/DA_MovementSettings_Default` (ADR 0002 section 1 values for the fields that exist), `DA_InputConfig_Default` (IA_* to `InputTag.*`), `DA_PawnData_Default` (`AVeloxCharacter`, the two above; pawn geometry is a class default of the pawn class, ADR 0003 section 6).
- `Config/DefaultGame.ini` points `DefaultMappingContext` and `DefaultPawnData` at them.
- `/Game/Weapons/DA_Weapon_*` and `DA_WeaponTable`, `/Game/Data/DA_CombatSettings_Default` - see "Weapons and combat settings" below.

## Weapons and combat settings (ADR 0003 sections 1, 4, 5)

`build_weapons()` writes one `UVeloxWeaponDefinition` per entry of the `WEAPONS` list to `/Game/Weapons/DA_Weapon_<Name>` (`MachineGun`, `Shotgun`, `RocketLauncher`, `LightningGun`, `Railgun`, `PlasmaGun`) and `DA_WeaponTable` listing them **in slot order** (MG 2, SG 3, RL 5, LG 6, RG 7, PG 8). The table index is the weapon's network id (inventory bitmask, `CurrentIndex`, switch event magnitude), so never reorder or insert in the middle; append new weapons at the end.

Q3 derivation (vanilla `g_weapon.c` / `bg_misc.c`; the script computes the numbers, do not hand-edit them in the assets):
- Distances: Q3 units x `Q3_UNIT_CM` (2.5 cm per Q3 unit). Hitscan range 8192 u = 20480 cm, LG 768 u = 1920 cm, RL splash radius 120 u = 300 cm, PG 20 u = 50 cm, RL speed 900 u/s = 2250 cm/s, PG 2000 u/s = 5000 cm/s.
- Spreads: Q3 gives an offset at 8192 u; `q3_spread_deg(units) = atan(units / 8192)`: MG 200 -> 1.3987 deg, SG 700 -> 4.8838 deg (11 pellets).
- Intervals (s), damage, pickup ammo: MG 0.1 / 7 / 40, SG 1.0 / 10 per pellet / 10, RL 0.8 / 100 (+100 splash) / 10, LG 0.05 / 8 / 100, RG 1.5 / 100 / 10 (penetrates up to 4 pawns), PG 0.1 / 20 (+15 splash) / 50. One ammo per shot.
- Tags: `Velox.Weapon.<Name>`, `Velox.Damage.<...>`, `Velox.Ammo.<Type>`, cues `GameplayCue.Weapon.<Name>.Fire|Impact` (LG also `.Beam`). `fire_ability` is `UVeloxGameplayAbility_FireHitscan` for MG/SG/LG/RG; RL/PG get theirs with task 05.

`build_combat_settings()` writes `/Game/Data/DA_CombatSettings_Default`: the `values` table (field = snake_case of the C++ `UVeloxCombatSettings` property, the class defaults equal the ADR table), `weapon_table = DA_WeaponTable` and `spawn_loadout = [MachineGun, 100 ammo]` (B5: the loadout is the only source of spawn ammo). `ammo_types` keeps the six C++ default rows. Weapon timing fields (task 04b), all with a non-shipping `velox.combat.<Field>` cvar override:

| Field (script key) | Default | Meaning |
|---|---|---|
| `SwitchTimingToleranceSeconds` (`switch_timing_tolerance_seconds`) | 0.25 s | B18: the server accepts a remote shooter's shot while `GE_WeaponSwitching` has at most this left; the shooter's own machine gates on its local `SwitchEndTime` exactly. |
| `FireRateSpacingTolerance` (`fire_rate_spacing_tolerance`) | 0.25 s | B19: server minimum spacing between accepted shots of one weapon = `max(0, FireInterval - tolerance)` (rail 1.25 s, SG 0.75 s, RL 0.55 s, MG/PG/LG 0), on top of the token bucket (`FireRateBurstAllowance` 3). |
| `FireRateJitterToleranceSeconds` (`fire_rate_jitter_tolerance_seconds`) | 0.5 s | Task 08b (D3): receive-time delay spread the fire-rate bucket absorbs; capacity per weapon = `max(FireRateBurstAllowance, 1 + tolerance / FireInterval)` (MG 6, LG 11, PG 6, rail/SG/RL 3). A 2x fire-rate client is rejected after about this long. |
| `BeamKeepAliveSeconds` (`beam_keep_alive_seconds`) | 0.25 s | B23: the LG beam cue stays this long (at least one fire interval) after the last accepted shot. |

After adding a field to `UVeloxCombatSettings`, add its key to `values` in `build_combat_settings()`, rebuild (`infra/ci/build.sh`) and re-run the script; commit only the assets that actually changed (the script re-saves every asset it touches).

## Linux (this VM, no GPU)
```
source infra/.env
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$VELOX_PROJECT" -run=pythonscript \
  -script="$PWD/Tools/editor/create_bootstrap_content.py" \
  -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
```
The Velox module must be built first (`infra/ci/build.sh`). Exit code is non-zero if the script raised.

Smoke test the map (the commandlet-style Cmd binary idles after load, so use `timeout`):
```
timeout 60 "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$VELOX_PROJECT" L_Empty -game -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
```
Expect `LogLoad: Game class is 'VeloxGameMode'` and `Bringing World /Game/Maps/L_Empty.L_Empty up for play`.

Headless cook (host platform). `RunUAT.sh BuildCookRun` currently fails on the trimmed engine (missing `VisionOS.Automation.dll`), so use the cook commandlet:
```
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$VELOX_PROJECT" -run=cook -targetplatform=Linux \
  -map=/Game/Maps/L_Empty -iterate -unattended -nopause -nosplash -nullrhi -stdout -FullStdOutLogOutput
```
Output goes to `Saved/Cooked/Linux/` (gitignored). First run takes ~4 minutes on CPU.

## Mac fallback
Same arguments, with the Launcher install's binary and your project path:
```
"/Users/Shared/Epic Games/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd" "$HOME/path/to/velox/Velox.uproject" \
  -run=pythonscript -script="$HOME/path/to/velox/Tools/editor/create_bootstrap_content.py" \
  -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
```
(`-nullrhi` is optional on the Mac.) Alternatively open the editor and run the script via Tools > Execute Python Script.

## Re-running after settings changes

The script is idempotent: existing data assets (e.g. `DA_MovementSettings_Default`) are loaded and every property value in the script is re-applied and saved, so after adding/renaming fields on `UVeloxMovementSettings` just update the value table in `build_data()` and re-run the script. Only the map (`L_Empty`) is skipped if it exists. Note: re-running also re-saves `IMC_Default.uasset`, which may show up as a modified binary in git.
