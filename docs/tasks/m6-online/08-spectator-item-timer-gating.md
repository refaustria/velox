# 08 — Server-side gating of item-timer data; dead players are players

**Milestone:** M6 Online
**Agent:** dev-hard
**Status:** done (2026-11-11)
**Depends on:** ADR 0007 §5.5; ADR 0004 §1–§2, §5, C10, C11 amendment 3 (vii)

## Goal
Item respawn times stop leaving the server toward anyone not entitled to them. Pickups replicate availability and a take counter; the timers spectators may see travel owner-only on the receiving PlayerState; a **dead human in the spectating state keeps the player's information rules** (no timers), which is how ADR 0004 C11 amendment 3 (vii)'s "the M6 gating must treat dead players as spectators, or it leaks through the death camera" is satisfied: the death camera cannot show what the connection never receives. ADR 0004 C10's age rule for late joiners is replaced by a clock-free baseline rule.

## Scope — files/modules the agent may touch
- `Source/Velox/Items/VeloxPickup.h/.cpp` — replication (`bAvailable`, `TakeCount`, `LastTakenBy`; `RespawnServerTime` server-only), `PreNetReceive`/`PostNetReceive` baseline rule, `OnRep_Available`/`OnRep_TakeCount` (split of today's `OnRep_RespawnServerTime`), cosmetics/cue/placeholder paths, `IsAvailable()` reading the new flag on clients, `GetRemaining()` server-only (or via the PlayerState on clients)
- `Source/Velox/Items/VeloxPickupRules.h` — delete `IsStaleTake`; `Source/Velox/Combat/VeloxCombatSettings.*` and `Tools/editor/create_bootstrap_content.py`, `docs/content-bootstrap.md` — delete `PickupTakeEventMaxAgeSeconds`
- `Source/Velox/Items/VeloxItemRegistry.h/.cpp` — server: `PushTimersTo(AVeloxPlayerState*)`, entitlement evaluation (`UVeloxGameModeDefinition::ItemTimers` + `IsOnlyASpectator()`), hooks on take/respawn/join/leave/`ResetLevel`
- `Source/Velox/GameModes/VeloxPlayerState.h/.cpp` — `ItemTimers : TArray<FVeloxItemTimerEntry { uint8 ItemIndex; float RespawnServerTime; }>` (`COND_OwnerOnly`, push model, `OnRep_ItemTimers`), `GetItemTimer(ItemIndex)`; `VeloxGameMode.cpp`/`VeloxGameModeMatch.cpp` — call the registry on `PostLogin`, `ServerRequestJoin`, `Logout`, phase reset
- `Source/Velox/UI/VeloxHUDViewModel.cpp`, `VeloxScoreboardWidget.cpp`/`VeloxScoreboardFormat.h` — read timers from the PlayerState; `ItemTimersVisible` stays as the display rule (it now has nothing to hide)
- `Source/Velox/Bots/*` — **no change** expected (bots read the server registry); verify and report
- `Source/Velox/Tests/VeloxItemTests.cpp` (rows), `VeloxUITests.cpp` (row)
- `infra/ci/nettest.sh` — `ItemGateRun` (new, joins the default list): a `?SpectatorOnly=1` client logs `VeloxItemTimers n=` with entries after the bots take items; a second round as a playing client logs `n=0` throughout, including while dead (`serverKill` row from `DeathSpectateRun`)
- `docs/tasks/m6-online/notes/08-report.md`

## Relevant ADRs / references
- ADR 0007 §5.5 (the design, the baseline rule, entitlement), fact 8 (`PostNetReceive` before `CallRepNotifies`, `DataReplication.cpp:1604/1609` — verify and cite the line in the report)
- ADR 0004 §1–§2 (pickup model, `bAlwaysRelevant`, 10 Hz), §4 (item timers), §5 (spectators, `ServerRequestJoin`), C10 (the rule being replaced), C11 (dead ≠ spectator; `UVeloxHUDViewModel::bSpectator` reads `IsOnlyASpectator()`), amendment 3 (vii)
- `Source/Velox/Items/VeloxPickup.cpp:141–144, 256, 764–840` (today's replication and `OnRep`)

## Acceptance criteria
- [x] `AVeloxPickup` replicates `bAvailable` (`ReplicatedUsing=OnRep_Available`), `TakeCount` (`uint8`, `ReplicatedUsing=OnRep_TakeCount`, wraps), `LastTakenBy`, `Definition` (initial only) — and **not** `RespawnServerTime` (`GetLifetimeReplicatedProps` row in `Velox.Items.Replication`: the property is not in the rep layout). Push model marks on the server where the values change.
- [x] Baseline rule: `PreNetReceive()` clears `bSuppressNetEvents`; `PostNetReceive()` sets `bReceivedInitialState` and `bSuppressNetEvents` on the first call; the two `OnRep`s update `Seen*` and the placeholder/visibility **without** cues or `OnPickupTaken` while suppressed. Unit rows in a standalone fixture that calls the hooks in the engine's order: first bunch with `TakeCount 5, bAvailable false` → silent; later `TakeCount 6` → take event; `bAvailable true` → respawn event; a first bunch equal to defaults (never taken) followed by a take → the take **is** announced.
- [x] Owner prediction (`IsPredictedTaken`, ItemRun's predicted-take assertions) unchanged; the own-take sound/message still play from the prediction; `velox.bots.Watch` placeholder-take lines still appear (they now follow `TakeCount`).
- [x] `AVeloxPlayerState::ItemTimers` owner-only; the registry pushes the full set on entitlement gain (`PostLogin` as spectator, mode rule), on every take/respawn of a **major** item (ADR 0004 §4 list; minor items never), clears on `ServerRequestJoin` success and on `ResetLevel`. Entitlement: `Spectators` → `IsOnlyASpectator()`; `Everyone` → all; `None` → nobody. **A dead player (`NAME_Spectating` controller, `bOnlySpectator == false`) is not entitled** — unit row in the `DeathFlow` harness: kill the pawn → the PlayerState's `ItemTimers` stays empty through the spectate and after the respawn; a `?SpectatorOnly=1` fixture → populated.
- [x] HUD/scoreboard read the PlayerState array; `UVeloxHUDViewModel::bShowItemTimers` unchanged in meaning; the row from C11 (dead duellist sees no timers) still passes and now also asserts the array is empty.
- [x] `ItemGateRun` PASS ×3: spectator round `VeloxItemTimers n≥1` after the first major take, entries' `RespawnServerTime` within 0.1 s of the server's `VeloxPickupRespawn` log; player round `n=0` always, including the dead window; `ItemRun`, `BotRun --repeat 3`, `DeathSpectateRun` PASS unchanged; FT item map (`fts.sh`) green.
- [x] Bandwidth: the PlayerState's extra traffic ≤ 100 B per major-item event per spectator (reason from the struct size; no measurement required).
- [x] Build and all tests green; `docs/content-bootstrap.md` and the bootstrap script lose the deleted setting.

## Verification steps
1. `infra/ci/build.sh` → green (new rows listed); `infra/ci/fts.sh` → green.
2. Under the soak lock: `nettest.sh --scripts ItemGateRun --repeat 3`, `--scripts ItemRun,DeathSpectateRun --repeat 1`, `--scripts BotRun --repeat 3`.
3. `grep -n "RespawnServerTime" Source/Velox/UI/*.cpp Source/Velox/Items/VeloxPickup.cpp` → only server-side uses remain (list them).

## Report back
Standard format plus: the engine line proving the `PostNetReceive` → `CallRepNotifies` order, the final replicated property list of `AVeloxPickup`, the entitlement function, and the `ItemGateRun` log excerpts for both rounds.

## Notes / outcome
2026-11-06 (dev-hard), report: `docs/tasks/m6-online/notes/08-report.md`.
- Done: pickup replication split (no `RespawnServerTime` on the wire, push model), baseline rule, owner-only
  `AVeloxPlayerState::ItemTimers` + registry entitlement (dead player not entitled; ViewAPlayer/CanSpectate refused for
  dead players, architect F1.2), HUD reads the PlayerState, `IsStaleTake` / `PickupTakeEventMaxAgeSeconds` deleted.
  Unit rows `Velox.Items.Replication`, `Velox.GameMode.ItemTimerGating`, `Velox.UI.DeadIsNotSpectator` (+array empty).
  Editor build + 132/132 tests green. ItemGateRun spectator round PASS x3 (dedicated).
- Remaining: the ItemGateRun player round failed x3 for a harness reason (FFA Countdown->Live reset replaced the scripted
  pawn before the kill; all client lines n=0, all pushes remote=0). Fix committed but **unverified** (server forces Live
  1 s after start). Not run: ItemGateRun x3 with the fix, listen mode, ItemRun/DeathSpectateRun, BotRun x3, fts.sh.
2026-11-11 (dev-hard), finished on the head merged with origin/m6-online (02b, 05): Editor build + 134/134 tests green.
  ItemGateRun PASS 3/3 dedicated and 3/3 listen (both rounds; player round dead_lines=4 all_n0=1, spectator max_err_s
  <= 0.017), ItemRun + DeathSpectateRun PASS, BotRun x3 set PASS, fts.sh OK 8/8. Listen first failed on a harness name
  collision (host and client both log in as `Player`); `VeloxItemTimerEntitlement` now logs `local=` and the evaluator
  ignores the host's local rows. Details in the report.
