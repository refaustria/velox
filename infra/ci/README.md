# Velox CI scripts

Local CI until GitHub Actions is available (2026-11-01); afterwards the same scripts are the pre-merge check. See ADR 0001 sections 4 and 5.

## Running

Linux: `infra/ci/build.sh`   macOS: `infra/ci/build-mac.sh`

Configure via environment or `infra/.env` (copy `infra/env.example`; the real environment wins over the file):
`UE_ROOT` (required; on Mac `/Users/Shared/Epic Games/UE_5.8` is probed if unset, on Linux `/opt/UnrealEngine-5.8`), `VELOX_PROJECT` (default `<repo>/Velox.uproject`), `VELOX_OUT` (default `Saved/CI`), `VELOX_CONFIG` (default `Development`).

Flags: `--config X`, `--targets a,b`, `--no-tests`, `--tests-only`, `--clean`, `--dry-run`, `-h`.
`--dry-run` prints every command without running it and needs no engine, only a directory with `Engine/Build/BatchFiles` (and optionally `Engine/Build/InstalledBuild.txt`).

Logs: `Saved/CI/<timestamp>/` (`build-<Target>.log`, `tests.log`, `automation/index.json`). Paths are printed on failure. Exit code is non-zero on any build or test failure.

## What SKIPPED means

`VeloxServer` and `VeloxClient` need a source-built engine. If `Engine/Build/InstalledBuild.txt` exists (Epic Launcher build, prebuilt Linux zip) the scripts print `SKIPPED (installed engine cannot build Server/Client targets)` and continue with exit code 0. On Mac, Server/Client are always skipped. This is not a pass for those targets; it means they are unverified.

## Tests

Runs `UnrealEditor-Cmd <project> -ExecCmds="Automation RunTests Velox; Quit" -TestExit="Automation Test Queue Empty" -ReportExportPath=<dir>/automation -nullrhi -unattended -nopause -nosplash -log`, then fails on any test in `index.json` whose state is not `Success`, on a missing report, or on `Test Completed. Result={Fail` in the log. The editor exit code is not trusted alone.

Add tests: put `IMPLEMENT_SIMPLE_AUTOMATION_TEST` tests named `Velox.<Area>.<Name>` in `Source/Velox/Tests/*Test.cpp` (wrapped in `#if WITH_DEV_AUTOMATION_TESTS`). The `Velox` filter picks them up automatically.

## Caveats

Not executed against a real engine: Unreal Engine is not installed on the dev VM. Only `--dry-run` and error paths have been verified. The `index.json` field names (`tests[].fullTestPath`, `state`) follow the UE automation report format and must be confirmed on first real run.
shellcheck: no suppressions.
