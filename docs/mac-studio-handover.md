# Mac Studio handover runbook (Apple Silicon, arrives about 2026-11-20)

One ordered list for the human. The Mac Studio becomes the main compile / test / bake / playtest host. Until now the Mac was an Intel MacBookPro that could compile but not render UE 5.8 (STATUS.md question 26), so everything Mac-side was queued for "the Apple Silicon Mac". Steps 1-4 close M5 and the open playtests; step 5 is M6 work; step 6 is what to send back.

Conventions: shell blocks are copy-pasteable in Terminal (zsh). `$REPO` is the clone directory. Commands were checked against the scripts in `infra/ci/` on branch `m6-online`; where a script has a known defect the runbook says so and gives a workaround ("Known script gaps" at the end).

## 1. Setup

1. **macOS** Sonoma 14.5 or newer. Native arm64, no Rosetta needed.
2. **Xcode 26.0 or 26.1.1. Not 26.4** (incompatible with UE 5.8; STATUS q26, `docs/playtests/m1-movement-checklist.md` section 1). Install from the App Store or developer.apple.com (older releases), then once:
   ```
   sudo xcode-select -s /Applications/Xcode.app
   sudo xcodebuild -license accept
   xcodebuild -version        # must say 26.0 or 26.1.1
   ```
   Do not let the App Store auto-update Xcode to 26.4.
3. **UE 5.8** via the Epic Games Launcher: Unreal Engine -> Library -> install 5.8 (about 40 GB, default `/Users/Shared/Epic Games/UE_5.8`, which `build-mac.sh` probes without configuration). Do not build the engine from source for this. Start the editor once from the Launcher so it finishes its first-run setup.
4. **Tools** (Homebrew; on Apple Silicon these are bottles, no source compile):
   ```
   brew install git git-lfs coreutils
   git lfs install
   ```
   (`coreutils` is optional: the scripts use `gtimeout` if present and a built-in bash fallback otherwise.)
5. **Clone and branch.** `m6-online` contains `main` (M0-M5 VM part) plus the M6 work merged so far; the lead names a different branch if that changes.
   ```
   git clone https://github.com/refaustria/velox "$HOME/velox" && cd "$HOME/velox"   # REPO=$HOME/velox
   git checkout m6-online
   git lfs pull
   git lfs ls-files | head -3     # sanity; a pointer stub (about 130 bytes) instead of content means the pull failed
   ```
   Without `git lfs pull`, maps and assets are stubs and the automation run crashes (this happened on the Intel Mac, STATUS "First Mac compile").
6. **Environment** (only needed if UE is not at the default path; `infra/.env` is gitignored, template `infra/env.example`):
   ```
   printf 'UE_ROOT="/Users/Shared/Epic Games/UE_5.8"\nVELOX_PROJECT=%s/Velox.uproject\n' "$PWD" > infra/.env
   ```
   Real environment variables win over `infra/.env`. Never commit it.

## 2. First build and automation tests

