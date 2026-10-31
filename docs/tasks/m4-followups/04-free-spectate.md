# 04 — Free-spectate death camera

**Milestone:** M4 follow-ups (human decision q16, 2026-10-30; an M3 game-loop feature)
**Agent:** dev-hard
**Status:** todo
**Depends on:** ADR 0004 C11 (this task's ruling), ADR 0003 §8 / B8 / B28 / B43, STATUS question 16

## Goal
A dead human free-flies through the arena until the respawn instead of watching the corpse: the engine's spectating state with a project spectator pawn (`AVeloxSpectatorPawn`, client-local, unreplicated), selected per mode by `UVeloxGameModeDefinition::DeathCamera` (default `FreeSpectate`; `Corpse` keeps today's path). The server clamps the client-synced view point into the arena; a dead player keeps the player's information rules (no spectator item timers, player rows); fire/jump after `MinRespawnDelay` respawns as today; bots, dummies, forced respawn and the intermission keep working. B8's death order is preserved.

## Scope — files/modules the agent may touch
- `Source/Velox/Character/VeloxCharacter.cpp` — `CompleteDeath`: `NotifyControllerDied(Controller, Corpse)` replaces `PC->SetViewTarget(this)`; eye/rotation capture in `HandleDeath`
- `Source/Velox/GameModes/VeloxGameMode.h/.cpp`, `VeloxGameModeMatch.cpp` — `NotifyControllerDied` branch, `SpectatorClass`, the nav-bounds AABB cache, `EnterIntermission` ordering
- `Source/Velox/GameModes/VeloxGameModeDefinition.h` — `EVeloxDeathCamera DeathCamera`; `Tools/editor/create_item_content.py` — writes the field for `DA_GameMode_FFA/Duel`
- `Source/Velox/GameModes/VeloxPlayerController.h/.cpp` — `BeginDeathSpectate`, `ClientBeginDeathSpectate`, `EndDeathSpectate`, `BeginSpectatingState` override, `ServerSetSpectatorLocation_Implementation` override, `GetInputConfig()` accessor, logging
- `Source/Velox/GameModes/VeloxSpectatorPawn.h/.cpp` (new), `Source/Velox/GameModes/VeloxSpectateMath.h` (new, world-free clamp)
- `Source/Velox/Combat/VeloxCombatSettings.h` + `Config/DefaultGame.ini` — the six `DeathSpectate*` settings; `Tools/editor/create_bootstrap_content.py` — `DA_CombatSettings_Duel` (copy of Default, `ForceRespawnDelay = 5`) and `DA_GameMode_Duel.CombatSettingsOverride`; `Source/Velox/Tests/VeloxMatchRulesTest.cpp` — the Duel delay and drift-guard rows; `Source/Velox/UI/VeloxUISettings.h` — the prompt string; `Source/Velox/UI/VeloxHUDViewModel.cpp` — the prompt selection only
- `Source/Velox/Movement/VeloxScriptedInputComponent.h/.cpp` — the `serverKill` row and `ServerKillSelfTest` RPC (non-shipping, `serverHaste` pattern); `Content/Data/MoveScripts/DeathSpectateRun.json` (new)
- `infra/ci/nettest.sh`, `infra/ci/README.md` — `DeathSpectateRun` (default list), the `-ini:` override
- `Source/Velox/Tests/VeloxDeathTests.cpp`, `Source/Velox/Tests/VeloxUITests.cpp` — the C11 (2) tests
(Out of scope: `Bots/*`, relevance/replication-graph changes, follow-cam, any Blueprint. Report back if the engine facts in C11 (1) differ.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` §5, §6, **C11**; `docs/adr/0003-combat-gas-and-lag-compensation.md` §8, B8 (death order), B28 (AI auto-respawn), B43 (supersession note)
- UE 5.8 `Engine/Source/Runtime/Engine/Private/PlayerController.cpp`: `BeginSpectatingState`, `SpawnSpectatorPawn`, `ServerSetSpectatorLocation_Implementation`, `GetPlayerViewPoint`, `OnPossess`, `ClientRestart_Implementation`; `GameModeBase.cpp`: `RestartPlayerAtPlayerStart`, `MustSpectate`
- `docs/tasks/m2-combat/notes/06-report.md` (death flow as built), `docs/tasks/m4-followups/01-max-rewind-035.md` (format)

## Acceptance criteria
- [ ] The six engine facts of C11 (1) are verified and quoted (file + line) in the notes before the implementation; a differing fact is reported first. (Done 2026-10-31: (a) corrected — `SetReplicates(false)` is in `SpawnSpectatorPawn`, not the constructor; (c) replaced — the client syncs at ≈ 4 Hz, not per tick; architect ruling in C11: keep the engine rate, no own send.)
- [ ] Clamp `Dt` is floored at `DeathSpectateSyncDtFloorSeconds` and `BeginDeathSpectate` seeds the last accepted point with the death eye; `AVeloxSpectatorPawn` does not touch `bReplicates`.
- [ ] Duel: `DA_GameMode_Duel.CombatSettingsOverride = DA_CombatSettings_Duel` with `ForceRespawnDelay = 5`; FFA override null (20). `Velox.GameMode.ModeDefinitions` gains the C11 (2) rows (Duel 5, FFA 20, `DeathCamera = FreeSpectate` both, reflection drift guard over every other `UVeloxCombatSettings` property).
- [ ] `DeathCamera = FreeSpectate`: after a death the owning client is in `NAME_Spectating` with an `AVeloxSpectatorPawn` at the death eye/rotation, flying with `IA_Move`/`IA_Look`/`IA_Jump` (up)/`IA_Crouch` (down) at ≤ `DeathSpectateSpeedCmS`, contained by the 07f clip; no spectator pawn exists on a dedicated server (`spectator_pawn=0`); `DeathCamera = Corpse` reproduces today's view-the-corpse behaviour.
- [ ] Fire/jump after `MinRespawnDelay` respawns (requested) and `ForceRespawnDelay` respawns (forced) exactly as before; both ends leave the spectating state and destroy the spectator pawn; the new pawn is possessed with `GE_SpawnInit`. Bots and dummies: no change in `VeloxRespawn` lines of `BotRun`/`BotDuel`.
- [ ] `ServerSetSpectatorLocation` is clamped by `VeloxSpectate::ClampViewPoint` (nav-bounds AABB + margin, `KillZ + 50`, speed step); clamps are logged and counted; `clamps=0` in the soak.
- [ ] Dead ≠ spectator: with `ItemTimers = Spectators`, a dead player's HUD shows no item timers and the scoreboard keeps him in the player rows (unit row).
- [ ] Intermission while spectating → `Inactive`, spectator pawn destroyed, intermission camera shown; the Live reset restarts the player normally.
- [ ] No literals: the six `DeathSpectate*` settings and the prompt string live in settings; the mode rule and the Duel delay in the data assets.
- [ ] Tests: `Velox.GameMode.SpectateClamp`, `Velox.GameMode.DeathSpectate`, the UI row — green; `infra/ci/build.sh` CI OK; `infra/ci/fts.sh` 7/7.
- [ ] `infra/ci/nettest.sh --scripts DeathSpectateRun --repeat 2` PASS with the C11 (2) pass rule; `--scripts FireRun,FireRunControl,BotRun --repeat 1` PASS (death flow and bots unaffected).

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`, tests incl. the three new ones.
2. `infra/ci/fts.sh` → `OK 7/7`.
3. `infra/ci/nettest.sh --scripts DeathSpectateRun --repeat 2` → both rounds PASS; the detail line shows `spectate=free`, `forced/auto` at ≈ 4 s, `spectator_pawn=0`, `clamps=0`, `10 ≤ syncs ≤ 20` (engine 4 Hz throttle; C11 (2) rule — `≥ 60` was written against the false fact (c)), client `begin`/`end reason=respawn`.
4. `infra/ci/nettest.sh --scripts FireRun,FireRunControl,BotRun --repeat 1` → PASS.
5. Headless check of the `Corpse` rule: `velox.combat.KillSelf` on `L_CombatTest` with `DA_GameMode_FFA` temporarily set to `Corpse` through a cvar/ini override (state the mechanism) → `VeloxDeath step=6 spectate=corpse`.

## Report back
Standard format plus the verified engine facts, the final settings values, the measured `syncs` count per round, and a one-paragraph description of what a dead player can see (Duel decided: free, 5 s). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
