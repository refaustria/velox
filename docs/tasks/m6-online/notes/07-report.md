# M6-07 report: server hardening (dev-hard, 2026-11-01)

Status: **partially done**. The code is in place, the Editor build is green, 127/127 automation tests pass, and the soaks below
were run. Still open: the Game Development + Shipping builds, KnockbackRun x10, a clean BotRun, and `RpcGateRun,PasswordRun --repeat
3` (each ran 2x, both PASS). The project paused (wind-down) before these were done.

## Final `_Validate` rules

| RPC | owner | rule |
|---|---|---|
| `ServerExecRPC` (engine) | `AVeloxPlayerController` | `IsLocalController()`, else `VeloxRpcRefused reason=remote` -> false |
| `ServerPause` (engine; not in the ADR, added) | `AVeloxPlayerController` | same as ServerExecRPC |
| `ServerAddBot`, `ServerKickBot` (now `WithValidation`) | `AVeloxPlayerController` | `IsLocalController() \|\| VeloxNet::ValidateTestRpc` (the args and rate checks stay in `AcceptBotCommand`) |
| `ServerApplyTestKnockback` | `UVeloxScriptedInputComponent` | Shipping false; `ValidateTestRpc` && the existing impulse bound |
| `ServerGiveTestHaste` | same | Shipping false; `ValidateTestRpc` && `0 < s <= MaxTestHasteSeconds` |
| `ServerKillSelfTest`, `ServerTeleportTestPawn` | same | Shipping false; `ValidateTestRpc` |
| `ServerSetReady`, `ServerRequestJoin`, `ServerRequestRespawn` | PC | unchanged (gameplay) |

`ValidateTestRpc` is true only when `velox.net.AllowTestRpcs != 0`. The cvar is `ECVF_Default`, it does not exist in Shipping, and
in Shipping the check is always false. A refusal walks the owner chain to the PC, which counts it (`GetNumRpcRefused`), broadcasts
`OnRpcRefused`, and logs at most one line per `RpcRefusedLogIntervalSeconds` (default 1 s, from config). `AVeloxGameMode::AllowCheats`
is not overridden; a `static_assert` in `VeloxNetTests.cpp` pins that.

## Engine disconnect (RpcGateRun)

- Server: `VeloxRpcRefused controller=VeloxPlayerController_0 player=Player rpc=ServerExecRPC reason=remote count=1 suppressed=0`,
  then `LogRep: Error: ReceivedRPC: RPC_GetLastFailedReason: ServerExecRPC_Validate` and `UNetConnection::Close`.
- Client: `ConnectionLost "Your connection to the host has been lost."` arrives 0.03-0.05 s after the send.
- The kill probe gives the same result (`reason=test_rpcs_off`), and the server never runs `kill_server`.

## Password and names

- **Password source:** `-ServerPassword=` wins, then `?ServerPassword=` on the URL, then the env var `VELOX_SERVER_PASSWORD` (my
  addition). The server logs only `VeloxNet password=on|off source=`.
- **PreLogin:** a wrong or missing `?Password=` is rejected with `UVeloxUISettings::WrongPasswordMessage` ("Wrong password"). This
  covers spectators too. The comparison checks length first, then compares in constant time.
- **What the client sees:** `PendingConnectionFailure: Wrong password`.
- **Names:** sanitation (`VeloxNames.h`) runs in an `AVeloxGameMode::ChangeName` override, so the engine's `ServerChangeName`
  renames are covered as well. `InitNewPlayer` re-applies the raw `?Name=` after Super, because the engine cuts names to 20 UTF-16
  units before its own `ChangeName`.
- **Bots:** names pass through unchanged (they are AI controllers).

## Discrepancy and speed hack

See `docs/adr/evidence/0007-discrepancy/README.md`. Detection was already on since M1. Off vs on: no degradation. With detection on,
the speed hack is 20 % *slower* than an honest client (not within 5 %); with it off, the hack is 30 % faster.

## Open issues / for the lead and architect

1. **Speed-hack penalty:** the acceptance's "within 5 %" is not met. The engine overcorrects (-20 %). Decide whether to keep it or
   add `MovementTimeDiscrepancyResolutionRate` < 1 (an ADR amendment).
2. **Password in the server log:** the engine logs the password in clear: the command line (`LogInit: Command Line`,
   `LogCsvProfiler` metadata) and every client's `LogNet: Login request: ?...Password=...`. Task 06 should pass it as
   `VELOX_SERVER_PASSWORD` in the EnvironmentFile, not as `-ServerPassword=`. The login-URL echo can only be avoided by raising
   `LogNet` above Log level.
3. **Menu password field:** not built. `UVeloxGameInstance::ConnectToServer` / `NormalizeConnectAddress` reject `?`, and Core/ is
   outside this task's scope. Needed for task 05.
4. **Heartbeat flag:** `VeloxServerStatus.h` and `UVeloxOnlineSettings` (task 04) do not exist yet. The heartbeat should read
   `AVeloxGameMode::HasServerPassword()`. `MaxPlayerNameLength=24` lives on `AVeloxGameMode` (from config) until task 04 owns it.
5. **Name with no `?Name=`:** a client without one is now "Player", and a second one "Player (2)", instead of the engine's
   "Player<id>". A name with a space breaks the `\S+` log parsing if multi-human soaks ever key on names.
6. **New nettest knobs:** `VELOX_NETTEST_SERVER_ARGS`, `VELOX_TIME_SCALE`, and `SpeedHackRun` (not in the default list). The
   default list is now 17 scripts.
