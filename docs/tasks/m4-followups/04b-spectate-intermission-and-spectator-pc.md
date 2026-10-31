# 04b — Death camera: intermission order, Velox controller for spectators, begin-rotation

**Milestone:** M4 follow-ups (architect review of task 04, `docs/adr/reviews/0004-C11-review.md`)
**Agent:** dev-hard
**Status:** todo
**Depends on:** task 04 (merged, `post-m4` 5f5fec6), ADR 0004 C11 amendment 3 (2026-10-31), ADR 0003 B27/B43

## Goal
Four small fixes from the C11 design review, all in the death-camera and spectator path. D1: the victim of the match-ending frag no longer gets a death camera on top of the intermission (`EvaluateMatch` runs before `CompleteDeath` on the same tick; the fix is order-independent). D2: `?SpectatorOnly=1` logins become `AVeloxPlayerController`s, so spectators get the Velox binds, the view-point clamp, the scoreboard buttons and the intermission view. D3: a remote client keeps its own control rotation when the spectator pawn appears (no snap). D4: every `Spectating` controller, not only a death-spectating one, goes `Inactive` at the intermission. Plus the nits listed in the review where the same file is touched.

## Scope — files/modules the agent may touch
- `Source/Velox/GameModes/VeloxGameMode.cpp` — constructor (`ReplaySpectatorPlayerControllerClass`), `NotifyControllerDied` (intermission guard, `spectate=intermission` log), `GetSpectateBounds` header comment lifetime note (in `.h`)
- `Source/Velox/GameModes/VeloxGameMode.h` — comment only
- `Source/Velox/GameModes/VeloxPlayerController.cpp` — `PlaceDeathSpectatorPawn` (client keeps its control rotation), `EnterIntermission` (drop the `bDeathSpectating` condition), `BeginDeathSpectate` (single seed after `ChangeState`, keep the comment)
- `Source/Velox/Tests/VeloxDeathTests.cpp` — D1 and D4 cases in `Velox.GameMode.DeathSpectate` (or a sibling test in the same file)
- `Source/Velox/Tests/VeloxMatchRulesTest.cpp` — `Velox.GameMode.ModeDefinitions` row for the spectator controller class
- `infra/ci/nettest.sh` — comment only (the `clamps=0` note from the review's nits); `infra/ci/README.md` — one sentence on the HUD prompt under the CI cvar
- `docs/tasks/m4-followups/04b-spectate-intermission-and-spectator-pc.md` — outcome notes
(Out of scope: `Bots/*`, the clamp math, the spectator pawn class, any Blueprint, ADR text. Report back if the engine facts in the review differ.)

## Relevant ADRs / references
- `docs/adr/reviews/0004-C11-review.md` — D1–D4, nits, decisions (a)–(c)
- `docs/adr/0004-game-loop-items-modes-ui.md` C11 and its amendment 3 (2026-10-31)
- `docs/adr/0003-combat-gas-and-lag-compensation.md` B27 (death completes on the next tick), B43
- UE 5.8.4 `GameModeBase.cpp` `SpawnPlayerController` (`ReplaySpectatorPlayerControllerClass`, `SpectatorOnly=1`), `PlayerController.cpp` `Reset` (`bPlayerIsWaiting = !IsOnlyASpectator()`), `EndSpectatingState`, `BeginInactiveState`

## Acceptance criteria
- [ ] D1: `AVeloxGameMode::NotifyControllerDied` takes neither death-camera branch for a player controller that `IsInIntermission()` (Velox PC) or while `bMatchRulesEnabled && Phase == PostMatch` (any PC); the view target is untouched; log `VeloxDeath step=6 spectate=intermission controller=`; the pending respawn is still armed as before. The rule must not depend on timer order.
- [ ] D1 test: in `Velox.GameMode.DeathSpectate` (harness `DeathFlow`), kill the pawn, call `W.PC->EnterIntermission(nullptr)` *before* advancing the tick that runs `CompleteDeath`, then advance: not `Spectating`, `GetSpectatorPawn() == nullptr`, `IsDeathSpectating()` false, the step-6 line contains `spectate=intermission`. The existing case 5 (intermission *after* the spectate began) stays green.
- [ ] D2: `AVeloxGameMode` constructor sets `ReplaySpectatorPlayerControllerClass = AVeloxPlayerController::StaticClass()`; `Velox.GameMode.ModeDefinitions` gains a row asserting it on the CDO. `BotDuel --repeat 1` PASS with the spectator client's log free of `engine default spectator bindings` and showing the Velox spectator pawn bound from `DA_InputConfig_Default`.
- [ ] D3: `PlaceDeathSpectatorPawn` on a client (`GetLocalRole() < ROLE_Authority`) sets the spectator pawn's rotation and the control rotation from the client's *current* control rotation and uses only the server's eye position; on the authority (listen host, standalone test) behaviour unchanged. The existing "control rotation = rotation at death" assertion still holds in the standalone test.
- [ ] D4: `EnterIntermission` moves any `Spectating` controller to `Inactive` (plus `ClientGotoState(Inactive)` for a remote one), not only a death-spectating one. Test: harness PC after `StartSpectatingOnly()` + `ChangeState(Spectating)` → `EnterIntermission(nullptr)` → `Inactive`, spectator pawn destroyed, `IsOnlyASpectator()` still true.
- [ ] Nits: single seed in `BeginDeathSpectate`; `GetSpectateBounds` comment says the cache lives for the game mode (one map); `nettest.sh` comment on the order-two `clamps` false positive; README sentence on the HUD prompt under `velox.combat.ForceRespawnDelay`.
- [ ] `infra/ci/build.sh` CI OK (all tests, including the new rows); `infra/ci/fts.sh` 7/7; `nettest.sh --scripts DeathSpectateRun,BotDuel,BotRun --repeat 1` PASS; `DeathSpectateRun --repeat 2` PASS with the C11 (2) rule unchanged.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`; the test count grows by the new rows, `0 not successful`.
2. `infra/ci/fts.sh` → `OK 7/7`.
3. `infra/ci/nettest.sh --scripts DeathSpectateRun --repeat 2` → 2/2 PASS, `clamps=0`, `10 ≤ syncs ≤ 20`, client `begin`/`end reason=respawn`.
4. `infra/ci/nettest.sh --scripts BotDuel,BotRun --repeat 1` → PASS; in the BotDuel client log: no `engine default spectator bindings`, the spectator pawn's bind line names `DA_InputConfig_Default`.
5. Headless D1 check (state the mechanism): standalone `L_CombatTest?game=FFA -nullrhi` with a frag limit of 1 through the mode's data or a cvar, kill → the server log shows `VeloxMatch freeze` then `VeloxDeath step=6 spectate=intermission`, no `VeloxSpectate begin`.

## Report back
Standard format plus: the step-6 line of the D1 headless check, the BotDuel spectator bind line, and whether the `FTimerManager` order (EvaluateMatch before CompleteDeath) was observed in a log. Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
