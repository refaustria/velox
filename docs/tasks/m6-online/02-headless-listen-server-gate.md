# 02 — Headless listen server: nettest mode, host semantics, the soak gate

**Milestone:** M6 Online
**Agent:** dev-hard
**Status:** part A blocked (F1, needs a Movement/ ruling); part B todo
**Depends on:** ADR 0007 §1 (a) gate; part A (editor binary) can start now, part B needs task 01's server archive

## Goal
Prove, with the same evidence standard as every milestone before (the `nettest.sh` default list at 100 ms / 2 %, BotRun/BotDuel sets, host sidecar), that a **headless listen server with a spectator-only host** — the shape the cooked Game build can take, since `IsRunningDedicatedServer()` is false in a packaged Game build (ADR 0007 fact 2) — behaves like the dedicated server the soaks have always used. Part A runs the editor binary in that shape (`-game Map?listen?SpectatorOnly=1 -nullrhi`); part B runs the packaged Development server from task 01. Any behavioural difference is a finding, not something to paper over; if the gate fails for a non-bug reason, ADR 0007 §1 (b) (editor `-server`) becomes the deployed mode and this task records why.

## Scope — files/modules the agent may touch
- `infra/ci/nettest.sh` — `--server-mode dedicated|listen` (default dedicated), `--server-binary <path>` (packaged binary; implies listen), `--server-userdir`; the server launch line (around line 319), readiness check (a listen host logs `Game Engine Initialized` too; add the `Listening`/`UIpNetDriver Initialized` check), `t.MaxFPS 60` on the listen host (`-ExecCmds`), the host sidecar unchanged
- `infra/ci/lib.sh` — only if a helper is shared with task 01's smoke
- `Source/Velox/Core/VeloxServerKind.h` (new, header-only): `VeloxServerKind::IsHeadlessHost(const UObject* WorldContext)` = `GetNetMode() == NM_ListenServer` and the local player controller `IsOnlyASpectator()` and `-nullrhi` on the command line (read once); `IsServerLike(World)` = dedicated or headless host
- The 22 server-kind sites listed in ADR 0007 fact 9 — **only** where a measured cost justifies it (see acceptance), each change one line (`NM_DedicatedServer` → `VeloxServerKind::IsServerLike(...)`), and never the correctness paths of `UVeloxLagCompensationSubsystem`, `AVeloxJumpPad`, B12/B25 listen-host rules
- `Source/Velox/GameModes/VeloxGameModeMatch.cpp` / `VeloxMatchRules*` — only if the host counts as a human in `FVeloxMatchPlayer` rows (see acceptance); `Source/Velox/Tests/VeloxMatchRulesTest.cpp` for the row
- `docs/adr/evidence/0007-listen-gate/` (logs, stats.json, summaries), `docs/tasks/m6-online/notes/02-report.md`
(Anything not listed is out of scope — in particular `Movement/`, `Net/VeloxLagCompensationSubsystem.*`, weapons.)

## Relevant ADRs / references
- ADR 0007 §1 (a) and (b), facts 2, 6, 9; §5.6 (frame budget numbers)
- ADR 0004 §5 (spectators, `?SpectatorOnly=1`, `MaxActivePlayers`), C11 amendment 3 (ii) (spectator controllers are `AVeloxPlayerController`)
- ADR 0005 D46/D47 (host sidecar, window classes), `infra/ci/README.md` (nettest semantics), `infra/ci/bands/`
- STATUS q29 (VM slowdown under a 120 Hz client; the listen host must be pinned to 60)
- Memory: soak lock, `pgrep` match on the binary path, kill own UE processes

