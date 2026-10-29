# 10 — Main menu, settings and pause UI, connect by IP

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** todo
**Depends on:** 07, 09; ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 2-3 h; Wave 4 (parallel with 08)

## Goal
Implement ADR 0004 implementation-notes task **`10-menus`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Main menu, settings and pause UI, connect by IP. The ADR sections §7 menus are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/UI/*` - `UVeloxMainMenuWidget` (connect by IP, host local, settings, quit), `UVeloxSettingsWidget` (sensitivity, FOV, keybind list with rebind, audio, video, apply/defaults), `UVeloxPauseMenuWidget`
- `Source/Velox/GameModes/VeloxMenu*.*` - `AVeloxMenuGameMode`, `AVeloxMenuPlayerController`
- `Source/Velox/Core/VeloxGameInstance.*` - `ConnectToServer`, network-failure return
- `Config/DefaultEngine.ini` - `GameDefaultMap`
- `Tools/editor/create_bootstrap_content.py` - `L_MainMenu`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§7 menus; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] Headless `-game` starts on `L_MainMenu` with the widget.
- [ ] `ConnectToServer("127.0.0.1:7777")` against a headless `Velox -server L_ItemTest` joins (log); a failure returns to the menu with a message.
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
- Engine facts: `UUserWidget::RebuildWidget` + `UWidgetTree::ConstructWidget` (programmatic widget trees; `if (!WidgetTree->RootWidget) {...} return Super::RebuildWidget();`); `UEnhancedInputUserSettings` / `UPlayerMappableKeySettings` (`bEnableUserSettings` in `EnhancedInputDeveloperSettings`) for the rebind list. Port 7777 in the acceptance text is the default; use the lead-assigned port for the actual run.
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
