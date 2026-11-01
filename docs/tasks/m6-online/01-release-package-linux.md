# 01 — Release package (Linux): one cook, Shipping client archive, Development server archive

**Milestone:** M6 Online
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0007 §1 (a), §2 (versioning rule), §4

## Goal
`infra/ci/package.sh` turns a tagged commit into the two Linux artefacts M6 ships: `Velox-<ver>-Linux.tar.zst` (Shipping, players) and `Velox-<ver>-LinuxServer.tar.zst` (Development, the headless server), from a single Linux cook on this GPU-less VM, with checksums, a version rule the engine's network version enforces anyway, and disk guards that keep the 15 GB floor. Verified facts: `Build.sh Velox Linux Shipping` succeeds on the installed engine (75 s, 174 MB), `NullDrv` is in both precompiled sets; `-server` does **not** make a packaged Game build dedicated (ADR 0007 fact 2), so the server archive is tested as a headless listen host.

## Scope — files/modules the agent may touch
- `infra/ci/package.sh` (new), `infra/ci/lib.sh` (only: a `ci_package` entry point and shared helpers), `infra/ci/build.sh` (only: `--package` flag calling `ci_package` after green build + tests), `infra/ci/README.md` (package section)
- `Config/DefaultGame.ini` — add `[/Script/EngineSettings.GeneralProjectSettings] ProjectVersion=0.6.0` (ADR 0007 §2)
- `.gitignore` — `Saved/StagedBuilds/`, `Saved/Packages/` if not already covered by `Saved/`
- `docs/tasks/m6-online/notes/01-report.md`
(Anything not listed is out of scope. No Source changes.)

## Relevant ADRs / references
- ADR 0007 §1 (a), §2 "Versioning rule", §4 (pipeline), fact 1–3 (engine, disk)
- ADR 0001 §5 (UAT invocations; the `-server` line there is superseded by ADR 0007)
- `infra/ci/build.sh`, `infra/ci/lib.sh` (env loading, `ENGINE_INSTALLED`, `VELOX_OUT`)
- Memory/STATUS disk rule: keep ≥ 15 GB free; standing OK to delete caches/build artefacts (`~/.cache`, docker build cache), never containers/tagged images/volumes/engine

## Acceptance criteria
- [ ] `infra/ci/package.sh [--platform Linux] [--version vX.Y.Z | --dev] [--out DIR] [--skip-build]` exists; `--help` documents it. Default `--out` is `${VELOX_OUT:-Saved/CI}/packages/<ver>/`.
- [ ] **Version rule:** without `--dev` the script requires `git describe --exact-match --tags` = `vX.Y.Z` and `ProjectVersion=X.Y.Z` in `Config/DefaultGame.ini`, else it exits 2 with a one-line reason. With `--dev` the version is `<ProjectVersion>+<short sha>` and the script prints `DEV BUILD — do not publish`.
- [ ] **Disk guard:** refuses to start below 12 GB free (`df --output=avail /`), prints the free space before and after; after archiving it deletes `Saved/StagedBuilds/` always and `Saved/Cooked/` only when free space is below 15 GB; never ends below 15 GB free in the verification run.
- [ ] One `RunUAT.sh BuildCookRun -project=… -platform=Linux -clientconfig=Shipping -build -cook -stage -pak -compressed -archive -archivedirectory=<out>/Linux -unattended -utf8output -noP4 -nocompileeditor`, then a second `-clientconfig=Development -skipcook -stage -pak -compressed -archive -archivedirectory=<out>/LinuxServer` reusing `Saved/Cooked/Linux`. If `-skipcook` with `-stage` is refused by 5.8's UAT, the fallback is `-clientconfig=Shipping+Development` in one run and splitting the staged binaries by name — report which path was needed.
- [ ] Player archive `Velox-<ver>-Linux.tar.zst`: contains `Velox.sh`, `Velox/Binaries/Linux/Velox-Linux-Shipping`, `Velox/Content/Paks/*.pak`, **no** `.debug`/`.sym`, **no** Development binary. Server archive `Velox-<ver>-LinuxServer.tar.zst`: `Velox/Binaries/Linux/Velox` (Development) plus its `.debug`/`.sym`, same paks. Both ≤ 2.5 GB; sizes printed.
- [ ] `SHA256SUMS` beside the archives (`sha256sum -c` passes); a `MANIFEST.json` with `version`, `net_version` (read from the server log line `LogNetVersion: … Network Version:` of the smoke run below), `sha256`, `size`, `built_at`, `commit`.
- [ ] **Smoke:** the script extracts the server archive to a temp dir under the scratchpad/`VELOX_OUT` and runs `Velox L_ItemTest?listen?game=FFA?SpectatorOnly=1?Name=Server -nullrhi -nosound -unattended -nopause -nosplash -log -port=7797 -UserDir=<tmp>/saved -ExecCmds="t.MaxFPS 60"` for 30 s; pass = log contains `Game Engine Initialized`, a `LogNet: … Listening` (or the 5.8 equivalent `UIpNetDriver … Initialized`) on port 7797, no `Error:`/`ensure`; the process is terminated with SIGTERM and exits within 30 s (ADR 0007 fact 8). The player archive's Shipping binary is started the same way with `-game` for 20 s and must not crash (it will not render; `-nullrhi`).
- [ ] `build.sh --package` runs the script after green build + tests; `infra/ci/README.md` documents the flow and the disk rule.
- [ ] Cook output is deterministic enough that a second run with `--skip-build` reuses the cooked data (iterative cook) and takes less than half the first run's time; both times reported.

## Verification steps
1. Before the first cook: `df -h /`; if below 25 GB free, delete under the standing authorisation: `rm -rf ~/.cache/*` (list first), `docker builder prune -f` (build cache only — never images/containers/volumes). Report what was freed.
2. `flock -o <scratchpad>/soak.lock infra/ci/package.sh --dev --out <scratchpad>/pkg` → both archives, `SHA256SUMS`, `MANIFEST.json`; smoke lines in the output; `df -h /` after ≥ 15 GB.
3. `tar --zstd -tf Velox-*-Linux.tar.zst | grep -c "\.debug$"` → 0; `… | grep "Binaries/Linux/Velox$"` → no match; the server archive: `grep "Binaries/Linux/Velox$"` → 1.
4. `infra/ci/package.sh --dev --skip-build --out <scratchpad>/pkg2` → faster; print both durations.
5. Delete the scratch output; `df -h /`.

## Report back
Standard format plus: wall times of the first and the iterative cook, archive sizes, the UAT path that worked (`-skipcook` vs `+` configs), the exact `Listening`/`Initialized` log line used for the smoke, free space before/after, and anything the Shipping client printed to the log in its 20 s.

## Notes / outcome
(Filled by the agent or the lead after completion.)
