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
| `FireRateJitterToleranceSeconds` (`fire_rate_jitter_tolerance_seconds`) | 0.5 s | B32/B37, the one resend tolerance for every receive-time decision on a remote shooter. (1) Fire rate: receive-time delay spread the server fire-rate bucket absorbs; capacity per weapon = `1 + tolerance / FireInterval` (MG 6, PG 6, LG 11, SG 1.5, RL 1.625, RG 1.333), no separate spacing rule (after a full bucket two shots are at least `FireInterval - tolerance` apart: rail 1.0 s once, then 1.5 s on average). A 2x fire-rate client is rejected after about this long. (2) Switch timing (B18, B33): the server accepts a remote shooter's shot while `GE_WeaponSwitching` has at most this left, and a superseding switch completes the pending one while at most `WeaponRaiseTime` + this is left; the shooter's own machine gates on its local `SwitchEndTime` exactly. `FireRateBurstAllowance` and B19's `FireRateSpacingTolerance` were deleted in task 08c, the separate 0.25 s switch tolerance in task 08h. |
| `BeamKeepAliveSeconds` (`beam_keep_alive_seconds`) | 0.25 s | B23: the LG beam cue stays this long (at least one fire interval) after the last accepted shot. |
| `MaxRewindSeconds` (`max_rewind_seconds`) | 0.30 s | B34: oldest server pose a hitscan shot may rewind to, and the B25 projectile fire-stamp window. A hinted shot needs RTT + 20-40 ms, so 0.30 covers the 250 ms RTT limit (0.25 covered about 210 ms). No cvar. |

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

## Bot content (M4-03)

`Tools/editor/create_bot_content.py` writes the four difficulty presets `DA_Bot_Easy`, `DA_Bot_Medium`, `DA_Bot_Hard`, `DA_Bot_Expert` (`UVeloxBotDifficulty`) to `/Game/Bots` with the ADR 0005 section 11 table and the section 9 weapon preferences (RG/LG weights x 0.5 for Easy, x 0.8 for Medium). Run it after the Velox editor module is built (`UVeloxBotDifficulty` and the Velox weapon tags must exist):
```
"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$VELOX_PROJECT" -run=pythonscript \
  -script="$PWD/Tools/editor/create_bot_content.py" -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
```
Idempotent: values are compared and an asset is only re-saved when something changed; the script then re-reads every asset and raises when a value differs from the table. Project-wide bot settings (`UVeloxBotSettings`: `DefaultDifficulty`, 16 Latin `BotNames`, `DefaultBotCount`, nav/goal tunables) are in `Config/DefaultGame.ini` `[/Script/Velox.VeloxBotSettings]`. Check that the registry sees the presets headless: `UnrealEditor-Cmd "$VELOX_PROJECT" L_Empty -game -nullrhi -ExecCmds="velox.bots.ListPresets" -stdout` logs `VeloxBotPreset asset=...` lines and `VeloxBotPreset count=4 names=16 default=...`. Non-shipping per-field overrides: `velox.bots.<Field> <value>` (`-1e30` = use the asset), applied by `UVeloxBotDifficulty::BuildParams()`.
