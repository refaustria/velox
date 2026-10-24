# Velox Status

_Last updated: 2026-10-24 evening (session 1, project lead). Branch: `main` (M0 merged), M1 work on `m1-movement`._

## Done
- **M1-05a** debug tooling (`velox.move.*` cvars, HUD block, `showdebug VeloxMovement`, CSV recorder + `Tools/analyze_run.py`), scripted input component with test RPCs, four move scripts, `FT_MovementDeterminism` (bit-identical), scripted regression tests (stairs 100 %, ramp exit Vz = v·tanθ, latch, knockback). 21 tests green. Map defects fixed (ramps, pad C, PlayerStart). ADR A23 corrects the wall-slide expectation (Q3 wall-hug ≈ 943 cm/s).
- **M1-05b** `infra/ci/nettest.sh` + `build.sh --net-soak`: headless dedicated server + client soak with packet simulation.
- **M1-04** saved-move fields + combine rules, move-response container with per-timestamp server snapshot, `AddKnockback` flow, jump latch (chained through replay), `AVeloxJumpPad` (AimAtTarget/FixedVelocity, MaxLaunchSpeed validation, map check), pads placed in `L_MovementTest`; architect-reviewed (A17–A22), D1–D6 fixed. 15 tests green.
- **M1-02** Quake physics in `UVeloxMovementComponent` (CalcVelocity via sim, auto-hop, ramp slide/exit, knockback consume hook, settings as single writer of engine fields), character geometry from pawn data; architect-reviewed (A9–A16), follow-ups applied. 12 tests green, `L_MovementTest -game -nullrhi` clean.
- **M1-01** `FVeloxMoveSim` (world-free Q3/CPM sim, fixed 8 ms substeps), full `UVeloxMovementSettings` table (29 fields), 11 movement unit tests; architect-reviewed, two fidelity fixes applied; ADR 0002 amendments A1–A8.
- **M1-03** `L_MovementTest` greybox map generator (5120 cm corridor with markers, ramps 10–50°, stairs, 3 pad placeholders, 3 gaps) + 12 unlit greybox materials; default map switched.
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
- **M1-05b** net soak analysis: architect root-causing two findings from `infra/ci/nettest.sh` (100 ms / 2 % loss both ways, server 60 Hz, client 120 Hz):

  | Script | Moves | Corrections | max / mean cm | Result |
  |---|---|---|---|---|
  | StrafeRun | 437 | 0 | 0 / 0 | PASS (peak 1430 cm/s = 1.79×) |
  | PadRun | 2819 | 0–3 | ≤ 1.8 | PASS |
  | KnockbackRun | 300 | 4 | 23.0 / 5.8 | FAIL: 3–5 zero-cm "echo" corrections after the intended one (unpredicted server knockback → in-flight moves mismatch) |
  | LatchRun | 420 | 5 | 5.0 / 1.0 | flaky 1/4: one 32 cm correction, server leads by exactly 1/30 s at 800 cm/s |

  Then fixes → final CI → merge → M1-06 human playtest on the Mac.

## Blocked
- (none)
- Mac client build: human has Xcode but no UE 5.8 yet (Epic Games Launcher install needed, ~40 GB).

## Next
- M0-04 → full CI run → merge `m0-infrastructure` into `main` → M1 Movement: `architect` ADR 0002 (movement component design), then tasks.
- Disk hygiene: the VM disk cannot be enlarged (human, 2026-10-24). Candidates still awaiting the human's OK: `~/.cache/ms-playwright` 3.5 GB, `~/.cache/pnpm` 0.5 GB, `gitref-v2` build artefacts 4.1 GB, `rusuite` build artefacts 1.8 GB.

## Open questions for the human
11. **Crouch out of M1** (architect, ADR 0002 A11): the engine's crouch resizes to class-default capsule geometry, not our pawn-data geometry; fixing it properly belongs to the M2 hitbox/lag-comp design (recommendation: Lyra-style class-default geometry). `bCanCrouch=false` for the M1 playtest. Veto if you want crouching in the movement playtest.
10. **GitHub push blocked (403).** `gh` is logged in as refaustria with push permission on the repo, but `git push` over HTTPS is denied and no SSH key exists on the VM. Likely the stored token lacks the `repo` write scope. Fix options: run `gh auth refresh -h github.com -s repo` interactively (device-code flow), or add an SSH deploy key with write access. Nothing has been pushed yet; all work is local on `main`, `m0-infrastructure`, `m1-movement`.
1. ~~Disk space~~ — resolved 2026-10-24: disk cannot be enlarged; freed instead: docker 19 GB, engine trim 17 GB + exclusions, caches/build artefacts 9.8 GB (approved). **35 GB free.** Budget rule from now on: keep ≥ 15 GB free; cooked builds go to `Saved/` and are pruned after upload.
2. ~~UE 5.8 Linux download~~ — resolved 2026-10-24: human supplied the signed URL; UE 5.8.4 streamed into `/opt/UnrealEngine-5.8` via `bsdtar` (debug symbols, Templates, FeaturePacks, Samples, Documentation excluded to fit the disk).
3. **Installed vs source engine.** Installed engines (Launcher, Linux zip) cannot build the `VeloxServer`/`VeloxClient` targets; the dedicated server is then `Velox -server`, which is fine until M6. A source build (needs EpicGames GitHub org linked to your Epic account, ~150 GB disk, hours of compile) is needed for M6 shipping. Recommendation: installed engine now, source build before M6. OK?
4. **Mac toolchain.** Human has Xcode but not UE 5.8 yet. Action for the human when the Linux build is green: install the Epic Games Launcher on the Mac and UE 5.8 through it (~40 GB, default path `/Users/Shared/Epic Games/UE_5.8`), then run `infra/ci/build-mac.sh` from the repo root. Still needed: Apple Silicon or Intel.
5. **Renderer decision (veto window).** ADR 0001 picks forward renderer + MSAA + baked static lighting (Q3-style readability, max fps). This shapes all M5 art. Veto now if you prefer deferred + TAA.
6. **Scale.** Netcode defaults assume ≤ 16 players per server until M6. Confirm.
7. ~~Delete `Engine/Platforms/{Android,IOS,VisionOS}`~~ — approved and done 2026-10-24.
9. ~~Fresh download link~~ — provided and used 2026-10-24 (four targeted extractions).
8. ~~ADR 0002 defaults~~ — approved 2026-10-24 as written (vanilla Q3, CPM air control off, ground accel 10, unit scale 2.5, auto-hop on).
