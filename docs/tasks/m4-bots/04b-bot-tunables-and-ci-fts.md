# 04b — Bot tunables into data (ReactionJitter, fight score, maneuver constants) and a CI script for the headless functional tests

**Milestone:** M4 Bots
**Agent:** dev
**Status:** todo
**Depends on:** 04 (merged), the wave-3 merge of 05 and 06 (this task edits `SetMoveSegment` and one line of 06's routing glue; run it after both are merged). ADR 0005 D4, D9 (`docs/reviews/m4-01-02-04-review.md` F2, F3).

## Goal
No bot tunable is a literal in code: `ReactionJitter` is a preset field like the rest of the §11 table, the fight-score parameters and the five maneuver constants are `UVeloxBotSettings` properties, and the three headless functional tests (`FT_ItemPickup`, `FT_BotNavigation`, `FT_BotStrafeCorridor`) have one CI entry point that runs them and checks their exit codes. Behaviour is unchanged (every new field defaults to today's value).

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotTypes.h` — `float ReactionJitter = 0.3f;` in `FVeloxBotDifficultyParams` (aim block)
- `Source/Velox/Bots/VeloxBotDifficulty.{h,cpp}` — `ReactionJitter` UPROPERTY (ClampMin 0, ClampMax 1), `BuildBaseParams`, the `VELOX_BOT_FLOAT_FIELDS` cvar table
- `Source/Velox/Bots/VeloxAimModel.cpp` — `FVeloxAimModel::MakeParams`: `P.ReactionJitter = Difficulty.ReactionJitter` (nothing else in the aim model)
- `Source/Velox/Bots/VeloxBotSettings.{h,cpp}` — new `Config` properties: `Bots|Goals`: `float FightBaseScore = 20`, `float FightFullStrength = 100`, `float FightWeakWeaponScale = 0.5`; `Bots|Navigation`: `float LinkStartAcceptanceCm = 30`, `float JumpFastSpeedFraction = 0.9`, `float JumpFastRunUpCm = 600`, `int32 JumpFastRetries = 1`, `float RocketJumpImpulseTimeoutSeconds = 0.3`
- `Source/Velox/Bots/VeloxBotPathFollowingComponent.cpp` — `SetMoveSegment` only: fill `PendingParams.StartAcceptanceCm / RequiredSpeedFraction / RunUpCm / MaxRetries / ImpulseTimeoutSeconds` from the settings (next to the existing `EndAcceptanceCm` / `TakeoffLeadCm` lines)
- `Source/Velox/Bots/VeloxBotItemRouting.cpp` (06's file, one statement) — where `FVeloxBotDecisionInput::Fight` is filled (marked `// 04b` by 06): read the three settings fields. If 06 did not leave the marker, add the assignment where the decision input is assembled and report it.
- `Tools/editor/create_bot_content.py` — `ReactionJitter: 0.3` in all four presets, verification step extended; `Content/Bots/DA_Bot_Easy/Medium/Hard/Expert.uasset` regenerated
- `Config/DefaultGame.ini` — `[/Script/Velox.VeloxBotSettings]` entries for the new fields (explicit, equal to the defaults)
- `infra/ci/fts.sh` (new), `infra/ci/README.md` (one paragraph), `docs/movement-tooling.md` (the `fts.sh` line in the bots section)
- `Source/Velox/Tests/VeloxBotTests.cpp` — one `AimError` row only: `ReactionJitter 0` → the first aim change lands at exactly `ReactionSeconds ± 1 tick`
(Anything not listed is out of scope. `VeloxBotController.*`, `VeloxBotCombat.*`, the maneuver core and the ADR-side formulas are not touched.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §4 (maneuver constants), §2/§11 (fight score, `ReactionJitter`), §14 (functional tests), amendments **D4**, **D9**
- `docs/reviews/m4-01-02-04-review.md` §3 F2, F3
- `Source/Velox/Tests/VeloxItemFunctionalTest.cpp` (`velox.items.FtItemPickup [quit]`), `Source/Velox/Tests/VeloxBotFunctionalTest.cpp` (header comment: exact command lines and flags), `infra/ci/build.sh` / `nettest.sh` (log directory and `[ci]` output conventions)

## Acceptance criteria
- [ ] `grep -rn "0\.3f\|20\.f\|100\.f\|0\.5f\|600\.f" Source/Velox/Bots/VeloxBotDecision.h Source/Velox/Bots/VeloxAimModel.h Source/Velox/Bots/VeloxBotManeuver.h` still shows the struct defaults (the pure cores keep plain defaults), but every wired value comes from the asset or `UVeloxBotSettings`: `MakeParams` copies `ReactionJitter`; `SetMoveSegment` copies the five maneuver constants; the routing glue copies the three fight params.
- [ ] `UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=<abs>/Tools/editor/create_bot_content.py -nullrhi -unattended …` regenerates the four presets with `ReactionJitter = 0.3`; `velox.bots.ListPresets` on a headless `-game` world lists them; `velox.bots.ReactionJitter 0` overrides it like the other `velox.bots.<Field>` cvars.
- [ ] `infra/ci/fts.sh [--port N]` runs, in order, `L_ItemTest` + `velox.items.FtItemPickup quit`, `L_ItemTest` + `velox.bots.FtNavigation quit`, `L_MovementTest` + `velox.bots.FtStrafeCorridor quit`, each as `UnrealEditor-Cmd Velox.uproject <map> -game -nullrhi -unattended -nosound -UseFixedTimeStep -FPS=60 -log -ExecCmds="velox.bots.Seed 1, <cmd>"` under a 240 s timeout; logs go to `Saved/CI/<stamp>/fts-<name>.log`; it greps `result=PASS` and the exit code; prints `[fts] <name> PASS|FAIL` per test and ends with `[fts] OK 3/3` (exit 0) or `[fts] FAIL k/3` (exit 1). Three consecutive runs on the idle machine: 3/3 each.
- [ ] `infra/ci/build.sh` green (92 tests with the new `AimError` row); `fts.sh` is **not** called from `build.sh` (≈ 3 min wall); README says when to run it (every bot task, before a merge to `main`).
- [ ] `FT_BotNavigation` and `FT_BotStrafeCorridor` results are identical to the 04 report within noise (the defaults did not change).

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`, `Velox.Bots.AimError` lists the jitter-0 row.
2. The content script command above, then `UnrealEditor-Cmd Velox.uproject L_CombatTest -game -nullrhi -ExecCmds="velox.bots.ListPresets, quit"` → four `DA_Bot_*` lines.
3. `infra/ci/fts.sh --port <N>` three times → `[fts] OK 3/3` each; paste the three summaries and one `VeloxBotFt … result=PASS` line per test.

## Report back
Standard dev format. List every new property with its default, the `DefaultGame.ini` lines, the exact `fts.sh` command lines, and whether 06's `// 04b` marker existed.

## Notes / outcome
(Filled by the agent or the lead after completion.)