```
cd "$REPO"
infra/ci/build-mac.sh
```
Builds `VeloxEditor` and `Velox` (Mac, Development, arm64 by the host default), then runs the headless `Velox.*` automation tests. `VeloxServer` / `VeloxClient` print `SKIPPED` (expected, ADR 0001 section 5). First run 20-60 min, later a few minutes. Success ends with `[ci] CI OK. Logs: Saved/CI/<timestamp>`; the log directory holds `build-*.log`, `tests.log`, `automation/index.json`. The Intel Mac reached 53 tests green on 2026-10-30; on `m6-online` expect roughly 125 (the lead's last full build). The exact count is not a criterion; "0 not successful" is.

Flags (all in `infra/ci/lib.sh`): `--no-tests`, `--tests-only`, `--clean`, `--config Development|Shipping|...`, `--targets VeloxEditor,Velox`, `--dry-run`, `-h`. `--net-soak` and `--package` are rejected on Mac for now (Linux only; Mac `--package` is task 11, step 5.1).

On failure: send `Saved/CI/<timestamp>/` (step 6) and stop. Then open `Velox.uproject` once (first start compiles shaders, 5-10 min; answer "Yes" to a rebuild prompt) and let it finish.

## 3. M5 acceptance: bake, commit, checklist, decision 40

Spec: `docs/tasks/m5-map-characters/16-lighting-bake-and-perf-audit.md` part B, `docs/content-bootstrap.md` ("Lighting bake"), `docs/playtests/m5-map-characters-checklist.md`.

1. **Production bake of `L_DM1`** (1-3 h expected on Metal, unattended; close other apps, keep the Mac awake):
   ```
   cd "$REPO"
   git checkout -b mac/m5-bake
   caffeinate -i infra/ci/build-mac.sh --bake /Game/Maps/DM1/L_DM1 Production
   ```
   `--bake <Map> <Quality>` is passed through to `infra/ci/bake.sh` with `VELOX_BAKE_PLATFORM=Mac`; quality is `Preview|Medium|High|Production`; env knobs `VELOX_BAKE_TIMEOUT` (default 14400 s for Production, 5400 s otherwise) and `VELOX_BAKE_EXTRA`. Log: `Saved/CI/<timestamp>-bake/bake.log`. It exits non-zero unless the log shows Lightmass completion and a save line for `L_DM1`.
2. **Verify the bake** (the figures `Velox.Maps.LightingBuilt` asserts):
   ```
   UE="/Users/Shared/Epic Games/UE_5.8"   # or your UE_ROOT
   "$UE/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor-Cmd" "$PWD/Velox.uproject" -run=pythonscript \
     -script="$PWD/Tools/editor/audit_map.py /Game/Maps/DM1/L_DM1 --lighting" -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
   ```
   Expect a `lighting` line with `registry=1 lightmapped=<n>/<m> vlm=1`, `n` > 0, and `LIGHTING PASS`.
3. **Commit exactly two files** and push the branch (LFS):
   ```
   git add Content/Maps/DM1/L_DM1.umap Content/Maps/DM1/L_DM1_BuiltData.uasset
   git status --short            # only these two
   git commit -m "feat(maps): Production lighting bake of L_DM1 (Mac)"
   git push -u origin mac/m5-bake
   ```
   Tell the lead the wall time and the `LogLightmassSolver` completion line (`grep -a LogLightmassSolver Saved/CI/*-bake/bake.log | tail -3`). The lead then adds `L_DM1` to `+MapsToCook` (`Config/DefaultGame.ini`), re-runs `Velox.Maps.LightingBuilt` / `DM1Budget` on the VM, and merges.
4. **M5 playtest**: `docs/playtests/m5-map-characters-checklist.md` (about 60 min; section 1 is the setup, tables 2-7, sign-off in section 8). Tables 4 and 6 (lighting, frame time) only count on the baked map. Table 7 holds the Mac-only checks (ABP_Velox, `weapon_r` offsets, bot feel q30, bots on the art map). Fill the sign-off block (section 8) including the `ProfileGPU` screenshot.
5. **Decision 40**: in the sign-off tick "Decision 40 confirmed: `ServerDefaultMap` -> `L_DM1`: YES / NO" (recommendation yes). The lead then changes `Config/DefaultEngine.ini` `ServerDefaultMap` (currently `L_ItemTest`) and ticks the ROADMAP M5 box on PASS or PASS WITH NOTES.

## 4. Earlier playtests still open

All have been blocked since 2026-10-30 only by the missing renderer (STATUS "Human playtests M1/M2/M3 BLOCKED"); none has a signed sign-off yet. Do them after step 3 (the bake needs the whole machine). Each has its own setup section, a table, and a sign-off block; the PIE recipe is the same (Number of Players 2, Net Mode Play As Client, Run Under One Process on, 100 ms / 2 %).

| Order | Checklist | Sign-off block |
|---|---|---|
| 1 | `docs/playtests/m1-movement-checklist.md` (M1 done criterion) | section 5, paste into STATUS.md |
| 2 | `docs/playtests/m2-combat-checklist.md` | section 5, paste into STATUS.md |
| 3 | `docs/playtests/m3-gameloop-checklist.md` | the template at the end of the file |
| 4 | `docs/playtests/m4-bots-checklist.md` (M4 playtest; difficulty rows relate to STATUS q30) | section 6 |

Check STATUS.md "Done" / "Blocked" before starting in case the lead has signed one off from other evidence.

## 5. M6 items queued for the Mac

1. **Task 11, macOS client package** (`docs/tasks/m6-online/11-mac-client-package.md`). **`build-mac.sh --package` does not exist yet** (task status `todo`; the script rejects `--package` on Mac). The lead dispatches a `dev` agent to write it once the Mac is up. When it lands the flow is: `infra/ci/build-mac.sh --package --dev` (Shipping, arm64, ad-hoc signed per q47 (a); produces `Velox-<ver>-macOS.zip`, `SHA256SUMS`, `MANIFEST.json`), `codesign -dv --verbose=2 Velox.app`, `spctl -a -vv Velox.app` (rejection expected for ad-hoc), launch it, Servers -> Connect to the public server, play 5 min, `scp` the zip to the VM, then on the VM `sudo infra/deploy/publish-download.sh <ver> Velox-<ver>-macOS.zip`. Developer ID / notarisation (q47 (b)) is out of scope; credentials never go into the repo.
2. **Mac-only checks named in STATUS.md's M5 entry**: ABP_Velox, `weapon_r` / view-model offsets, dry fire, tracer origin at the eye for remote pawns, projectile looks. M5 checklist rows 48-51 cover ABP_Velox, offsets, bot feel and bots on the art map; dry fire, tracer origin on remote pawns and projectile looks are covered by table 2 rows 10-12 and the combat rows of the M2 checklist. Write any deviation into the free-text findings.
3. **Optional, no decision implied: task 15, server-capable installed engine** (`docs/tasks/m6-online/15-server-capable-installed-build.md`, blocked on q41). It needs a build host with at least 300 GB free disk and the human's Epic-linked GitHub access (the Linux `UnrealServer` / `UnrealClient` environments need a source engine build, which the VM's disk cannot hold). The Mac Studio's 2 TB SSD and 128 GB RAM make the size and compile time feasible, but the task 15 recipe is a Linux BuildGraph (`Make Installed Build Linux`), so it needs Linux (a VM or container on the Mac, or another box) rather than macOS itself. If you want it, say so under q41 in STATUS.md; otherwise skip. The Game-build listen server (q41 (b)) already carries M6.

