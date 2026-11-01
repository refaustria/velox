# 07 — Server hardening: RPC gate, test-RPC cvar, password, name sanitation, speed-hack guard

**Milestone:** M6 Online
**Agent:** dev-hard
**Status:** todo
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
- [ ] `AVeloxPlayerController::ServerExecRPC_Validate` returns `false` unless `IsLocalController()`; the refusal logs `VeloxRpcRefused controller= rpc=ServerExecRPC` (rate-limited) and the engine disconnects the sender (verified in `RpcGateRun`: client gone within 2 s, server `PostLogin`→`Logout` with the engine's validation-failure reason). A unit row cannot exercise the network kick; the nettest row is the proof.
- [ ] Every test RPC's `_Validate` returns `false` when `velox.net.AllowTestRpcs == 0` (default) or in Shipping; existing argument validation stays; `nettest.sh` sets the cvar on its servers and every default-list script still passes; `RpcGateRun` (cvar unset for that round) shows the disconnect for `ServerKillSelfTest`.
- [ ] `Velox.Net.NoCheatManagerForRemote`: `AVeloxGameMode::AllowCheats` is the engine's (reflection: not overridden), and a remote-PC fixture in the `DeathFlow` harness has `CheatManager == nullptr`.
- [ ] Password: `-ServerPassword=<s>` (or `?ServerPassword=` on the host URL — accept both, command line wins); `PreLogin` rejects with `ErrorMessage = "Wrong password"` (string from `UVeloxUISettings`) when the option is set and `?Password=` differs (`FCString::Strcmp` on equal-length strings only after a length check — constant time is nice-to-have, note it); spectators also need it; the heartbeat's `password` is true. `Velox.Net.PasswordRule` rows: unset → anything passes; set → equal passes, missing/other fails.
- [ ] Names: `VeloxNames::Sanitize` strips control/format/bidi characters (`U+0000–001F`, `U+007F–009F`, `U+200B–200F`, `U+202A–202E`, `U+2066–2069`), collapses whitespace, trims, clamps to `UVeloxOnlineSettings::MaxPlayerNameLength` (24) by code points, replaces empty with `Player`; `Dedupe` appends ` (2)`, ` (3)`… against the current PlayerStates; applied in `InitNewPlayer` before the engine's `ChangeName`. Bots are untouched (Latin names, ADR 0005). Unit rows for each rule incl. an RTL-override attack string and a 200-char name.
- [ ] Discrepancy detection on per ADR §5.3; the **full default list** (13 + the two new scripts) PASS at 100 ms / 2 % with StrafeRun+PadRun ×10 Σ strays ≤ 2.5 cm unchanged from the day's baseline (run the baseline first with detection off, same host state), KnockbackRun 10/10, RocketJumpRun 0 non-echo corrections; the engine's resolution log (`LogNetPlayerMovement` discrepancy lines) count = 0 for honest clients over the set. If the set degrades: widen once to ±0.5 s, re-run; if it still degrades, leave detection **off**, keep the config lines commented with the evidence path, and say so in the report (ADR amendment by the lead).
- [ ] A speed-hack proxy row: `velox.move.DebugTimeScale 1.3` (new, non-shipping, **client-side only**, scales the client's move `DeltaTime` stamps — the smallest possible knob, in `VeloxScriptedInputComponent` or a cheat file, *not* in the movement component) → with detection on, the server logs resolution lines and the client's displacement over 10 s matches the honest client within 5 %; with detection off, it gains ≥ 20 %. Report-only evidence (`docs/adr/evidence/0007-discrepancy/`), the row is not in the default list.
- [ ] Build green (Editor, Game Development, Game Shipping once — the cvar and the test RPC bodies compile out), all tests green.

## Verification steps
1. `infra/ci/build.sh` → green; `Build.sh Velox Linux Shipping -Project=…` → Succeeded (then delete the Shipping outputs).
2. Baseline under the soak lock: `nettest.sh --scripts StrafeRun,PadRun --repeat 10` with detection off (temporarily `-ini:Game:[/Script/Engine.GameNetworkManager]:bMovementTimeDiscrepancyDetection=False` on the server) → Σ strays; then the same with the committed config; then the full default list `--repeat 1`.
3. `nettest.sh --scripts RpcGateRun,PasswordRun --repeat 3` → 3/3 each.
4. The speed-hack proxy row twice (detection on/off); paste displacement numbers.

## Report back
Standard format plus: the Σ-stray and correction comparison (off vs on), the discrepancy log line format, the exact engine disconnect reason text seen by the client, and the list of RPCs with their final `_Validate` rules.

## Notes / outcome
(Filled by the agent or the lead after completion.)
