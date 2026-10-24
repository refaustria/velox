# Velox CI scripts

Local CI until GitHub Actions is available (2026-11-01); afterwards the same scripts are the pre-merge check. See ADR 0001 sections 4 and 5.

## Running

Linux: `infra/ci/build.sh`   macOS: `infra/ci/build-mac.sh`

Configure via environment or `infra/.env` (copy `infra/env.example`; the real environment wins over the file):
`UE_ROOT` (required; on Mac `/Users/Shared/Epic Games/UE_5.8` is probed if unset, on Linux `/opt/UnrealEngine-5.8`), `VELOX_PROJECT` (default `<repo>/Velox.uproject`), `VELOX_OUT` (default `Saved/CI`), `VELOX_CONFIG` (default `Development`).

macOS notes: needs `brew install coreutils` (macOS has no `timeout`; `build-mac.sh` uses `gtimeout`, or runs the tests without a time limit and warns), Xcode, Launcher-installed UE 5.8; Server/Client targets are always SKIPPED. First-run walkthrough for the human: `docs/playtests/m1-movement-checklist.md`.

Flags: `--config X`, `--targets a,b`, `--no-tests`, `--tests-only`, `--clean`, `--net-soak` (Linux only: runs `nettest.sh` after build and tests, logs in `<log dir>/nettest/`), `--dry-run`, `-h`.
`--dry-run` prints every command without running it and needs no engine, only a directory with `Engine/Build/BatchFiles` (and optionally `Engine/Build/InstalledBuild.txt`).

Logs: `Saved/CI/<timestamp>/` (`build-<Target>.log`, `tests.log`, `automation/index.json`). Paths are printed on failure. Exit code is non-zero on any build or test failure.

## What SKIPPED means

`VeloxServer` and `VeloxClient` need a source-built engine. If `Engine/Build/InstalledBuild.txt` exists (Epic Launcher build, prebuilt Linux zip) the scripts print `SKIPPED (installed engine cannot build Server/Client targets)` and continue with exit code 0. On Mac, Server/Client are always skipped. This is not a pass for those targets; it means they are unverified.

## Tests

Runs `UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Velox; Quit" -TestExit="Automation Test Queue Empty" -ReportExportPath=<dir>/automation -nullrhi -unattended -nopause -nosplash -log`, then fails on any test in `index.json` whose state is not `Success`, on a missing report, or on `Test Completed. Result={Fail` in the log. The editor exit code is not trusted alone.

Add tests: put `IMPLEMENT_SIMPLE_AUTOMATION_TEST` tests named `Velox.<Area>.<Name>` in `Source/Velox/Tests/*Test.cpp` (wrapped in `#if WITH_DEV_AUTOMATION_TESTS`). The `Velox` filter picks them up automatically.

## Net soak (`infra/ci/nettest.sh`)

`infra/ci/nettest.sh [--scripts a,b] [--port N] [--timeout S] [--repeat N [--keep-going]]` runs, per script (`StrafeRun`, `PadRun`, `KnockbackRun`, `KnockbackGroundRun`, `LatchRun`), a dedicated server (`UnrealEditor-Cmd Velox.uproject L_MovementTest -server -nullrhi`, 60 Hz via `NetServerMaxTickRate`) and one `-game -nullrhi` client (`t.MaxFPS 120`, `-VeloxScript=<name>`) with client-side packet simulation in both directions (`-PktLag=100 -PktLagVariance=20 -PktLoss=2 -PktIncomingLagMin=80 -PktIncomingLagMax=120 -PktIncomingLoss=2`, read from the command line by `UNetDriver::InitBase`). It waits for the client's `VeloxNetSoak:` line (timeout 120 s), kills both processes, applies the per-script criteria of `docs/tasks/m1-movement/05-debug-tooling-and-net-soak.md` to both logs and prints one summary row per script (`C`/`max`/`mean` over non-echo corrections, `echoes=E` separately, ADR 0002 A24; every `VeloxReplay` line must start at the move after the correction, A25; `gaps=N` counts the server's `VeloxMoveGap` lines, i.e. server moves > 30 ms that merged lost RPCs, A28(iv), and `gaps=0` over >= 1000 client moves fails as `loss simulation inactive`); exit code 1 if any script fails. `PadRun` (A28(iii)/A30(iii)): per pad exactly one client `VeloxJumpPad: ... src=live applied=1` line and one server line with equal move time stamp `ts` (a mismatch is reported as `launch one move apart (lost entry move)` when a `VeloxMoveGap` covers the entry `ts`, else `overlap ordering regression`); `src=replay applied=1` lines at most once per pad and replay, with the server's `ts`; `applied=0` (A19 teleport overlaps) ignored. `KnockbackGroundRun` (A30): echoes with response grace > 0 keep `svel` within 1 % of the intended correction's, every burst correction is an echo, and after the grace expiry `t_exp = ts_c + grace_c` every burst response follows `svel_c * exp(-GroundFriction * (ts - t_exp))` within 3 % while that is > 1.2 x StopSpeed (GroundFriction/StopSpeed from the `event=info` line, defaults 6 / 250 when absent). `--repeat N` runs the list N times (logs in `round-<k>/`), stopping at the first failing round unless `--keep-going`. Logs: `Saved/CI/<timestamp>-<pid>/nettest/<script>-{server,client}.log` and `<script>.result`. UDP 7777 must be free (`ss -ulnp`). `VELOX_NETTEST_EXEC="p.NetShowCorrections 1"` adds console commands to both processes (debugging). Fixed time step is deliberately not used: it runs frames as fast as possible and the wall-clock packet simulation would then add seconds of game time per 100 ms.

## Caveats

Not executed against a real engine: Unreal Engine is not installed on the dev VM. Only `--dry-run` and error paths have been verified. The `index.json` field names (`tests[].fullTestPath`, `state`) follow the UE automation report format and must be confirmed on first real run.
shellcheck: no suppressions.
