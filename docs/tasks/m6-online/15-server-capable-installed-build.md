# 15 — Server/client-capable installed engine build (blocked on a build host — question 41)

**Milestone:** M6 Online (not an M6 gate; ADR 0007 §1 (c))
**Agent:** dev-hard (when unblocked)
**Status:** blocked (q41)
**Depends on:** ADR 0007 §1 (c); question 41 (a build host with ≥ 300 GB disk and the human's Epic-linked GitHub access); the disk swap acceptance (floor breached by ≈ 5 GB during the swap)

## Goal
Replace `/opt/UnrealEngine-5.8` with an installed build of UE 5.8.4 that also carries the precompiled `UnrealServer` and `UnrealClient` build environments, so `VeloxServer` (Shipping with `bUseLoggingInShipping`) and `VeloxClient` build here, the public server becomes a true dedicated server, and ADR 0001's four targets are finally all real. Built elsewhere because a source build needs > 100 GB and this disk cannot grow.

## Scope — files/modules the agent may touch
- `infra/engine/installed-build-with-server.md` (new): the recipe — on the build box: `git clone --depth 1 -b 5.8 git@github.com:EpicGames/UnrealEngine.git` (the human's access), `Setup.sh`, `GenerateProjectFiles.sh`, then `Engine/Build/BatchFiles/RunUAT.sh BuildGraph -target="Make Installed Build Linux" -script=Engine/Build/InstalledEngineBuild.xml -set:HostPlatformOnly=true -set:WithLinux=true -set:WithLinuxArm64=false -set:WithWin64=false -set:WithMac=false -set:WithAndroid=false -set:WithIOS=false -set:WithTVOS=false -set:WithServer=true -set:WithClient=true -set:WithDDC=false -set:WithFullDebugInfo=false -set:GameConfigurations=Development;Shipping` (verify each option name against the 5.8 `InstalledEngineBuild.xml` on the box; report deviations), expected time and disk (≈ 6–10 h on 16 cores, ≈ 150–250 GB peak), output `LocalBuilds/Engine/Linux`; trim there with `infra/engine/trim-plugins.py` (same list as the 2026-10-24 trim plus `--verify-deps` and the readelf scan from the trim lessons), `tar --zstd` (expect ≈ 25–30 GB compressed), transfer (`rsync -P` over SSH; the archive may not fit here next to the old engine — stream-extract directly as `install-ue-linux.sh` did)
- `infra/engine/install-ue-linux.sh` — a `--from-archive <url|path> --replace` mode: stop `velox-server@*`, verify the free-space arithmetic (current engine 32 GB out, new ≈ 40 GB in; the swap must leave ≥ 10 GB or abort), move the old engine to `/opt/UnrealEngine-5.8.old` only if space allows, else delete after a `--yes` from the human, stream-extract, fix exec bits, restore the clang toolchain path check, sanity build of `VeloxEditor`
- `infra/ci/lib.sh` / `build.sh` — capability detection `test -d "$UE_ROOT/Engine/Intermediate/Build/Linux/x64/UnrealServer"` replaces the `InstalledBuild.txt`-only skip (keep `InstalledBuild.txt` for the editor-only facts); `infra/ci/README.md`
- `Source/VeloxServer.Target.cs`, `Source/VeloxClient.Target.cs` — only if the 5.8 UBT needs a change for installed-build unique environments (expected: none; the targets are already `Unique`)
- `infra/deploy/run-server.sh` (task 06) — `VELOX_SERVER_MODE=dedicated` running `VeloxServer-Linux-Shipping` from a `-LinuxServer` archive built with `-server -serverplatform=Linux -noclient -serverconfig=Shipping`; `infra/ci/package.sh` — the `-server` BuildCookRun when capability is detected
- `docs/machine.md`, `docs/tasks/m6-online/notes/15-report.md`

## Relevant ADRs / references
- ADR 0007 §1 (c), facts 1 and 3; ADR 0001 §5 (targets, UAT server invocation)
- `infra/engine/README.md`, `TRIM-REPORT.md`, memory "engine trim lessons" (verify deps + readelf before deleting; never delete `Engine/Extras/ThirdPartyNotUE`)
- STATUS q1 (disk), q3 (the superseded "source build before M6")

## Acceptance criteria
- [ ] The recipe document is complete enough for the human (or an agent with SSH to the box) to run unattended; every BuildGraph option verified against the 5.8 XML; a disk/time log kept.
- [ ] After the swap: `Build.sh VeloxServer Linux Shipping -Project=…` and `Build.sh VeloxClient Linux Development -Project=…` succeed; `build.sh` prints `VeloxServer: OK` instead of `SKIPPED`; all automation tests green; `nettest.sh --server-mode dedicated --server-binary <VeloxServer-Linux-Shipping>` passes the default list (Shipping with logging — the log lines the runner judges must exist; `bUseLoggingInShipping` is already in the target).
- [ ] `package.sh` produces `Velox-<ver>-LinuxServer.tar.zst` from the real server cook (smaller: no client content); `run-server.sh` dedicated mode; the public instance switched and `release-smoke.sh --public` PASS.
- [ ] Free space after everything ≥ 15 GB (the floor is breached only during the swap, with the human's acceptance recorded in the report).
- [ ] ADR 0007 amended by the lead: §1 (c) becomes "done", ADR 0001 §5's target table is finally true.

## Verification steps
1. On the box: the BuildGraph run; `ls LocalBuilds/Engine/Linux/Engine/Intermediate/Build/Linux/x64/` shows `UnrealServer` and `UnrealClient`.
2. Here: the swap per the script with each root step stated; `df -h /` before/after.
3. `infra/ci/build.sh` → all four targets built; the nettest list; `release-smoke.sh --public`.

## Report back
Standard format plus: BuildGraph options actually used, build time and peak disk on the box, archive size, the swap's free-space timeline, and the first `VeloxServer` RSS/start-up time compared with the Game-build listen host.

## Notes / outcome
(Filled by the agent or the lead after completion.)