4. **The VM's engine install (`/opt/UnrealEngine-5.8`, ~33 GB) — decide after a Linux build test.** The public server needs a Linux x86_64 package, and as far as we know Epic supports cross-compiling to Linux only from a Windows host, not from macOS. Before the VM engine is removed, prove on the Mac Studio that a Linux x86_64 `LinuxServer` package can be built there (a Linux VM or container: x86_64 emulation, or an arm64 Linux host cross-compiling to x86_64 — untested), copy it to the VM and pass the packaged gate P1 (ADR 0007 F2). Until that has worked, the VM keeps the engine for Linux packages and the netcode soaks; running the deployed server needs only the unpacked package (~870 MB) and `veloxmaster`, not the engine. The lead announces the removal before doing it (it is outside the repo).

## 6. What to send back to the lead

Everything goes via the repo or a share; never commit secrets or `infra/.env`.

- **Branch** `mac/m5-bake` pushed (step 3.3).
- **Build**: the `Saved/CI/<timestamp>/` directory of the first green (or failing) `build-mac.sh` run, zipped (`zip -r ci-first-run.zip Saved/CI/<timestamp>`); the last lines `[ci] CI OK. Logs: ...` and the `N tests, 0 not successful` line.
- **Bake**: `Saved/CI/<timestamp>-bake/bake.log` (zipped), wall time, the `LogLightmassSolver` line, the `audit_map.py --lighting` output.
- **Checklists**: each filled sign-off block pasted into STATUS.md as the checklist instructs, or saved as a text file and committed on a branch `mac/playtests`; screenshots (HUD, scoreboard, menus; the `ProfileGPU` screenshot and the `stat fps` / `stat unit` / `stat scenerendering` figures per position for M5 table 6); the newest `Saved/Logs/Velox*.log` per session (M3 asks for it).
- **Decisions**: decision 40 YES/NO, bot feel answers (q30 a/b/c), whether you want task 15 (q41).
- **Machine facts** for `docs/machine.md`: `sw_vers`, `xcodebuild -version`, `sysctl -n machdep.cpu.brand_string`, `df -h /`, the UE version shown in the Launcher.
- **How**: `git push -u origin <branch>` (LFS files go with it); large zips via `scp` to the VM or any share. Then tell the lead "Mac ready / M5 baked / playtests done"; the lead merges and updates STATUS and ROADMAP.

## Known script gaps

(The earlier bake gaps - GNU-only `time -v`, missing `timeout`, a too-short default timeout - are fixed in `bake.sh`/`lib.sh`.)

1. `build-mac.sh --package` (task 11) is missing; `--net-soak` is Linux only, so there is no network soak on the Mac (playtests use PIE).
