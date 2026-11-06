# Task m6-online/08: report (dev-hard, 2026-11-06). Partially done, stopped by the lead (disk below 10 GB)

## Status
- Done: code, unit rows, Editor build, the full automation run (132/132 green). ItemGateRun **spectator round PASS ×3 (dedicated)**.
- Not done: the **player round** of ItemGateRun failed ×3 for a harness reason, not a leak: FFA's Countdown→Live
  `ResetLevel` at +20 s replaces the scripted pawn, so `ItemGateRun.json` never reaches its `serverKill` row. Every client
  line of that round still read `n=0` (all_n0=1), and the server pushed 18–19 major-item timer changes with `remote=0`.
  A fix is committed **but has not been run**: the server forces Live 1 s after start (`velox.bots.After 1 velox.match.Phase Live`).
- Not run (the lead stopped all new UE steps): ItemGateRun ×3 with the fix, ItemGateRun in `--server-mode listen`,
  `ItemRun,DeathSpectateRun`, `BotRun --repeat 3`, `fts.sh`. The Game/Server/Client targets were not built here
  (on the installed engine, Server and Client are skipped by `build.sh`).

## Design
- `AVeloxPickup` replicates `bAvailable` (OnRep_Available), `TakeCount` (uint8, wraps, OnRep_TakeCount), `LastTakenBy`
  (OnRep_LastTakenBy), `Definition` (COND_InitialOnly) and **`ItemIndex`** (uint8, COND_InitialOnly, assigned by the
  server registry in `RegisterPickup`). All of them are push-based and marked dirty where they change (`CommitServerState`,
  `TryPickup`, `Reset`, `SetDefinition`, `SetItemIndex`). `RespawnServerTime` is a plain server-only member and no longer a UPROPERTY.
- Baseline rule: `PreNetReceive` clears `bSuppressNetEvents`. The first `PostNetReceive` sets `bReceivedInitialState`
  and `bSuppressNetEvents` and adopts `Seen*`. While the flag is set, the OnReps update `Seen*` and the placeholder silently.
  Engine order: `DataReplication.cpp:1604` `PostNetReceive();` then `:1609` `CallRepNotifies(true);`
  (`FObjectReplicator::PostReceivedBunch`). `PreNetReceive` is called at `:1035`/`:1144`. The unmapped-object path
  (`:2525`) calls Pre then Post as well. `IsStaleTake` and `PickupTakeEventMaxAgeSeconds` are deleted (C++, cvar, bootstrap script).
- `AVeloxPlayerState::ItemTimers : TArray<FVeloxItemTimerEntry{uint8 ItemIndex; float RespawnServerTime;}>` is
  COND_OwnerOnly and push-based, sorted by index and updated in place (one element per event). `GetItemTimer(Index)`.
  `OnRep_ItemTimers` logs `VeloxItemTimers n= entries=[...] reason=rep`.
- Entitlement: `VeloxItemTimerRules::IsEntitled(Rule, bOnlySpectator, bIsBot)`:
  ```
  if (bIsBot) return false;
  return Rule == Everyone || (Rule == Spectators && bOnlySpectator);
  ```
  It is evaluated on the server by `UVeloxItemRegistrySubsystem::IsEntitledToItemTimers(PS)`. A dead player
  (NAME_Spectating, `bOnlySpectator == false`) is not entitled.
- Registry (server): `PushTimersTo(PS)` sends the full major set or clears it, on `PostLogin` and on a successful
  `RequestJoin`. `HandleItemTimerChanged` runs on every timer change of a **major** pickup (take, respawn, initial
  schedule, reset) and is called from `CommitServerState`. `RefreshAllItemTimers` runs from `AVeloxGameMode::Reset`
  (the last step of `ResetLevel`) and from `RefreshModeRules`. `Logout` clears the leaving PlayerState's timers.
  Server logs: `VeloxItemTimerEntitlement`, and `VeloxItemTimerPush ... entitled= remote=` (remote = non-local owners).
  The listen host's spectator-only PlayerState is entitled but local, so nothing leaves the machine for it.
- ViewAPlayer (architect F1.2): `AVeloxGameMode::CanSpectate` returns true only for a spectator-only viewer and a
  non-spectator target that is not the viewer. `AVeloxPlayerController::ViewAPlayer` refuses non-spectators and logs
  `VeloxViewRefused`. The override is needed because the engine's fallback in `GetNextViewablePlayer` would otherwise
  retarget the view to the dead player's own PlayerState. The rule is world-free in `VeloxSpectate::MayViewOtherPlayers`.