## Acceptance criteria
- [ ] `nettest.sh --server-mode listen` starts `UnrealEditor-Cmd <project> <map>?listen?game=…?SpectatorOnly=1?Name=Server -game -nullrhi -nosound -unattended -nopause -nosplash -log -port=… -ExecCmds="t.MaxFPS 60, <sexec>"`; `--server-binary <staged Velox>` starts that binary with the same URL and switches plus `-UserDir`; `--server-mode dedicated` is byte-identical to today's behaviour (diff of a dedicated run's command line against `main`).
- [ ] **Host semantics pinned by tests:** (a) the host's PlayerState is `bOnlySpectator`, never possesses a pawn, is not in `FVeloxMatchPlayer` rows (`Velox.GameMode.MatchRules` row: a spectator-only human does not block `CanStartCountdown`/`AllHumansReady` and does not count for `MinPlayersToStart`); (b) `AGameSession::AtCapacity` does not count the host toward `MaxPlayers` (a log assertion in the BotDuel run: 2 bots + spectator client with `MaxPlayers=2` still admits the client as spectator and refuses a third active login — add a 20 s `CapacityRun` row or verify by log); (c) the host's `UCheatManager` is absent (`AllowCheats` false on `NM_ListenServer`; log line).
- [ ] **Part A (editor binary, listen):** full default list (13 scripts) PASS at 100 ms / 2 % under the soak lock on an idle host, same bands as `main`; `BotRun --repeat 3` and `BotDuel --repeat 3` sets PASS with the D46 classification; `DeathSpectateRun` syncs/clamps unchanged; FireRun/RocketJumpRun correction counts within the dedicated baseline of the same day (run the dedicated default list once as the baseline, same host state).
- [ ] **Host cost:** the sidecar shows no `X` (cost) windows over the sets; `VeloxBotBudget mean_us` within the dedicated baseline + 20 %; the host process RSS (sampled from `/proc/<pid>/status` VmRSS by the sidecar — add the column) printed per round, expected ≤ 2.5 GB for the packaged Development server; frame deficit ≤ 5 % (the host runs at 60 fps: `frames=` per window ≈ 600).
- [ ] **Server-kind audit:** a table in the report of the 22 sites (file:line, what it skips on a dedicated server, cost on a headless host measured or reasoned, changed yes/no). Changes only where the measured frame time or RSS moves by ≥ 2 % — expected candidates: `UVeloxAssetManager` cosmetics preload, `UVeloxAudioSettings` asset loads, `AVeloxCharacter` view-model/weapon-mesh creation for *remote* pawns (keep skeletal poses ticking: the hitbox history reads bones). Every change has a unit row or a log assertion.
- [ ] **Part B (packaged server from task 01):** the default list and both sets PASS again with `--server-binary`; `LogNetVersion` of the packaged server equals the editor's (same `ProjectVersion`); start-up time to `Listening` printed (expected ≤ 10 s).
- [ ] If any script fails for a reason attributable to the listen topology and not fixable within scope: stop, record evidence, and the report states "gate FAILED — recommend ADR 0007 §1 (b)" with the mechanism.
- [ ] Evidence directory `docs/adr/evidence/0007-listen-gate/` with the set summaries, `*.stats.json`, host sidecars (≤ 15 MB; logs trimmed to the judged lines if larger), and `SUMMARY.md` (baseline vs listen vs packaged, one table).

## Verification steps
1. Baseline (dedicated, today's path): `flock -o <scratchpad>/soak.lock infra/ci/nettest.sh --repeat 1` → 13/13.
2. `flock -o … infra/ci/nettest.sh --server-mode listen --repeat 1` → 13/13; then `--scripts BotRun --repeat 3`, `--scripts BotDuel --repeat 3` → set PASS, no cost windows.
3. `infra/ci/build.sh` → tests green (new rows included).
4. After task 01: extract the server archive to the scratchpad; `flock -o … infra/ci/nettest.sh --server-mode listen --server-binary <dir>/Velox/Binaries/Linux/Velox --repeat 1` → 13/13; the two sets again.
5. `pgrep -f '^/opt/UnrealEngine-5.8/Engine/Binaries/Linux/UnrealEditor' ; pgrep -f 'Binaries/Linux/Velox '` → none left.

## Report back
Standard format plus: the baseline/listen/packaged comparison table (per-script PASS, correction sums, bot budget mean, RSS, frame deficit), the server-kind audit table, the exact readiness log line for a listen host, and the explicit verdict "gate PASSED (listen)" or "gate FAILED — §1 (b)".

## Notes / outcome
2026-11-01, dev-hard, part A: **gate BLOCKED, not passed.** F1: `UCharacterMovementComponent::OnRegister` forces Linear -> Exponential smoothing on `NM_ListenServer`, so `ReplicatedServerLastTransformUpdateTimeStamp` replicates as 0, clients get no proxy stamp, and FireRun rail shots lose the stamp hint (stamp=0/11 listen vs 11/11 dedicated). Proposed one-line fix (out of scope, Movement/): `bNetworkAlwaysReplicateTransformUpdateTimestamp = true;` in the `UVeloxMovementComponent` constructor. Done: `--server-mode/--server-binary/--server-userdir`, listen readiness check, sidecar `srv_rss_kb`, `VeloxServerKind.h`, 11 one-line server-kind site changes, analyzer listen checks, `Velox.GameMode.HostSemantics`, CapacityRun probe. Evidence: `docs/adr/evidence/0007-listen-gate/` (A0/A1/A2 sets, capacity). Full default list and the x3 sets are owed after the F1 ruling.
