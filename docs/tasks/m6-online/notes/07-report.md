# M6-07 report: server hardening (dev-hard, 2026-11-01)

Status: **partially done**. The code is in place, the Editor build is green, 127/127 automation tests pass, and the soaks below
were run. Still open: the Game Development + Shipping builds, KnockbackRun x10, a clean BotRun, and `RpcGateRun,PasswordRun --repeat
3` (each ran 2x, both PASS). The project paused (wind-down) before these were done.

## Final `_Validate` rules

| RPC | owner | rule |
|---|---|---|
| `ServerExecRPC` (engine) | `AVeloxPlayerController` | `IsLocalController()`, else `VeloxRpcRefused reason=remote` -> false |
| `ServerPause` (engine; kept by ADR 0007 F1.2) | `AVeloxPlayerController` | same as ServerExecRPC; plus `bPauseable=false` in the `AVeloxGameMode` constructor, so `AllowPausing` is false for every networked controller (standalone still pauses via `NM_Standalone`); pinned in `Velox.Net.NoCheatManagerForRemote` |
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


---

# Update 2026-11-06 (dev-hard): ADR 0007 amendment F1 checklist

Status: **partially done** (stopped by the lead: disk below 10 GB, no new UE steps). Branch `wip/m6-07-hardening`, merged
with `origin/m6-online` (01b, 02 part A, 04b; one trivial conflict in `nettest.sh` `analyze_round`: both gates kept).

## Done
- F1.1 (i): `MovementTimeDiscrepancyDriftAllowance=0.005` in `Config/DefaultGame.ini`.
- F1.2: `bPauseable=false` in the `AVeloxGameMode` constructor; `AllowPausing(Remote)==false` row in
  `Velox.Net.NoCheatManagerForRemote`.
- F1.3 (ii): `VeloxNet::ResolveServerPassword(CmdLine, HostOptions, EnvValue) -> {Password, Source}` (world-free, in
  `Net/VeloxNetCheats.h/.cpp`), used by `AVeloxGameMode::InitGame`; 4 new `Velox.Net.PasswordRule` rows. **Bug found by the new
  row and fixed:** `FParse::Value` skips whitespace after `=`, so an empty `-ServerPassword= -nullrhi` used to set the password
  to `-nullrhi`; the value is now parsed only when it starts right after the `=`. Also new: an empty `?ServerPassword=` falls
  through to the environment (before: source `url`, password off).
- F1.1 (ii): server branch of `velox.move.DebugTimeScaleLog`: `VeloxTimeScale role=server pawn= player= real_t= world_t= pos=
  speed2d=` per remote player's pawn, 1 Hz. Both branches now log `pos=` as `FVector::ToString` (`ToCompactString` drops zero
  components).
- F1.1 (iii): `SpeedHackRun` server gets `velox.move.DebugTimeScaleLog 1`; `analyze_round` prints `dx_server=`/`dx_client=`
  (10 s of real time from the first 1 Hz sample that moved by 1-600 cm; the cap skips the script's teleport).
- F1.3 (i): `run_password` starts the server with `VELOX_SERVER_PASSWORD` in its environment (exported only around the server
  start; `nettest.sh` unsets it globally so every other soak server is open), asserts `source=env`, and FAILs on
  `pw_cmdline_echo_lines > 0`. `Login request:`/`Join request:` echoes are counted in `pw_engine_echo_lines` but not judged.
- Pre-existing Game-target compile error fixed: `VeloxNetTests.cpp` used `TGuardValue<bool>(GIsEditor, ...)`, but `GIsEditor`
  is a `#define false` without `WITH_EDITORONLY_DATA`. Now behind a macro that matches CoreGlobals.h's condition.

## Verification
- Builds: Editor + Game Development (`infra/ci/build.sh`, Server/Client SKIPPED: installed engine) green; Game Shipping
  (`Build.sh Velox Linux Shipping`) Succeeded. The Game Development/Shipping outputs were deleted afterwards (disk).
- Automation: **131/131** pass (`Saved/CI/20261106-091046-2378293`).
- StrafeRun+PadRun x10: 10/10 PASS, 1 non-echo correction (2.62 cm), Σ strays 0, 0 `VeloxTimeDiscrepancy`.
- KnockbackRun x10: 10/10 PASS.
- RpcGateRun,PasswordRun --repeat 3: 3/3 each (`exec_dt=0.032`, `kill_dt=0.033`, `ConnectionLost`; `wrong=failed:Wrong_password
  spectator=failed:Wrong_password right=joined pw_cmdline_echo_lines=0 pw_engine_echo_lines=2`).
- SpeedHackRun (server-side, `docs/adr/evidence/0007-discrepancy/README.md`): honest 1998.3 cm; 1.3 on 1588.8 cm (**-20.5 %**,
  15 resolutions, client 1513.0 cm); 1.3 off 2591.7 cm (+29.7 %). Diagnostic run with `p.DebugTimeDiscrepancy 2`: 0.94 simulated
  / server time, plus a velocity reset at every payback (likely).

## Password source table
| source | precedence | in the server log? | use |
|---|---|---|---|
| `-ServerPassword=<s>` | 1 (empty value falls through) | yes (`LogInit: Command Line:`, CSV metadata, crash context) | developer convenience |
| `?ServerPassword=<s>` on the host URL | 2 (empty falls through) | yes (`LoadMap`/`Browse`) | developer convenience |
| `VELOX_SERVER_PASSWORD` (environment, systemd EnvironmentFile) | 3 | no (only the clients' `Login request:`/`Join request:` echo, per F1.3 (b)) | **production** |

## Remaining (not run: stopped by the lead)
1. **Full default list `--repeat 1`** with the new allowance, and a clean BotRun (or BotRun/BotDuel x3). Not run.
2. **Speed-hack criterion not met on the server side** (-20.5 %, not within +-5 %). Needs an architect ruling: either restate
   the criterion as "the hack gains nothing (server-side <= honest + 5 %)", which is met, or look for a knob. The cause is
   engine-side (resolution accounting plus the payback ticks at `MIN_TICK_TIME`); a fix would mean either `ResolutionRate` < 1,
   which F1.1 rejected, or a change in the movement component, which is out of scope here.
3. The `dx_*` window rule was committed after the three SpeedHackRun rounds. It was checked offline on their logs and then by
   the diagnostic round (nettest printed `dx_server=1493.2`). The honest / 1.3 on / 1.3 off rows were not re-run with the
   committed code.
4. **Heartbeat flag:** task 04b is merged. `VeloxServerHeartbeatSubsystem.cpp:242` has `Out.bPassword = false; // Task 07 sets it.`.
   It needs one line, `bPassword` from `AVeloxGameMode::HasServerPassword()`. Not done, because it could not be compiled
   (no new builds).
5. Carried over: menu password field (Core/, task 05).

# Update 2026-11-11 (dev-hard): ADR 0007 F1.4 checklist - task done

## Merge
`origin/m6-online` (02b: tag hash folded into the net version, StateTree disabled, nettest taghash compare; 05 server browser)
merged into `wip/m6-07-hardening`. Two text conflicts:
- `docs/adr/0007-online.md` Amendments: both sides added an "F1". Kept both, m6-online's list (F1 proxy stamps, F2 gate/B1/server
  path) first, then this task's "F1 - task 07 rulings" (F1.1-F1.4). **Numbering collision left for the architect:** two different
  "F1"s now stand in the ADR; task 07's is referenced as F1.1-F1.4 in code comments, nettest and this task, so it was not renamed.
- `docs/tasks/m6-online/06-deploy-systemd-firewall.md` `run-server.sh` criterion: took this branch's text (m6-online's plus the F1.3
  note that `VELOX_SERVER_PASSWORD` is never turned into `-ServerPassword=`).

