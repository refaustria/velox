# 04 — Bot controller and navigation: input injection, Velox path follower, link maneuvers, AddBot/KickBot

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** todo
**Depends on:** 01, 02, 03; ADR 0005 §1, §2 (tick phases), §4 (traversal), §5, §12

## Goal
A bot possesses an `AVeloxCharacter`, drives it exclusively through the player input entry points, follows nav paths with strafe-jumping and bunny-hopping, traverses jump-pad, drop and gap links through its own maneuvers, roams when it has no goal, and can be added/kicked by console command, URL option or client RPC with a name, a colour and a scoreboard row. Combat (05) and item routing (06) plug into two stubs. The ROADMAP's functional tests "bot reaches a jump-pad-only platform" and "bots strafe-jump along long corridors" pass.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotController.{h,cpp}` (new): §1 constructor flags, `UpdateControlRotation` override (no `Super`), `Tick` with phases Perceive/Decide/Navigate/Act (`UpdateCombat(Dt)` and `UpdateGoal(Dt)` as virtual stubs; Act applies an idle aim toward the path direction), `ApplyInput`, `FVeloxBotState`, possession lifecycle, forced goals, `Roam`, stuck handling, `GetNavFilter()` from preset + state
- `Source/Velox/Bots/VeloxBotPathFollowingComponent.{h,cpp}` (new): `FollowPathSegment` (no `Super`) → `VeloxBotSteering::Steer` or the maneuver; `SetMoveSegment` link detection; `OnPathFinished` → controller
- `Source/Velox/Bots/VeloxBotSubsystem.{h,cpp}` — `AddBot`, `KickBot`, `NumBots`, bot registry, `BotIndex`; `Bots/VeloxBotCheats.cpp` — `AddBot`, `KickBot`, `velox.bots.List/Goal/Freeze/Debug`
- `Source/Velox/Character/VeloxCharacter.{h,cpp}` — `SetJumpInput(bool)` extracted from `Input_JumpStarted/Completed` (both paths call it)
- `Source/Velox/GameModes/VeloxGameMode.{h,cpp}` — `?Bots=`, `?BotsFill=`, `?BotDifficulty=` in `InitGame` → subsystem after `InitGameState`; `GameModes/VeloxPlayerController.{h,cpp}` — `ServerAddBot/ServerKickBot` (`#if !UE_BUILD_SHIPPING`, rate-limited) and the client-side console forwarding
- `Source/Velox/Tests/VeloxBotFunctionalTest.cpp` (new): `FT_BotNavigation` (pad + drop cases; the rocket-jump case is added by 05), `FT_BotStrafeCorridor`; `Tools/editor/create_item_test_map.py` / `create_movement_test_map.py` — place the `AFunctionalTest` actors if the pattern of `VeloxItemFunctionalTest.cpp` needs them
- `docs/movement-tooling.md` — `velox.bots.*` commands
(Anything not listed is out of scope. `Movement/`, `Combat/`, `Weapons/`, `Items/`, `Net/` are not touched beyond `SetJumpInput`.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §1 (entry points, order, `UpdateControlRotation`, lifecycle), §2 (phases, state, goal struct), §4 (JumpPad/Drop/Jump/JumpFast traversal, logging), §5 (follower, steering, stuck), §12 (AddBot/KickBot, names, colours, URL, RPC), §13 (no stats yet), §14 (functional tests, tooling)
- `docs/adr/0002-movement-component-design.md` §8; `docs/adr/0003-combat-gas-and-lag-compensation.md` B6 (ability input after rotation), B28 (AI auto-respawn), B29 (labels/colours); `docs/adr/0004-game-loop-items-modes-ui.md` C8 (AI always ready)
- `Source/Velox/Bots/VeloxDummyController.cpp` (input pattern), `Source/Velox/Movement/VeloxScriptedInputComponent.cpp` (`SelectWeaponSlot`, jump handling)

## Acceptance criteria
- [ ] The controller never reads or writes `Velocity`, never calls `Launch`/`RequestDirectMove`/`SetActorLocation`; grep-verifiable. Input order per ADR §1 (rotation → `AddMovementInput` → `SetLocalMoveInput` → `SetJumpInput` → crouch → ability tags → `ProcessAbilityInput`).
- [ ] `FT_BotNavigation` (L_ItemTest, `-UseFixedTimeStep -FPS=60`, `velox.bots.Seed 1`): nav built < 5 s; Medium bot with goal `Velox.Item.Health.Mega` → `VeloxBotLink type=JumpPad result=ok` and `OnPickupTaken(Mega, bot)` within 25 s; goal `Velox.Item.Armor.Red` → a `Drop` traversal and RA taken within 35 s.
- [ ] `FT_BotStrafeCorridor` (L_MovementTest): Hard bot to the corridor end → `PeakSpeed2D ≥ 1.25 × MaxGroundSpeed` before 5120 cm, arrival ≤ 7 s; Easy bot arrives ≤ 9 s and never exceeds `MaxGroundSpeed + 1`.
- [ ] `AddBot Hard Celer` on a headless listen world (`L_CombatTest?listen`): PlayerState name `Celer`, `IsABot()`, a palette colour, scoreboard row; `KillTest` on it produces a kill-feed entry with its name; it auto-respawns after `MinRespawnDelay`; `KickBot Celer` removes it and `Logout` runs; `?Bots=2?BotDifficulty=Easy` spawns two at map start; `AddBot` refused at `MaxActivePlayers`.
- [ ] From a nettest client, `AddBot` forwards through `ServerAddBot` and the bot appears in the client's scoreboard data.
- [ ] `infra/ci/build.sh` green; `nettest.sh` default list unchanged (one run of `StrafeRun,PadRun,FireRun` at least).

## Verification steps
1. `infra/ci/build.sh` (functional tests run under `Velox.Functional`).
2. `UnrealEditor-Cmd Velox.uproject "L_CombatTest?listen" -game -nullrhi -ExecCmds="AddBot Hard Celer, velox.bots.Debug 1" …` → log lines above; `velox.combat.KillTest`.
3. `infra/ci/nettest.sh --port <N> --scripts StrafeRun,PadRun,FireRun`.

## Report back
Standard dev-hard format with a design summary for `architect` review: tick order evidence (controller before pawn), how link segments are matched, any deviation from the §1 entry points, measured per-bot tick cost (rough, `stat` or timers).

## Notes / outcome
(Filled by the agent or the lead after completion.)