- Clients: `GetTimeUntilRespawn()` reads the local PlayerState entry, and returns −1 when this machine has none.
  `UVeloxHUDViewModel::UpdateItemTimers` reads the PlayerState array. `ItemTimersVisible` is still the display rule.
  The scoreboard needs no change: it reads the view model's array.
- Bots: no change. Routing reads `GetTimeUntilRespawn()` on the server. `velox.bots.Watch` keys on placeholder
  visibility, which now follows `bAvailable`.
- Bandwidth: one event updates one array element: the array handle and element index (about 2–3 B) plus a 4 B float
  (the index is unchanged), roughly 10–30 B with bunch overhead per spectator. That is under the 100 B bound.
  The full set at entitlement gain is at most 23 × 5 B plus overhead, sent once.

## Remaining `RespawnServerTime` uses (verification step 3)
`VeloxPickup.cpp`: `GetTimeUntilRespawn` (the `HasAuthority()` branch), `CommitServerState` (written from `Timer`), and
`GetRespawnServerTime()` (server-only accessor). It is read on the server by `UVeloxItemRegistrySubsystem::BuildTimerSet`
and `HandleItemTimerChanged`, by the item cheats and by the FT. `UI/*.cpp` uses only `FVeloxItemTimerEntry::RespawnServerTime`.

## Tests
New: `Velox.Items.Replication` (rep layout, owner-only ItemTimers, entitlement table, baseline rule in engine order: first
bunch take 5/down → silent, then take 6 → one take, available → one respawn, default first bunch then a take → announced,
uint8 wrap), and `Velox.GameMode.ItemTimerGating` (player: empty through the take, the death camera and the respawn;
dead → ViewAPlayer refused, CanSpectate false, HUD hides; spectator: full set, the take's exact respawn time, HUD ≈35 s,
join clears). Extended: `Velox.UI.DeadIsNotSpectator` (the array is empty). Removed: the `IsStaleTake` rows.
`build.sh --targets VeloxEditor`: 132/132 Success. (The pre-existing `LIGHTING FAIL` audit line is informational.)

## ItemGateRun evidence (dedicated, ×3, logs under the scratchpad `t08/gate-ded`)
```
ItemGateSpectator  PASS  entitled=1 takes_pushed=16 respawns_matched=9 max_err_s=0.017 client_lines=95 max_n=7
ItemGateSpectator  PASS  entitled=1 takes_pushed=16 respawns_matched=9 max_err_s=0.016 client_lines=96 max_n=7
ItemGateSpectator  PASS  entitled=1 takes_pushed=15 respawns_matched=8 max_err_s=0.016 client_lines=92 max_n=7
ItemGatePlayer     FAIL  entitled=0 takes_pushed=19 client_lines=111 dead_lines=0 all_n0=1   (pawn replaced at Live; no kill)
```
Spectator client: `VeloxItemTimers n=7 entries=[DA_Item_Powerup_Regen#15@51.138,...,DA_Item_Powerup_Quad#22@43.607] reason=rep player=Player`.
Server: `VeloxItemTimerPush item=DA_Item_Powerup_Regen index=15 respawn_t=69.163 entitled=1 remote=1 to=[Player]`.
Player round server: `VeloxItemTimerEntitlement player=Player entitled=0 only_spectator=0 rule=...Spectators n=0`, and every push reads `remote=0`.

## Deviations / for the architect
- `ItemIndex` is a new replicated property (initial-only). The ADR names the index but not how a client maps it to a
  pickup, and registration order differs between server and client.
- Files outside the listed scope: `GameModes/VeloxPlayerController.h/.cpp` (the ViewAPlayer override),
  `GameModes/VeloxSpectateMath.h` (the rule), `Tests/VeloxDeathTests.cpp` (the DeathFlow-family harness row),
  `Content/Data/MoveScripts/ItemGateRun.json` (the player-round script). `docs/content-bootstrap.md` never mentioned the setting, so it is unchanged.
- `velox.items.LogTimers <s>` (non-shipping) lives in `VeloxPlayerState.cpp`.

## To finish
1. `nettest.sh --scripts ItemGateRun --repeat 3` (verify the forced-Live fix for the player round), then the same with `--server-mode listen`.
2. `--scripts ItemRun,DeathSpectateRun --repeat 1`, `--scripts BotRun --repeat 3`, `fts.sh`.
