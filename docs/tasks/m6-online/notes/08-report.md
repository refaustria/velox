# Task m6-online/08: report (dev-hard, 2026-11-06, finished 2026-11-11). Done

## Status
- 2026-11-11 (on the merged head with origin/m6-online, incl. 02b and 05): Editor build + **134/134** tests green.
  **ItemGateRun PASS 3/3 dedicated and 3/3 listen** (plus 1/1 dedicated on the final binary), `ItemRun`,
  `DeathSpectateRun` PASS, `BotRun --repeat 3` set PASS, `fts.sh` OK 8/8.
- The forced-Live harness fix (server `velox.bots.After 1 velox.match.Phase Live`) made the player round reach its
  `serverKill`: dead_lines=4, all_n0=1 in every round.
- Listen mode first failed 0/3 for a **harness** reason (no leak): the listen host's own spectator PlayerState and the
  remote client both log in as `Player` (the login name is the LocalPlayer nickname; `?Name=` in the host's map URL or
  the client's connect URL does not reach it, verified: `Login request: ?Name=Player` with `127.0.0.1:7832?Name=GatePlayer`),
  so the evaluator read the host's entitled, local rows as the client's. Fix: `VeloxItemTimerEntitlement` now carries
  `local=` (owner is a local PlayerController) and the evaluator ignores `local=1` rows. The unused `host_names` is gone.
- Host load (foreign postgres loop): load1 1.0-2.0 at round starts (gate 2.0, waited <= 30 s); the loop was gone by
  the listen sets (lead), load1 0.7-2.1 during them, the remaining load is the other agent's UE runs and our own.
- Game/Server/Client targets: not built here (installed engine; `build.sh` skips Server/Client).

## Evidence 2026-11-11 (logs under the scratchpad `t08/`)
```
dedicated (gate-ded, x3):
  ItemGateSpectator  PASS  entitled=1 takes_pushed=16 respawns_matched=9 max_err_s=0.015 client_lines=95 max_n=7
  ItemGatePlayer     PASS  entitled=0 takes_pushed=7 client_lines=46 dead_lines=4 all_n0=1 view_refused=0
  ItemGateSpectator  PASS  entitled=1 takes_pushed=16 respawns_matched=9 max_err_s=0.015 client_lines=97 max_n=7
  ItemGatePlayer     PASS  entitled=0 takes_pushed=6 client_lines=47 dead_lines=4 all_n0=1 view_refused=0
  ItemGateSpectator  PASS  entitled=1 takes_pushed=16 respawns_matched=9 max_err_s=0.013 client_lines=96 max_n=7
  ItemGatePlayer     PASS  entitled=0 takes_pushed=8 client_lines=47 dead_lines=4 all_n0=1 view_refused=0
  Rounds passed: 3/3
listen (gate-listen3, x3, after the local= fix):
  ItemGateSpectator  PASS  entitled=1 takes_pushed=16 respawns_matched=9 max_err_s=0.015 client_lines=97 max_n=7
  ItemGatePlayer     PASS  entitled=0 takes_pushed=9 client_lines=47 dead_lines=4 all_n0=1 view_refused=0
  ItemGateSpectator  PASS  entitled=1 takes_pushed=16 respawns_matched=9 max_err_s=0.016 client_lines=97 max_n=7
  ItemGatePlayer     PASS  entitled=0 takes_pushed=6 client_lines=47 dead_lines=4 all_n0=1 view_refused=0
  ItemGateSpectator  PASS  entitled=1 takes_pushed=16 respawns_matched=9 max_err_s=0.014 client_lines=95 max_n=7
  ItemGatePlayer     PASS  entitled=0 takes_pushed=8 client_lines=46 dead_lines=4 all_n0=1 view_refused=0
  Rounds passed: 3/3
dedicated, final binary (gate-ded2, x1): Spectator PASS max_err_s=0.017, Player PASS dead_lines=4 all_n0=1
ItemRun PASS, DeathSpectateRun PASS (spectate=free); BotRun x3: PASS/PASS/PASS, set behaviour PASS, cost PASS
  (round 1 cost not judged: clean 4 + process-slow 16 of 20 windows; the set's cost band passed)
```
Listen player round, server (host rows local=1, the client's row local=0):
`VeloxItemTimerEntitlement player=Player entitled=1 only_spectator=1 local=1 rule=...Spectators n=7` (host)
`VeloxItemTimerEntitlement player=Player entitled=0 only_spectator=0 local=0 rule=...Spectators n=0` (client)
`VeloxItemTimerPush item=DA_Item_Powerup_Regen index=15 respawn_t=44.896 entitled=1 remote=0 to=[Player]` (host only)
Client in the dead window: `VeloxItemTimers n=0 entries=[] reason=tick player=Player state=Spectating only_spectator=0 pawn=0 t=41.602`.
Listen spectator round: `VeloxItemTimerPush item=DA_Item_Powerup_Regen index=15 respawn_t=72.121 entitled=2 remote=1 to=[Player,Player]`.

## History (2026-11-06)
- Code, unit rows, Editor build 132/132, spectator round PASS x3 (dedicated). The player round failed x3 (harness:
  FFA Countdown->Live `ResetLevel` replaced the scripted pawn before `serverKill`); stopped by the lead (disk).

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

## ItemGateRun evidence 2026-11-06 (before the forced-Live fix; superseded above)
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
- 2026-11-11: `VeloxItemTimerEntitlement` gained `local=` (log only) for the listen evaluator; player names are not
  unique on a listen host (host and client both `Player`). Anything keying server logs by player name in listen mode
  (other nettest evaluators, ADR 0007 listen tooling) has the same ambiguity; the PlayerState label could carry the
  PlayerId if the architect prefers that over a per-line flag.
