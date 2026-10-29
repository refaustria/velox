# 07 — HUD: view model, widget, damage direction cues

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** todo
**Depends on:** 01, 02, 06; ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 3 h; Wave 3

## Goal
Implement ADR 0004 implementation-notes task **`07-hud`** (`docs/adr/0004-game-loop-items-modes-ui.md`): HUD: view model, widget, damage direction cues. The ADR sections §7 HUD are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/UI/*` - `UVeloxUserWidget`, `UVeloxUISettings`, `UVeloxHUDViewModel` + `VeloxHUDFormat`, `UVeloxHUDWidget` (all elements of §7); `AVeloxHUD` creates it and drops the Canvas hit marker/death overlay
- `Source/Velox/Weapons/VeloxCombatFeedbackComponent.*` - damage direction recording
- `Source/Velox/Combat/VeloxHealthSet.cpp` or the cue call site - damage cue parameters (`Location` from the server)
- `Config/DefaultGame.ini` (`[/Script/Velox.VeloxHUD] HUDWidgetClass=...`)
- `Source/Velox/Tests/VeloxUITests.cpp`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§7 HUD; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] `Velox.UI.HUDFormat` green (clock, powerup ceil, weapon bar rows, feed line).
- [ ] `FT_HUDWidget` green: constructs `UVeloxHUDWidget` under NullRHI with a hand-filled view model and asserts the bound texts.
- [ ] Headless `-game -nullrhi` log shows the widget constructed with the bound values; visual check deferred to the playtest (task 12).
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
- Engine facts to verify: `UUserWidget::RebuildWidget` + `UWidgetTree::ConstructWidget` (programmatic widget trees; `if (!WidgetTree->RootWidget) {...} return Super::RebuildWidget();`).
- Widgets read the view model in `NativeTick` and never touch gameplay objects. Elements declared `UPROPERTY(meta = (BindWidgetOptional))`, `UCLASS(Blueprintable)`. Crosshair etc. without textures or materials. Whether UI stays C++ is open question 2.
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