## Done
- Heartbeat: `UVeloxServerHeartbeatSubsystem::BuildSnapshot` sets `Out.bPassword` from
  `World->GetAuthGameMode<AVeloxGameMode>()->HasServerPassword()` (false when the auth game mode is not a Velox one).
- Bug fixed in `infra/ci/nettest.sh` `run_rpc_gate`: commit `76e448a` had pasted `< 600.0)` (the SpeedHackRun window cap) into
  the RpcGateRun result lookup, a Python `SyntaxError`, so RpcGateRun produced no result. The 2026-11-06 RpcGateRun x3 rows were run
  before that edit. All three embedded analyzers now compile.

## Verification
- Builds: VeloxEditor + Velox (Game) Development via `infra/ci/build.sh --targets VeloxEditor,Velox`: Succeeded; automation
  **136/136** pass (`Saved/CI/20261111-094022-4151732`). Server/Client targets: SKIPPED (installed engine, as before). Map audit
  `RESULT PASS` (L_DM1 lighting NOT BAKED, pre-existing M5 human item).
- Speed hack, judged against **ADR 0007 F1.4**: "with detection on, `dx_server(1.3x) <= 1.05 x dx_server(honest)` and
  `>= 0.5 x dx_server(honest)` (expected ~0.8x), `dx_client(1.3x) <= dx_client(honest)`, >= 5 `VeloxTimeDiscrepancy` lines; with
  detection off `dx_server(1.3x) >= 1.2 x dx_server(honest)`".

  | client scale | detection | dx_server | / honest | dx_client | VeloxTimeDiscrepancy |
  |---|---|---|---|---|---|
  | 1.0 | on | 1997.1 cm | - | 2000.0 cm | 0 |
  | 1.3 | on | 1549.2 cm | 0.776 | 1562.4 cm | 14 |
  | 1.3 | off | 2604.4 cm | 1.304 | 2600.0 cm | 0 |

  **Met** (evidence README "2026-11-11 F1.4").
- Full default list `--repeat 1` (idle host, loadavg 1.62 at set start): 15/17 PASS; RpcGateRun no result (analyzer bug above,
  rerun PASS); BotRun cost FAIL on a slow-throughout round (frame_other_ref 2565 us). BotRun/BotDuel x3: BotRun set PASS (round 1
  D47 d); BotDuel cost PASS, behaviour PASS (JumpPad near-threshold, F2 item: 30/34, all 4 failures `reason=missed` on
  `VeloxNavLink_0`/`_4`). `VeloxTimeDiscrepancy` = 0 over every honest server log.

## Open
- ADR 0007 has two "F1" amendments after the merge (architect to renumber or annotate).
- BotRun's first round after a fresh server is slow throughout in both sets today (frame_other_ref ~2.5 ms vs ~0.9 ms); the set
  evaluation handles it (D47 d), nothing in this task touches bots. Same L_ItemTest JumpPad 0/4 item as F2 (i).
- Carried over: menu password field (Core/, task 05).
