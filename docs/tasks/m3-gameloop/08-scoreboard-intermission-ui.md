# 08 — Scoreboard widget, phase banners, spectator timers

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** done (merged ea57949)
**Depends on:** 06, 07; ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 2 h; Wave 4 (parallel with 10)

## Goal
Implement ADR 0004 implementation-notes task **`08-scoreboard-intermission-ui`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Scoreboard widget, phase banners, spectator timers. The ADR sections §7 (scoreboard, banners), §5 intermission, §4 item timers are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/UI/*` - `UVeloxScoreboardWidget` (Tab hold, forced in PostMatch, ready button/F3), phase banners and countdown, spectator item-timer list, "took" messages
- `Source/Velox/GameModes/VeloxPlayerController.*`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§7 (scoreboard, banners), §5 intermission, §4 item timers; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] `FT_HUDWidget` extended for the scoreboard rows green.
- [ ] Headless log of the forced scoreboard at PostMatch.
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
- Engine fact: `UUserWidget::RebuildWidget` + `UWidgetTree::ConstructWidget` (programmatic widget trees; `if (!WidgetTree->RootWidget) {...} return Super::RebuildWidget();`). Sorting is client-side (score desc, kills desc, deaths asc, name). Item timers visible to spectators only unless open question 4 is answered otherwise.
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
