# Velox Status

_Last updated: 2026-10-24 evening (session 1, project lead). Branch: `main` (M0 merged), M1 work on `m1-movement`._

## Done
- **M0 Infrastructure complete** (2026-10-24), merged to `main`. Final CI: `build.sh --clean` green (VeloxEditor, Velox, 2 tests, `L_Empty` loads). RunUAT repaired after the trim.
- Repo bootstrapped: `ROADMAP.md`, `STATUS.md`, `docs/tasks/TEMPLATE.md`, subagents `dev` / `dev-hard` / `architect` in `.claude/agents/`.
- **ADR 0001** project & module structure (`docs/adr/0001-project-and-module-structure.md`): one `Velox` module with domain folders, four targets, GAS on PlayerState, CMC saved-move prediction, server-side lag-compensation subsystem, forward renderer + MSAA + static lighting, 60 Hz server tick, test strategy, build-host strategy (Linux headless, Mac client on the human's Mac).
- **M0-01** machine inventory (`docs/machine.md`), Linux toolchain installed, Git LFS + `.gitattributes` + `.gitignore`, `infra/env.example`.
- **M0-02** `https://games.inceptum.at` live: nginx vhost, Let's Encrypt (valid to 2027-01-22, auto-renew), placeholder landing page, `/downloads/` and `/status/` locations prepared. Config in `infra/nginx/`.

- **M0-03** Unreal project skeleton: all classes/targets/config from ADR 0001; two-reviewer static pass, then **compiled green on this VM** (VeloxEditor 62 s, Velox 67 s, Development). Review findings applied (incl. removal of the nonexistent `bUseIris` target setting).
- **M0-05** CI scripts `infra/ci/build.sh` / `build-mac.sh` / `lib.sh`: real run green, automation report parsed, tests `Velox.Core.Smoke` and `Velox.Movement.SavedMoveFlags` pass.
- **M0-06** UE 5.8.4 (CL 58902052, installed build) at `/opt/UnrealEngine-5.8`, streamed from Epic's S3 link without on-disk archive; clang toolchain v26 restored from Epic's public CDN; exec bits fixed; 4 GB swapfile. Installer hardened (`infra/engine/install-ue-linux.sh`: ranged-GET size check, anchored exclusions, exec-bit fix, toolchain fetch, sanity check, optional plugin trim).
- Dependency-aware engine trim tooling (`infra/engine/trim-plugins.py`, `TRIM-REPORT.md`).
- **M0-04** `Tools/editor/create_bootstrap_content.py`: `L_Empty`, `IMC_Default` + `IA_Move/Look/Jump/Fire`, `DA_PawnData_Default`, `DA_MovementSettings_Default` (ADR 0002 values), `DA_InputConfig_Default`; idempotent, `-game -nullrhi` load clean, cook commandlet OK (414 packages). Assets in LFS (9 files).

- **ADR 0002** movement component design (`docs/adr/0002-movement-component-design.md`, accepted) and M1 task files `docs/tasks/m1-movement/01–06`.
- Engine trim applied: 17 GB freed (25 GB free now), restore list at `/home/bojan/ue-trim-restore-20261024-1432.txt`.

## In progress
- **M1 Movement** started: tasks 01 (move sim, dev-hard) and 03 (test map, dev) dispatched in parallel.

## Blocked
- (none)
- Mac client build: human has Xcode but no UE 5.8 yet (Epic Games Launcher install needed, ~40 GB).

## Next
- M0-04 → full CI run → merge `m0-infrastructure` into `main` → M1 Movement: `architect` ADR 0002 (movement component design), then tasks.
- Disk hygiene: the VM disk cannot be enlarged (human, 2026-10-24). Candidates still awaiting the human's OK: `~/.cache/ms-playwright` 3.5 GB, `~/.cache/pnpm` 0.5 GB, `gitref-v2` build artefacts 4.1 GB, `rusuite` build artefacts 1.8 GB.

## Open questions for the human
1. ~~Disk space~~ — resolved 2026-10-24: disk cannot be enlarged; freed instead: docker 19 GB, engine trim 17 GB + exclusions, caches/build artefacts 9.8 GB (approved). **35 GB free.** Budget rule from now on: keep ≥ 15 GB free; cooked builds go to `Saved/` and are pruned after upload.
2. ~~UE 5.8 Linux download~~ — resolved 2026-10-24: human supplied the signed URL; UE 5.8.4 streamed into `/opt/UnrealEngine-5.8` via `bsdtar` (debug symbols, Templates, FeaturePacks, Samples, Documentation excluded to fit the disk).
3. **Installed vs source engine.** Installed engines (Launcher, Linux zip) cannot build the `VeloxServer`/`VeloxClient` targets; the dedicated server is then `Velox -server`, which is fine until M6. A source build (needs EpicGames GitHub org linked to your Epic account, ~150 GB disk, hours of compile) is needed for M6 shipping. Recommendation: installed engine now, source build before M6. OK?
4. **Mac toolchain.** Human has Xcode but not UE 5.8 yet. Action for the human when the Linux build is green: install the Epic Games Launcher on the Mac and UE 5.8 through it (~40 GB, default path `/Users/Shared/Epic Games/UE_5.8`), then run `infra/ci/build-mac.sh` from the repo root. Still needed: Apple Silicon or Intel.
5. **Renderer decision (veto window).** ADR 0001 picks forward renderer + MSAA + baked static lighting (Q3-style readability, max fps). This shapes all M5 art. Veto now if you prefer deferred + TAA.
6. **Scale.** Netcode defaults assume ≤ 16 players per server until M6. Confirm.
7. ~~Delete `Engine/Platforms/{Android,IOS,VisionOS}`~~ — approved and done 2026-10-24.
9. ~~Fresh download link~~ — provided and used 2026-10-24 (four targeted extractions).
8. ~~ADR 0002 defaults~~ — approved 2026-10-24 as written (vanilla Q3, CPM air control off, ground accel 10, unit scale 2.5, auto-hop on).
