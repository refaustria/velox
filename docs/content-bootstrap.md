# Content bootstrap

`Tools/editor/create_bootstrap_content.py` generates the minimal content the C++ skeleton references. Re-running it is safe: existing assets are loaded and their properties re-applied; `L_Empty` is only built if missing (delete `Content/Maps/L_Empty.umap` to rebuild it).

## Assets created
- `/Game/Maps/L_Empty` - 100x100 m floor (static), `PlayerStart`, static `DirectionalLight` and `SkyLight`.
- `/Game/Input/IA_Move` (Axis2D, WASD), `IA_Look` (Axis2D, Mouse2D, no modifiers), `IA_Jump` (bool, Space), `IA_Fire` (bool, LMB), `IMC_Default`.
- `/Game/Data/DA_MovementSettings_Default` (ADR 0002 section 1 values for the fields that exist), `DA_InputConfig_Default` (IA_* to `InputTag.*`), `DA_PawnData_Default` (`AVeloxCharacter`, the two above).
- `Config/DefaultGame.ini` points `DefaultMappingContext` and `DefaultPawnData` at them.

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
