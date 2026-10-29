# 06 — Scoreboard data, kill feed, input actions

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** todo
**Depends on:** 05; ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 2 h; Wave 3

## Goal
Implement ADR 0004 implementation-notes task **`06-scoreboard-data-feed`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Scoreboard data, kill feed, input actions. The ADR sections §6 are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/GameModes/VeloxPlayerState.*` - `Kills`, `Deaths`, `bReady`, `Reset`
- `Source/Velox/GameModes/VeloxGameState.*`, `VeloxGameMode.cpp`, `VeloxKillFeed.h` - `FVeloxKillFeed` FastArray, `OnKillFeedEntry`, pickup-taken events for spectators (`LastTakenBy` already in 01)
- `Tools/editor/create_bootstrap_content.py` - `IA_Ready`/`IA_Scoreboard`/`IA_Menu` + `InputTag.Ready`/`Menu`
- `Source/Velox/Tests/VeloxMatchRulesTest.cpp`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§6; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] A client log shows kill-feed entries with names and tags for kills that happened before and after it joined (last `KillFeedLength 8` entries replicate to late joiners).
- [ ] `Kills`/`Deaths` incremented in `PlayerDied`; `Reset()` zeroes them; `FVeloxKillEvent` unchanged (B29).
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
- Engine fact: `FFastArraySerializer` callbacks `PostReplicatedAdd` on clients plus the server path both broadcast `OnKillFeedEntry` (verify in 5.8). `AGameModeBase::ResetLevel` (resets every controller and every actor whose `ShouldReset` is true, calls `AActor::Reset`), `APlayerState::Reset` (score to 0), `APawn::Reset` (a pawn without a controller or with a player-state controller is destroyed), `AGameMode::PlayerCanRestart_Implementation` (false unless `MatchState == InProgress`) for `APlayerState::Reset`.
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
