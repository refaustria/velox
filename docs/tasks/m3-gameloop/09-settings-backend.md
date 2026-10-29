# 09 — Settings backend: GameUserSettings, input rebinding, application

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** todo
**Depends on:** none (parallel with 07, wave 1 capable); ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 2 h; Wave 1 / finishing in Wave 3

## Goal
Implement ADR 0004 implementation-notes task **`09-settings-backend`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Settings backend: GameUserSettings, input rebinding, application. The ADR sections §7 settings are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/UI/VeloxGameUserSettings.*` - `UVeloxGameUserSettings` + `GameUserSettingsClassName`
- `Source/Velox/Character/VeloxCharacter.*` - sensitivity/FOV/invert application in `CalcCamera`
- `Source/Velox/GameModes/VeloxPlayerController.*` - `bAutoSwitchOnPickup` wiring
- `Source/Velox/Core/VeloxGameInstance.*` - `PlayerName` -> `?Name=`
- `Config/DefaultEngine.ini`, `Config/DefaultInput.ini`
- `Tools/editor/create_bootstrap_content.py` - sound classes/mix assets, `UPlayerMappableKeySettings` per action
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§7 settings; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] Values survive a restart in `Saved/Config/*/GameUserSettings.ini`.
- [ ] A rebound key survives a restart (save-slot file present).
- [ ] Headless log of the applied FOV/sensitivity.
- [ ] Volume application via `SC_Master/SC_Effects/SC_Music`; AA/frame-rate cvars applied.
- [ ] `infra/ci/build.sh` green (`[ci] CI OK`, all `Velox.*` tests).

## Standing rules
- The nettest port is assigned by the lead; pass it via `nettest.sh --port <N>`. Do not pick your own. Soak only on an idle machine.
- Unity-build safe names: no file-scope `using namespace`, no duplicate file-local helper names across `.cpp` files (use anonymous namespaces with distinctive names).
- Tunables live in data assets (`UVeloxItemDefinition`, `UVeloxGameModeDefinition`, `UVeloxCombatSettings`, `UVeloxMovementSettings`, `UVeloxUISettings`, `UVeloxGameUserSettings`) or `.ini`, never as magic numbers.
- Every change is covered by a test (automation test or functional test).
- No commit by the agent; the lead commits. Leave the project compiling; end with `infra/ci/build.sh` showing `[ci] CI OK` (and `nettest.sh` where named). State explicitly if Unreal was unavailable.
- Report ADR disagreements instead of deviating.

## Verification steps
1. `infra/ci/build.sh` -> `[ci] CI OK`.
2. The headless/automation/soak runs named in the acceptance criteria, with the lead-assigned nettest port.

## Report back
Standard format for the agent type (result, what changed, verification with commands + output tail, open issues). Note explicitly if Unreal was not available to compile.

## Notes
- Engine facts to verify: `UEnhancedInputUserSettings` / `UPlayerMappableKeySettings` (`bEnableUserSettings` in `EnhancedInputDeveloperSettings`).
- Q3 convention: degrees per count = 0.022 x Sensitivity, default 5; FOV horizontal default 105, range 80-130, `AspectRatio_MaintainXFOV`.
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
