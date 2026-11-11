# 14 — Windows client (blocked on a Windows host — question 46)

**Milestone:** M6 Online
**Agent:** dev (script, now) / human (host and run)
**Status:** blocked (q46)
**Depends on:** ADR 0007 §4; question 46; tasks 01/11 (same version rule, manifest, publish flow)

## Goal
A Windows client package `Velox-<ver>-Windows.zip` (Shipping, Win64) with the same version rule and manifest as the Linux and macOS packages. UE cannot cross-compile Windows from Linux or macOS and no Windows host exists; this task writes the script now and waits for a host. M6 is accepted with Windows listed as an exception unless question 46 supplies one.

## Scope — files/modules the agent may touch
- `infra/ci/build-win.ps1` (new, PowerShell 7): `-Package [-Version vX.Y.Z | -Dev] [-Out DIR]`, probes `UE_ROOT` (`C:\Program Files\Epic Games\UE_5.8` default), runs `Engine\Build\BatchFiles\RunUAT.bat BuildCookRun -project=… -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -compressed -archive -archivedirectory=<out>\Win64 -unattended -utf8output -noP4 -nocompileeditor`, zips `Windows\` (`Compress-Archive`), writes `SHA256SUMS` (`Get-FileHash`) and `MANIFEST.json`, prints sizes; `-Dev` naming as the other scripts
- `infra/ci/README.md` (Windows section, marked untested until run), `docs/tasks/m6-online/notes/14-report.md`
(No `Source/`. No execution possible here; `pwsh` is not installed on the VM — if it is, `pwsh -NoProfile -Command "& { Set-StrictMode -Version Latest; . ./infra/ci/build-win.ps1 -Help }"` is the only test.)

## Relevant ADRs / references
- ADR 0007 §4 (matrix), question 46 (options: defer; human's Windows PC with the Launcher engine ≈ 70 GB; paid hosted Windows runner — impractical)
- `infra/ci/package.sh` (task 01) for the exact manifest fields and naming

## Acceptance criteria
- [ ] `build-win.ps1` exists with the flow above, `Set-StrictMode -Version Latest`, `$ErrorActionPreference = 'Stop'`, clear error on a missing engine, the version rule (needs `git` on PATH), and a header stating it is untested until a Windows host runs it.
- [ ] README section with the prerequisites for the human (Windows 10/11, Visual Studio 2022 with the UE workload per Epic's 5.8 docs, Launcher UE 5.8, git + git-lfs, PowerShell 7).
- [ ] When a host exists (q46 (b)): the human runs it, the zip launches on Windows, connects through the browser to the public server, and is published with `publish-download.sh`; `latest.json` gains `windows`; the landing page's Windows note is updated (task 12).

## Verification steps
1. Static review by the lead (no Windows here); optional `pwsh` parse if available.
2. On the Windows host: `pwsh -File infra/ci/build-win.ps1 -Package -Dev` → zip + sums; launch; connect; publish.

## Report back
Standard format; state explicitly that the script is unexecuted.

## Notes / outcome
(Filled by the agent or the lead after completion.)
