# M3-05 match state — agent report (2026-10-29)

build.sh 55 tests (worktree, base without M3-01), 0 failures (`Saved/CI/20261029-210147-3805236`); nettest StrafeRun+FireRun 1/1 C=0 (`Saved/CI/20261029-210518-3814933`).

## Design
- `VeloxMatchRules::Evaluate(FVeloxMatchInput) -> FVeloxMatchVerdict` world-free, whole ADR §5 table, unit-tested. Input from PlayerArray (active players, AI = not human, always ready, Logout-excluded). Evaluated on kill/join/leave/ready (next tick, B27), 1 Hz timer (`RulesTimerIntervalSeconds`), exact one-shot at phase end.
- Engine match state: `ReadyToStartMatch` true once GameState exists; InProgress → `EndMatch()` at PostMatch → `SetMatchState(InProgress)` on restart (StartMatch refuses after start).
- `RestartMatch`: remove MatchFrozen → `ResetLevel()` → InProgress (engine restarts human PCs) → `RestartPlayer` for other active controllers (dummies keep fixed respawn) → `SetPhase`. `Reset()` drops pending respawns. Spectator pawns kept via `ShouldReset_Implementation`.
- `AVeloxGameState` (push model): `Phase` (OnRep → `OnPhaseChanged`), `PhaseStart/PhaseEnd/LiveStartServerTime`, `FragLimit`, `TimeLimitSeconds`, `OvertimeCount`, `WinnerPlayerState`; `GameModeDefinition`, `CombatSettingsOverride` initial-only; `ForceNetUpdate` per phase change. `AVeloxPlayerState::bReady` replicated, cleared in `Reset`.
- Duel: `InitNewPlayer` → `StartSpectatingOnly` at `MaxActivePlayers`; `RequestJoin` undoes it (only Warmup/PostMatch via `bAllowJoinDuringMatch`).
- PostMatch: `UVeloxGE_MatchFrozen` + `DisableMovement`, `ClientEnterIntermission(ViewTarget)` (camera tagged `Intermission` else own pawn), no scoring, no respawn.
- Base `AVeloxGameMode` has no definition → rules off (M1/M2 maps and soaks unchanged). FFA/Duel via DefaultGame.ini; `?FragLimit=`/`?TimeLimit=` override.
- Files: `GameModes/VeloxMatchRules.*`, `VeloxGameModeDefinition.*`, `VeloxMatchEffects.*` (MatchFrozen, tags Velox.State.MatchFrozen, Velox.Mode.FFA/Duel), `VeloxGameModeMatch.cpp`, `VeloxGameStateMatch.cpp`, `VeloxGameMode_FFA.*`, `VeloxGameMode_Duel.*`, `VeloxMatchCheats.cpp` (`velox.match.Phase/Ready/OnPhase/AddPlayer/RemovePlayer/Join`); tests `Velox.GameMode.MatchRules`, `ModeDefinitions`; `DA_GameMode_FFA/Duel`.

## Evidence
FFA (FragLimit 3, 2 dummies): Warmup → Countdown 1.001 → reset_level spawned 3/3 → Live 21.001 → 3 kills → PostMatch 22.733 `reason=frag_limit` freeze asc=3 → reset → Warmup 37.733 → Countdown 38.001. Duel (3 locals): third becomes spectator; ready → Countdown; join refused in Countdown; RemovePlayer → PostMatch `reason=forfeit`; join in PostMatch; reset → Warmup. Dedicated + client: client logs all phases, receives `ClientEnterIntermission`.

## Engine facts verified
`ResetLevel` GameModeBase.cpp:348/341/333; `APlayerState::Reset` PlayerState.cpp:230; `APawn::Reset` Pawn.cpp:934; `APlayerController::Reset/ClientReset` :1278/:1297; `AController::Reset` Controller.cpp:442; `PlayerCanRestart` GameMode.cpp:548; StartMatch no-op after start :186/:297; HandleMatchHasStarted :203. **`GameModeClassAliases` is on `UGameMapsSettings` (DefaultEngine.ini), not AGameModeBase** (GameMapsSettings.h:232, EngineSettingsModule.cpp:125/140). Spectators GameModeBase.cpp:772/1078/1001, GameMode.cpp:91, PlayerController.cpp:5848. GameState spawned in `PreInitializeComponents` (GameModeBase.cpp:116).

## Departures (architect)
- Ready kept from Warmup into Countdown only (ADR "cleared on every phase change" would abort a Duel countdown).
- New `bAllowJoinDuringMatch` field.
- Live/Overtime with no active players → Warmup with reset; PostMatch early exit needs ≥1 human, all humans ready; frag-limit end on the next tick.
- MatchFrozen lives in `GameModes/VeloxMatchEffects.*`.

## Hand-offs
- 02: damage execution treats MatchFrozen like Dead; fire abilities add it to `ActivationBlockedTags`; `AVeloxProjectile::Reset` → Destroy (rockets survive ResetLevel).
- 01 (follow-up): item settings from `AVeloxGameState::GetGameModeDefinition()` (null in base mode); `EVeloxItemTimerVisibility` is in `VeloxMatchRules.h`; call `Registry->SetModeRules` from the definition; MatchFrozen in `AVeloxPickup::IsPawnEligible`.
- 06: add `Kills`/`Deaths`, zero in `AVeloxPlayerState::Reset`; `PlayerDied` returns early in PostMatch.
- 07/08: GameState getters, `OnPhaseChanged` (read `GetPhase()` on bind; first Warmup has no OnRep), `AVeloxPlayerController::IsInIntermission()`; clocks from server time.
- 10: console `Ready`/`Join`; menu join calls `ServerRequestJoin`.
- Unread yet: `CombatSettingsOverride`, `PawnDataOverride`. Not measured: intermission correction count on a remote client.
- Merged as dfea0e2 (create_item_content.py add/add resolved: items + modes in one script; DefaultEngine.ini GameModeClassAliases FFA/Duel). Merged build 59 tests, 0 failures (`Saved/CI/20261029-210911-3822918`); `?game=FFA` resolves to VeloxGameMode_FFA.
