# 05 — CI build scripts for Linux and macOS

**Milestone:** M0 Infrastructure
**Agent:** dev
**Status:** in progress
**Depends on:** ADR 0001 §5 (build hosts)

## Goal
`infra/ci/build.sh` (Linux) and `infra/ci/build-mac.sh` (macOS) build every target that the installed engine can build, run the automation tests headlessly, parse the JSON report and exit non-zero on any failure. They are the CI until GitHub Actions is available again (2026-11-01) and remain the local pre-merge check afterwards.

## Scope — files/modules the agent may touch
- `infra/ci/build.sh`, `infra/ci/build-mac.sh`, `infra/ci/lib.sh` (shared functions), `infra/ci/README.md`
- `infra/env.example` (extend, keep existing keys)

## Relevant ADRs / references
- `docs/adr/0001-project-and-module-structure.md` §4 (tests) and §5 (build hosts): use the exact UBT/UAT invocations and the installed-vs-source detection via `Engine/Build/InstalledBuild.txt` given there.

## Acceptance criteria
- [ ] Both scripts: `set -euo pipefail`, read `UE_ROOT` (and `VELOX_PROJECT`, default = repo root `Velox.uproject`) from env or `infra/.env` if present, fail with a clear message if `UE_ROOT` is missing or has no `Engine/Build/BatchFiles`.
- [ ] Targets: always `VeloxEditor` and `Velox`; `VeloxServer` and `VeloxClient` only if the engine is a source build (no `InstalledBuild.txt`), otherwise print `SKIPPED (installed engine cannot build Server/Client targets)`. Config `Development` default, overridable via `--config`. Platform `Linux` / `Mac` respectively.
- [ ] Tests: run `UnrealEditor-Cmd` with the exact ADR §4 command (`-ExecCmds="Automation RunTests Velox; Quit"`, `-nullrhi -unattended -nopause -nosplash -log -TestExit="Automation Test Queue Empty" -ReportExportPath=<dir>`), then parse `index.json` in the report dir (python3 one-liner or jq) and fail if any test state is not `Success`. Do not trust the editor exit code alone. Print a summary table of tests.
- [ ] Flags: `--no-tests`, `--tests-only`, `--targets a,b`, `--config X`, `--clean`. `-h` prints usage.
- [ ] Log files land in `Saved/CI/<timestamp>/`; the scripts print the paths on failure.
- [ ] `build-mac.sh` uses `Engine/Build/BatchFiles/Mac/Build.sh` and `Engine/Binaries/Mac/UnrealEditor-Cmd` (app bundle path handled: `UnrealEditor-Cmd` may live inside `UnrealEditor.app/Contents/MacOS/`); default `UE_ROOT` candidates `/Users/Shared/Epic Games/UE_5.8` are probed if the env var is unset.
- [ ] `infra/ci/README.md`: how to run on each OS, what SKIPPED means, how to add tests.
- [ ] `bash -n` passes for all scripts; `shellcheck` (install via apt if missing) reports no errors (warnings allowed, document any you suppress).
- [ ] A dry-run mode (`--dry-run`) prints the commands without running them so the scripts can be verified on this VM without an engine. Include the dry-run output for Linux in the report.

## Verification steps
1. `bash -n infra/ci/*.sh && shellcheck infra/ci/*.sh`.
2. `UE_ROOT=/nonexistent infra/ci/build.sh` fails fast with the clear message.
3. `UE_ROOT=/tmp/fakeue infra/ci/build.sh --dry-run` (create the minimal fake dir structure in the scratchpad, incl. `Engine/Build/InstalledBuild.txt`) prints Editor+Game builds, SKIPPED for Server/Client, and the test command.

## Report back
Standard format with the dry-run output.
