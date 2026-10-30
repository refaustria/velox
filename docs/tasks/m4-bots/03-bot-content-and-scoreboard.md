# 03 — Bot content: difficulty assets, settings, names, scoreboard BOT marker

**Milestone:** M4 Bots
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0005 §11, §12 (data/UI side only; parallel with 01 and 02)

## Goal
The four difficulty presets exist as data assets with the ADR §11 table values, project-wide bot settings live in a developer-settings class backed by `DefaultGame.ini`, bot names come from a configurable list, and the scoreboard/HUD mark bot rows. Nothing spawns a bot yet.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotDifficulty.{h,cpp}` (new: `UVeloxBotDifficulty : UPrimaryDataAsset` with every §11 field, `FVeloxBotWeaponPreference`, `BuildParams()` → `FVeloxBotDifficultyParams` applying non-shipping `velox.bots.<Field>` cvar overrides, sentinel pattern of `velox.move.*`), `Bots/VeloxBotSettings.{h,cpp}` (new: `UVeloxBotSettings : UDeveloperSettings`, `Config=Game`, fields per ADR §11)
- `Source/Velox/Bots/VeloxBotTypes.h` — only if 02 has not created it: the `FVeloxBotDifficultyParams` POD (coordinate with the lead; one owner)
- `Config/DefaultGame.ini` — `[/Script/Velox.VeloxBotSettings]` section (names list, defaults)
- `Tools/editor/create_bot_content.py` (new, idempotent, pattern of `create_item_content.py`): `Content/Bots/DA_Bot_Easy/Medium/Hard/Expert`; `docs/content-bootstrap.md` — new section
- `Source/Velox/UI/VeloxHUDViewModel.*`, `UI/VeloxScoreboardWidget.*`, `UI/VeloxHUDFormat.h` — a `bIsBot` flag on the scoreboard row; the ping column shows `BOT`; `Tests/VeloxUITests.cpp` — one row test
- `Content/Bots/*.uasset` (LFS)
(Anything not listed is out of scope.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §9 (weapon preference defaults), §11 (fields, preset table, settings), §12 (`bIsABot`, names)
- `docs/adr/0004-game-loop-items-modes-ui.md` §6/§7 (scoreboard rows, view model rule: widgets read view models only)
- `Tools/editor/README.md`, `docs/content-bootstrap.md`

## Acceptance criteria
- [ ] `UVeloxBotDifficulty` has every field of the ADR §11 table with `EditDefaultsOnly`, clamps and units; `BuildParams()` returns the POD with cvar overrides applied; `UVeloxBotSettings` fields per ADR with the `DefaultGame.ini` defaults (16 Latin names, `DefaultBotCount 0`, `DefaultDifficulty` Medium).
- [ ] `create_bot_content.py` writes the four assets with exactly the table values and the §9 weapon preferences (Easy RG/LG × 0.5, Medium × 0.8); re-running changes nothing.
- [ ] A `-game -nullrhi` run logs the four `DA_Bot_*` assets found through the asset registry (temporary log in `UVeloxBotSettings::PostInitProperties` or a `velox.bots.ListPresets` command).
- [ ] Scoreboard row shows `BOT` for a PlayerState with `IsABot()`; `FT_HUDWidget`/`VeloxUITests` extended with a bot row.
- [ ] `infra/ci/build.sh` green.

## Verification steps
1. Run the content script headless; list `Content/Bots/`.
2. `infra/ci/build.sh`; grep the automation log for the UI row test.

## Report back
Standard dev format.

## Notes / outcome
(Filled by the agent or the lead after completion.)
