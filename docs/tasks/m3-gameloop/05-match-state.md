# 05 — Match state: mode definitions, phase machine, ready-up, spectators

**Milestone:** M3 Game loop
**Agent:** dev-hard
**Status:** todo
**Depends on:** none (parallel with 01; uses GE_MatchFrozen from 02 if merged); ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 3 h; Wave 1

## Goal
Implement ADR 0004 implementation-notes task **`05-match-state`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Match state: mode definitions, phase machine, ready-up, spectators. The ADR sections §5 are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/GameModes/*` - `UVeloxGameModeDefinition`, `VeloxMatchRules.h`, phase machine, `AVeloxGameState` replication, ready-up, spectator slots/`join`, forfeit, 1 Hz rules timer, intermission view, FFA/Duel subclasses + aliases, cheats `velox.match.Phase <name>` / `ready`
- `Config/DefaultGame.ini`
- `Tools/editor/create_item_content.py` (mode assets section: `DA_GameMode_FFA/Duel`)
- `Source/Velox/Tests/VeloxMatchRulesTest.cpp`
- `MatchFrozen` application: GE from 02; if 02 is not merged yet define `UVeloxGE_MatchFrozen` here and let 02 adopt it
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§5; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] `Velox.GameMode.MatchRules` green (frag limit ends, time limit with leader ends, tie -> Overtime (Duel) / ends (FFA), sudden-death frag ends, countdown requires ready in Duel and not in FFA, countdown aborts below `MinPlayers`, forfeit).
- [ ] Headless FFA with two dummies: Warmup -> Countdown (20 s) -> `ResetLevel` -> Live -> frag limit 3 reached via `KillTest` -> PostMatch -> Warmup after 15 s, each phase logged `VeloxMatch phase=`.
- [ ] Duel with a third controller -> spectator.
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
Standard format for the agent type (result, what changed, verification with commands + output tail, open issues). Include a design summary for `architect` review. Note explicitly if Unreal was not available to compile.

## Notes
- Verify the engine facts first, report mismatches: `AGameModeBase::ResetLevel` (resets every controller and every actor whose `ShouldReset` is true, calls `AActor::Reset`), `APlayerState::Reset` (score to 0), `APawn::Reset` (a pawn without a controller or with a player-state controller is destroyed), `AGameMode::PlayerCanRestart_Implementation` (false unless `MatchState == InProgress`); `AGameModeBase` `GameModeClassAliases` (`?game=FFA`).
- Engine match state stays `InProgress` from `BeginPlay` through Overtime and becomes `WaitingPostMatch` via `EndMatch()` at PostMatch. Clients compute every clock from `GetServerWorldTimeSeconds()`.
- Mode defaults pending human confirmation (open question 3): FFA 20 frags / 10 min, Duel 10 min sudden death, ready-up required, warmup countdown 20 s, intermission 15 s.
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
