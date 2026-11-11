# 07 — Server hardening: RPC gate, test-RPC cvar, password, name sanitation, speed-hack guard

**Milestone:** M6 Online
**Agent:** dev-hard
**Status:** done (2026-11-11, ADR 0007 F1.4 checklist complete)
**Depends on:** ADR 0007 §5.1–5.3; coordinate with task 09 (both touch `VeloxPlayerController.*`; dispatch 09 after this is merged)

## Goal
Close the surfaces a Development server exposes to arbitrary clients before the first public deploy: the engine's `ServerExecRPC` (executes any console command on any non-Shipping server — ADR 0007 fact 8), the project's test RPCs, unbounded names, no password option; and turn on the engine's movement time-discrepancy detection, which is the standard speed-hack guard and is off by default — accepted only if the movement soaks stay unchanged.

## Scope — files/modules the agent may touch
- `Source/Velox/GameModes/VeloxPlayerController.h/.cpp` — `ServerExecRPC_Validate` override (virtual in the UHT body, `PlayerController.generated.h:181`), `VeloxRpcRefused` logging
- `Source/Velox/Movement/VeloxScriptedInputComponent.h/.cpp` — `_Validate` of `ServerGiveTestHaste`, `ServerKillSelfTest`, `ServerApplyTestKnockback`, `ServerTeleportTestPawn` gain the cvar check (**no other change in `Movement/`**); `Source/Velox/Character/VeloxCharacter.*` (`ServerGiveTestHaste` if it is the RPC owner), `Source/Velox/Bots/*Cheats*.cpp` or wherever `ServerAddBot`/`ServerKickBot` live — the same cvar check in `_Validate`
- `Source/Velox/Net/VeloxNetCheats.cpp` (new): `velox.net.AllowTestRpcs` (int, default 0, `ECVF_Cheat` is wrong here — it must be settable on a Development server via `-ExecCmds`; use plain `ECVF_Default`, compiled out in Shipping) and the shared `VeloxNet::TestRpcsAllowed()` helper
- `Source/Velox/GameModes/VeloxGameMode.h/.cpp`, `VeloxGameModeMatch.cpp` — `PreLogin` password check, `InitNewPlayer` name sanitation, `-ServerPassword=` parsing in `InitGame`; `AVeloxGameMode::AllowCheats` **not** overridden (pin by test)
- `Source/Velox/Net/VeloxNames.h` (new, header-only, world-free): `VeloxNames::Sanitize(const FString& In, int32 MaxLen) → FString`, `VeloxNames::Dedupe(Name, ExistingNames) → FString`
- `Source/Velox/Net/VeloxServerStatus.h` (task 04) — set `password` true when a password is configured (one line)
- `Source/Velox/UI/VeloxMainMenuWidget.*` / `VeloxServerBrowserWidget.*` (task 05) — pass `?Password=` when the field is non-empty (if task 05 already did, nothing)
- `Config/DefaultGame.ini` — `[/Script/Engine.GameNetworkManager]` discrepancy settings per ADR §5.3
- `Source/Velox/Tests/VeloxNetTests.cpp` (new): `Velox.Net.NameSanitize`, `Velox.Net.PasswordRule`, `Velox.Net.NoCheatManagerForRemote`
- `infra/ci/nettest.sh` — the server gets `-ExecCmds="velox.net.AllowTestRpcs 1, …"`; new short scripts `RpcGateRun` (client sends `ServerExec stat fps` and one test RPC → must be disconnected: client log `NetworkFailure`/`ConnectionLost`, server log `VeloxRpcRefused` or the engine's `RPC_ValidateFailed`), `PasswordRun` (wrong password → `PreLogin` failure text in the client log; right password → joins); both join the default list
- `Content/Data/MoveScripts/RpcGateRun.json`, `PasswordRun.json` if the scripted-input runner needs rows (a `serverExec` test row is acceptable in the scripted input component **only** as a non-shipping client-side console call)
- `docs/tasks/m6-online/notes/07-report.md`

## Relevant ADRs / references
- ADR 0007 §5.1, §5.2, §5.3, fact 8 (engine lines), §5.4 (task 09 consumes the refused/discrepancy counters — expose a simple `OnRpcRefused`/counter hook on the PC)
- ADR 0002 A24–A34 (correction bands the discrepancy soak must keep), `infra/ci/bands/`
- ADR 0004 §5 (`PostLogin`, `?SpectatorOnly`), §7 (`VeloxMenuMath::NormalizeConnectAddress`, failure texts)
- Engine: `PlayerController.cpp:1874–1896`, `GameModeBase.cpp:1411`, `GameNetworkManager.cpp:47–53`, `BaseGame.ini:38–43`

## Acceptance criteria
- [x] `AVeloxPlayerController::ServerExecRPC_Validate` returns `false` unless `IsLocalController()`; the refusal logs `VeloxRpcRefused controller= rpc=ServerExecRPC` (rate-limited) and the engine disconnects the sender (verified in `RpcGateRun`: client gone within 2 s, server `PostLogin`→`Logout` with the engine's validation-failure reason). A unit row cannot exercise the network kick; the nettest row is the proof.
- [x] Every test RPC's `_Validate` returns `false` when `velox.net.AllowTestRpcs == 0` (default) or in Shipping; existing argument validation stays; `nettest.sh` sets the cvar on its servers and every default-list script still passes; `RpcGateRun` (cvar unset for that round) shows the disconnect for `ServerKillSelfTest`.
- [x] `Velox.Net.NoCheatManagerForRemote`: `AVeloxGameMode::AllowCheats` is the engine's (reflection: not overridden), and a remote-PC fixture in the `DeathFlow` harness has `CheatManager == nullptr`.
- [x] Password (done except the heartbeat flag): `-ServerPassword=<s>` (or `?ServerPassword=` on the host URL — accept both, command line wins; **`VELOX_SERVER_PASSWORD` in the environment is the production path and the one `PasswordRun` exercises, ADR 0007 F1.3**; an empty value at any level falls through to the next, the password is off only when all three are empty — F1.4); `PreLogin` rejects with `ErrorMessage = "Wrong password"` (string from `UVeloxUISettings`) when the option is set and `?Password=` differs (length check, then constant-time compare); spectators also need it; the heartbeat's `password` is true (`VeloxServerHeartbeatSubsystem.cpp:242` ← `AVeloxGameMode::HasServerPassword()`, done 2026-11-11). `Velox.Net.PasswordRule` rows: unset → anything passes; set → equal passes, missing/other fails; resolver rows cmdline > url > env, empty falls through (done).
- [x] Names: `VeloxNames::Sanitize` strips control/format/bidi characters (`U+0000–001F`, `U+007F–009F`, `U+200B–200F`, `U+202A–202E`, `U+2066–2069`), collapses whitespace, trims, clamps to `UVeloxOnlineSettings::MaxPlayerNameLength` (24) by code points, replaces empty with `Player`; `Dedupe` appends ` (2)`, ` (3)`… against the current PlayerStates; applied in `InitNewPlayer` before the engine's `ChangeName`. Bots are untouched (Latin names, ADR 0005). Unit rows for each rule incl. an RTL-override attack string and a 200-char name.
- [x] Discrepancy detection on (StrafeRun+PadRun x10, KnockbackRun x10, RpcGateRun/PasswordRun x3 done with `DriftAllowance=0.005`; **open: the full default list `--repeat 1`**) per ADR §5.3: the full default list (17 scripts) PASS at 100 ms / 2 % with StrafeRun+PadRun ×10 Σ strays ≤ 2.5 cm unchanged from the 2026-11-01 "on" set, KnockbackRun 10/10, RocketJumpRun 0 non-echo corrections; `VeloxTimeDiscrepancy` lines = 0 for honest clients over the set (and over BotRun ×3 / BotDuel ×3 if BotRun shows cost windows again). If the set degrades: widen once to ±0.5 s, re-run; if it still degrades, leave detection **off**, keep the config lines commented with the evidence path, and say so in the report (ADR amendment by the lead).
- [x] A speed-hack proxy row (`SpeedHackRun`, report-only, not in the default list; `velox.move.DebugTimeScale 1.3` client-side only, the server branch of `DebugTimeScaleLog` and the `dx_server`/`dx_client` notes are done) — **criterion per ADR 0007 F1.4 (replaces F1.1's ±5 %)**: with detection on, `dx_server(1.3×) ≤ 1.05 × dx_server(honest)` and `≥ 0.5 × dx_server(honest)` (expected ≈ 0.8×), `dx_client(1.3×) ≤ dx_client(honest)`, ≥ 5 `VeloxTimeDiscrepancy` lines; with detection off `dx_server(1.3×) ≥ 1.2 × dx_server(honest)`. The 2026-11-06 rows meet it (0.795 / 0.766 / 1.297, 15 lines) but were analysed offline; **open: re-run honest / 1.3 on / 1.3 off once each with the committed analyzer** and append the rows to `docs/adr/evidence/0007-discrepancy/README.md` under "2026-11-11 F1.4".
- [x] Build green (Editor, Game Development, Game Shipping once — the cvar and the test RPC bodies compile out), all tests green.

## Verification steps
1. `infra/ci/build.sh` → green; `Build.sh Velox Linux Shipping -Project=…` → Succeeded (then delete the Shipping outputs).
2. Baseline under the soak lock: `nettest.sh --scripts StrafeRun,PadRun --repeat 10` with detection off (temporarily `-ini:Game:[/Script/Engine.GameNetworkManager]:bMovementTimeDiscrepancyDetection=False` on the server) → Σ strays; then the same with the committed config; then the full default list `--repeat 1`.
3. `nettest.sh --scripts RpcGateRun,PasswordRun --repeat 3` → 3/3 each.
4. The speed-hack proxy row twice (detection on/off); paste displacement numbers.

## Report back
Standard format plus: the Σ-stray and correction comparison (off vs on), the discrepancy log line format, the exact engine disconnect reason text seen by the client, and the list of RPCs with their final `_Validate` rules.

## Notes / outcome
2026-11-01 (dev-hard): the code is complete. Editor build green, 127/127 automation tests pass (incl. Velox.Net.NameSanitize,
PasswordRule, NoCheatManagerForRemote, NameApply). RpcGateRun and PasswordRun PASS (2x each). StrafeRun+PadRun x10 off/on: no
degradation. Full default list, one round: 16/17 PASS (BotRun failed on cost windows only). Speed-hack proxy: off +30 %, on -20 %
(not within 5 %; needs a ruling). Details, the RPC table and open items: `notes/07-report.md`, `docs/adr/evidence/0007-discrepancy/`.
**Still open:** the Game Development and Shipping builds; KnockbackRun x10; a clean BotRun / `--repeat 3` set;
`RpcGateRun,PasswordRun --repeat 3`; the menu password field (Core/, out of scope); the heartbeat flag (waits for task 04).

**Rulings (architect, 2026-11-06): ADR 0007 amendment F1** (`docs/adr/0007-online.md` §"Amendments"). F1.1 speed hack: config kept
(`ResolutionRate` 1.0), `MovementTimeDiscrepancyDriftAllowance` 0.0 → 0.005, criterion restated to the server-side displacement
(the −20 % was the cheater's own client view, not the guard's output). F1.2 `ServerPause_Validate`: keep, plus `bPauseable=false`
in the `AVeloxGameMode` constructor and an `AllowPausing(Remote)==false` assertion. F1.3 password: `VELOX_SERVER_PASSWORD` in the
systemd EnvironmentFile is the only production path (same name in the env file and in the game; task 06 must not emit
`-ServerPassword=`); no log redaction in code for M6; `PasswordRun` switches to the env path and fails on any command-line echo.
Remaining checklist for the implementer:
1. `DefaultGame.ini` `MovementTimeDiscrepancyDriftAllowance=0.005`; `bPauseable=false` in the `AVeloxGameMode` constructor; `AllowPausing` row in `Velox.Net.NoCheatManagerForRemote`.
2. `VeloxNet::ResolveServerPassword(...)` world-free + 4 `Velox.Net.PasswordRule` rows (cmdline > url > env; empty cmdline falls through).
3. `velox.move.DebugTimeScaleLog` server branch (`VeloxTimeScale role=server ...` per remote pawn, 1 Hz); `SpeedHackRun` server `-ExecCmds` + `dx_server`/`dx_client` notes.
4. `run_password`: server started with `VELOX_SERVER_PASSWORD` in its environment, assert `source=env`, FAIL on `pw_cmdline_echo_lines > 0`.
5. Builds: Editor, Game Development, Game Shipping once (cvar and test-RPC bodies compile out); all automation tests green.
6. Soaks under the lock: StrafeRun+PadRun ×10 (new allowance, Σ strays and corrections vs the 2026-11-01 "on" set), KnockbackRun ×10, full default list `--repeat 1` with a clean BotRun (or BotRun/BotDuel ×3 if the first round shows cost windows again), `RpcGateRun,PasswordRun --repeat 3`, SpeedHackRun honest / 1.3 on / 1.3 off once each; append the server-side table to `docs/adr/evidence/0007-discrepancy/README.md`.
7. `notes/07-report.md`: final `_Validate` table unchanged, add `ServerPause` + `bPauseable`, the password source table, the server-side speed-hack numbers; tick the criteria above.

2026-11-06 (dev-hard): F1 checklist. Done: items 1-4 (DriftAllowance 0.005; `bPauseable=false` + AllowPausing row;
`VeloxNet::ResolveServerPassword` + 4 PasswordRule rows, which also fixed an empty `-ServerPassword=` swallowing the next switch;
server-side `DebugTimeScaleLog` + `dx_server`/`dx_client`; PasswordRun on the env path with `source=env` and a
`pw_cmdline_echo_lines` FAIL rule) and item 5 (Editor, Game Development and Game Shipping builds green; 131/131 tests; also fixed
a Game-target compile error in `VeloxNetTests.cpp`). Item 6, partly: StrafeRun+PadRun x10 10/10 (1 correction 2.62 cm, Σ strays 0),
KnockbackRun x10 10/10, RpcGateRun,PasswordRun x3 3/3, SpeedHackRun honest/on/off run plus one diagnostic round.
**Remaining:** the full default list `--repeat 1` (+ BotRun/BotDuel x3 if needed); the heartbeat `bPassword` line
(`VeloxServerHeartbeatSubsystem.cpp:242`); an architect ruling on the server-side speed-hack result (-20.5 % vs +-5 %; cause
analysed in `docs/adr/evidence/0007-discrepancy/README.md`); re-running the three SpeedHackRun rows with the committed window rule
(checked offline). Details: `notes/07-report.md` (update 2026-11-06).

**Ruling (architect, 2026-11-11): ADR 0007 F1.4.** Both server-side losses are verified in the engine source: (1) during resolution the
payback per server tick is bounded by `min(client delta delivered this tick + carry, ServerDelta)` and the carry is discarded at every
first move of a tick (`CharacterMovementComponent.cpp:9588–9598`), so server time in ticks that received no move (100 ms jitter, 2 % loss)
is neither simulated nor credited (0.95 s of 15.41 s, ≈ −6 %); (2) with `ResolutionRate` 1.0 every payback move runs at `MIN_TICK_TIME`,
the 2×10⁻⁴ cm step is below `MoveComponentImpl`'s minimum sweep distance (`PrimitiveComponent.cpp:3283`), and PhysWalking's "reflect
actual move" sets `Velocity = 0` (`:5873`; Velox's `RestoreGroundEndVelocity` keeps a clipped velocity), so the server pawn restarts from
rest after each of the 15 resolutions (≈ −14 %). Both need `bResolvingTimeDiscrepancy`, which honest clients never set. **No fix**
(`ResolutionRate` < 1 stays rejected — it removes (2) but doubles the window of (1); a `Movement/` change benefits only cheaters and
needs its own task); the criterion is restated to "no gain" (above). Empty `?ServerPassword=` falling through to the environment:
accepted (uniform rule, production has no URL/cmdline option). Remaining checklist for the implementer:
1. SpeedHackRun honest / 1.3 on / 1.3 off once each with the committed analyzer → rows under "2026-11-11 F1.4" in the evidence README, judged against F1.4.
2. Full default list `--repeat 1` (BotRun/BotDuel ×3 if BotRun shows cost windows), 0 `VeloxTimeDiscrepancy` lines.
3. Heartbeat flag: `Out.bPassword = GameMode->HasServerPassword()` at `VeloxServerHeartbeatSubsystem.cpp:242`; Editor + Game Development build, all tests green.
4. Tick the three open criteria above, finish `notes/07-report.md` (speed-hack section: quote F1.4's criterion and the new rows), mark the task done.

2026-11-11 (dev-hard): F1.4 checklist done. Merged `origin/m6-online` (02b, 05; ADR 0007 Amendments and task 06 conflicts, see
`notes/07-report.md`). (3) Heartbeat `Out.bPassword` from `AVeloxGameMode::HasServerPassword()`; Editor + Game Development green,
136/136 tests. (1) SpeedHackRun honest / 1.3 on / 1.3 off: dx_server 1997.1 / 1549.2 (0.776x, 14 lines, client 1562.4 <= 2000.0) /
2604.4 (1.304x): F1.4 met, rows in `docs/adr/evidence/0007-discrepancy/README.md` "2026-11-11 F1.4". (2) Full default list
`--repeat 1`: 15/17 PASS; RpcGateRun aborted on an analyzer syntax error introduced in 76e448a (fixed, rerun PASS); BotRun cost
FAIL on a slow-throughout round (frame_other_ref 2565 us), so BotRun/BotDuel x3 was run: BotRun set PASS (round 1 D47 d),
BotDuel behaviour PASS (JumpPad near-threshold, F2 item: 30/34, 4x `reason=missed` on VeloxNavLink_0/_4), cost PASS.
`VeloxTimeDiscrepancy` lines: 0 over every honest server log (17 + 1 + 6).
