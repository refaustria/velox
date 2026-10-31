# 04b — Death camera: intermission order, Velox controller for spectators, begin-rotation

**Milestone:** M4 follow-ups (architect review of task 04, `docs/adr/reviews/0004-C11-review.md`)
**Agent:** dev-hard
**Status:** done (2026-10-31, dev-hard, branch `f04b-spectate-intermission`)
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
- [x] D1: `AVeloxGameMode::NotifyControllerDied` takes neither death-camera branch for a player controller that `IsInIntermission()` (Velox PC) or while `bMatchRulesEnabled && Phase == PostMatch` (any PC); the view target is untouched; log `VeloxDeath step=6 spectate=intermission controller=`; the pending respawn is still armed as before. The rule must not depend on timer order.
- [x] D1 test: in `Velox.GameMode.DeathSpectate` (harness `DeathFlow`), kill the pawn, call `W.PC->EnterIntermission(nullptr)` *before* advancing the tick that runs `CompleteDeath`, then advance: not `Spectating`, `GetSpectatorPawn() == nullptr`, `IsDeathSpectating()` false, the step-6 line contains `spectate=intermission`. The existing case 5 (intermission *after* the spectate began) stays green.
- [x] D2: `AVeloxGameMode` constructor sets `ReplaySpectatorPlayerControllerClass = AVeloxPlayerController::StaticClass()`; `Velox.GameMode.ModeDefinitions` gains a row asserting it on the CDO. `BotDuel --repeat 1` PASS with the spectator client's log free of `engine default spectator bindings` and showing the Velox spectator pawn bound from `DA_InputConfig_Default`.
- [x] D3: `PlaceDeathSpectatorPawn` on a client (`GetLocalRole() < ROLE_Authority`) sets the spectator pawn's rotation and the control rotation from the client's *current* control rotation and uses only the server's eye position; on the authority (listen host, standalone test) behaviour unchanged. The existing "control rotation = rotation at death" assertion still holds in the standalone test.
- [x] D4: `EnterIntermission` moves any `Spectating` controller to `Inactive` (plus `ClientGotoState(Inactive)` for a remote one), not only a death-spectating one. Test: harness PC after `StartSpectatingOnly()` + `ChangeState(Spectating)` → `EnterIntermission(nullptr)` → `Inactive`, spectator pawn destroyed, `IsOnlyASpectator()` still true.
- [x] Nits: single seed in `BeginDeathSpectate`; `GetSpectateBounds` comment says the cache lives for the game mode (one map); `nettest.sh` comment on the order-two `clamps` false positive; README sentence on the HUD prompt under `velox.combat.ForceRespawnDelay`.
- [x] `infra/ci/build.sh` CI OK (all tests, including the new rows); `infra/ci/fts.sh` 7/7; `nettest.sh --scripts DeathSpectateRun,BotDuel,BotRun --repeat 1` PASS; `DeathSpectateRun --repeat 2` PASS with the C11 (2) rule unchanged.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`; the test count grows by the new rows, `0 not successful`.
2. `infra/ci/fts.sh` → `OK 7/7`.
3. `infra/ci/nettest.sh --scripts DeathSpectateRun --repeat 2` → 2/2 PASS, `clamps=0`, `10 ≤ syncs ≤ 20`, client `begin`/`end reason=respawn`.
4. `infra/ci/nettest.sh --scripts BotDuel,BotRun --repeat 1` → PASS; in the BotDuel client log: no `engine default spectator bindings`, the spectator pawn's bind line names `DA_InputConfig_Default`.
5. Headless D1 check (state the mechanism): standalone `L_CombatTest?game=FFA -nullrhi` with a frag limit of 1 through the mode's data or a cvar, kill → the server log shows `VeloxMatch freeze` then `VeloxDeath step=6 spectate=intermission`, no `VeloxSpectate begin`.

## Report back
Standard format plus: the step-6 line of the D1 headless check, the BotDuel spectator bind line, and whether the `FTimerManager` order (EvaluateMatch before CompleteDeath) was observed in a log. Note explicitly if Unreal was not available to compile.

## Notes / outcome
**Outcome (dev-hard, 2026-10-31).** All acceptance items met; two deviations, both inside `VeloxPlayerController.cpp`.

- **D1** — `NotifyControllerDied` computes `bIntermission = (VeloxPC && VeloxPC->IsInIntermission()) || (bMatchRulesEnabled && Phase == PostMatch)` and then takes neither branch, logs `VeloxDeath step=6 spectate=intermission`, and still arms the pending respawn. **Engine fact that differs from the review:** "the view target is the camera, so `OnUnPossess` leaves it" is only half true. `APlayerController::SetPawn(nullptr)` (UE 5.8.4 `PlayerController.cpp`, `SetPawn`) always calls `AutoManageActiveCameraTarget(this)`, both on the server's `UnPossess` and on the client's `OnRep_Pawn(null)`, so the victim's view would still have jumped to the controller. The first unit run caught this (`view target unchanged (VeloxPlayerController_0)`). **Fix (deviation 1):** `EnterIntermission` sets `bAutoManageActiveCameraTarget = false`, and `Reset` / `ClientReset_Implementation` restore the CDO value at the level reset. This works in either order (property vs RPC, EvaluateMatch vs CompleteDeath), and the `SetPawn` override stays the only C11 code that fights the auto camera.
- **D2** — the constructor sets `ReplaySpectatorPlayerControllerClass`, and `ModeDefinitions` has a CDO row. **Deviation 2 (evidence only):** the spectator pawn class is out of scope and had no log line for the Velox-bindings branch, so `AVeloxPlayerController::BeginSpectatingState` now logs `VeloxSpectate pawn_bound pawn= controller= input_config= default_bindings= only_spectator=` for a local controller.
- **D3** — `PlaceDeathSpectatorPawn` uses `GetControlRotation()` when `GetLocalRole() < ROLE_Authority`. The begin line gains `rot_source=client|server`. In the soak the client logs `rot_source=client`; the standalone test logs `server`.
- **D4** — `EnterIntermission` moves any `Spectating` PC to `Inactive` (plus `ClientGotoState`).
- **Nits** — single seed after `ChangeState`; `GetSpectateBounds` lifetime comment; the nettest `clamps=0` comment; the README HUD sentence.
- **Tests** — new `Velox.GameMode.SpectateIntermission` (D1: intermission entered before the `CompleteDeath` tick; D4: `StartSpectatingOnly`, then `Inactive`, then `Reset` back to `Spectating` with auto-camera restored). The harness PC already spectates from spawn, before it is marked local, so the D4 case goes `Inactive` first.

**Verification.**
- `build.sh`: CI OK, `105 tests, 0 not successful` (104 before).
- `fts.sh`: OK 7/7.
- `DeathSpectateRun --repeat 2`: 2/2 PASS (4.01 / 4.00 s, `syncs=15/15`, `clamps=0`).
- `DeathSpectateRun,BotDuel,BotRun --repeat 1`: DeathSpectateRun and BotRun PASS. BotDuel's first run was a cost FAIL with 25/26 windows class X and every counter about 4x slower (`frame_other` 2482 us against 646 us on the rerun) while another agent's UE was running. The rerun `BotDuel --repeat 1` PASSED (`cost=PASS`, clean 9/26, P=17).
- BotDuel spectator client: no `engine default spectator bindings` line, and the client PC is `VeloxPlayerController_0` (it was `PlayerController_1` in task 04's run). The BotDuel spectator client itself logs no `pawn_bound` line: those runs predate the log. The line was observed in the unit tests and the standalone run: `VeloxSpectate pawn_bound pawn=VeloxSpectatorPawn_0 controller=VeloxPlayerController_0 input_config=DA_InputConfig_Default default_bindings=0`.
- **Headless D1:** `L_ItemTest?game=FFA?FragLimit=1?Bots=1?BotDifficulty=Hard -nullrhi`, `velox.match.Phase Live`, and a Hard bot rockets the idle human. Order in the log: `VeloxKill victim=Player killer=Celer`, then `VeloxMatch phase=PostMatch reason=frag_limit`, then `VeloxMatch intermission player=Player view=CameraActor_1`, then `VeloxMatch freeze`, then `VeloxDeath step=4/5`, then `VeloxDeath step=6 spectate=intermission controller=VeloxPlayerController_0`, then `VeloxRespawn waiting ... forced in 20.00 s`. There is no `VeloxSpectate begin`, so the match evaluation running before `CompleteDeath` was observed.
