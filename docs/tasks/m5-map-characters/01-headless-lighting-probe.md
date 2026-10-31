# 01 — Headless lighting bake probe (CPU Lightmass on the VM)

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** done (verdict: PATH A FAILS)
**Depends on:** ADR 0006 §5

## Goal
Know, with a log as evidence, whether this GPU-less VM can bake static lighting: `ResavePackages -BuildLighting` through UE's Vulkan RHI on Mesa's llvmpipe software device, driving the local `UnrealLightmass`. Outcome either way is `infra/ci/bake.sh` (path A) or a documented fallback B (`build-mac.sh --bake`) plus the `Velox.Maps.LightingBuilt` guard test. Nothing else in M5 may assume a bake path before this task has reported.

## Scope — files/modules the agent may touch
- `infra/ci/bake.sh` (new), `infra/ci/build-mac.sh` (add `--bake <Map> <Quality>`), `infra/ci/lib.sh` (helper only if needed)
- `Source/Velox/Tests/VeloxMapTests.cpp` (new: `Velox.Maps.LightingBuilt`)
- `docs/content-bootstrap.md` (a "Lighting bake" section), this file
- Nothing under `Content/` is committed from this task (a baked `L_ItemTest` is evidence only; discard it with `git checkout`).

## Relevant ADRs / references
- `docs/adr/0006-first-map-and-characters.md` §5 (paths A/B/C, exact commandlet line), §10
- `docs/adr/0001-project-and-module-structure.md` (renderer settings, static lighting allowed)
- Engine: `/opt/UnrealEngine-5.8/Engine/Source/Editor/SwarmInterface/Private/SwarmInterfaceLocal.cpp`, `Engine/Binaries/Linux/UnrealLightmass`
- Host: `vulkaninfo --summary` shows llvmpipe; ICD at `/usr/share/vulkan/icd.d/lvp_icd.json`

## Acceptance criteria
- [ ] `infra/ci/bake.sh <Map> <Quality>` exists, takes the soak lock (`flock -o "$SOAK_LOCK"`, same lock file the soak scripts use), sets `VK_ICD_FILENAMES` to the lavapipe ICD, runs the ADR §5 commandlet with a `timeout` (default 90 min), tees the log to `Saved/CI/<stamp>-bake/`, and exits non-zero when the log lacks `LogLightmassSolver` completion or the map was not saved.
- [ ] Probe run on `/Game/Maps/L_ItemTest` at `-Quality=Preview` recorded in this file: (a) did the Vulkan RHI initialise on llvmpipe (quote the `LogVulkanRHI` device line or the failure), (b) did `UnrealLightmass` start and finish (quote the solver lines, wall time, peak RSS from `/usr/bin/time -v`), (c) does the saved map carry build data (`UMapBuildDataRegistry` with ≥ 1 lightmap; check via a 10-line Python snippet through the `pythonscript` commandlet or `audit_map.py --lighting` if 16 already exists). Also try `-nullrhi` once if Vulkan fails and record the result (expected: refused or black materials).
- [ ] Verdict written at the top of "Notes / outcome": **PATH A WORKS** (with the Preview wall time) or **PATH A FAILS: <reason>**; in the failing case `build-mac.sh --bake` is implemented (not runnable here; mirrors `bake.sh` with the Metal binary) and `docs/content-bootstrap.md` says the human runs it.
- [ ] `Velox.Maps.LightingBuilt` automation test: for every `+MapsToCook` entry except `L_Empty`, `L_MovementTest`, `L_CombatTest`, `L_ItemTest`, `L_MainMenu` and any `*_Blockout`, the map's `UMapBuildDataRegistry` has lighting data; fails otherwise. Passes today (no such maps yet). Registered in `build.sh`'s automation filter.
- [ ] Project compiles; `infra/ci/build.sh` green; no file under `Content/` modified in the commit.

## Verification steps
1. `source infra/.env; infra/ci/bake.sh /Game/Maps/L_ItemTest Preview` on an idle host (check `uptime` load < 2 and no other UE process: `pgrep -af UnrealEditor`); expect the exit code and log path printed.
2. `/usr/bin/grep -a -E "LogVulkanRHI|LogLightmassSolver|LogStaticLightingSystem|Saved .*L_ItemTest" Saved/CI/*-bake/bake.log | head -40`.
3. `git status --short Content/` is empty after `git checkout -- Content/Maps/L_ItemTest.umap`.
4. `infra/ci/build.sh` → `Velox.Maps.LightingBuilt` listed and passing.
5. Kill your own leftover processes: `pkill -u $USER -f "UnrealLightmass|UnrealEditor-Cmd.*ResavePackages"` (never touch `llama-server`).

## Report back
Standard format. Include the verdict line, the three quoted log lines, wall time, peak RSS, free disk before/after (`df -h /`).

## Notes / outcome
**VERDICT: PATH A FAILS: UE Vulkan RHI rejects llvmpipe (SM5 profile check), and `-nullrhi` crashes when Lightmass starts. Use fallback B (Mac).**

Evidence (VM, llvmpipe LLVM 19.1.7, Vulkan 1.4.305, `VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json`):
- (a) Vulkan RHI: `LogVulkanRHI: Starting Vulkan Profile check for VP_UE_Vulkan_SM5:` / `LogVulkanRHI: Warning: None of the 1 devices meet all the criteria!` / `LogRHI: Display: Skipping SF_VULKAN_SM5...` / dialog `Forced Vulkan device could not be created at the project's supported feature levels`. Commandlet exits 1 after ~85 s wall (cold start); Lightmass never started. With `-FeatureLevelES31`: `Trying to force specific Vulkan feature level but it is not supported.` No engine switch to skip the profile check was found (strings of libUnrealEditor-VulkanRHI.so).
- (b) `-nullrhi` (once): ~9 min of SF_VULKAN_SM5 shader compiles (cold DDC), then `LogStaticLightingSystem: Running Lightmass w/ Sorting mode ENABLED` followed immediately by `Assertion failed: InDesc.IsValid() [RenderGraphResources.inl:51]` in `CreateRenderTarget` and SIGSEGV. `UnrealLightmass` never launched, nothing saved. Peak RSS not captured: `/usr/bin/time` is not installed on this host (bake.sh uses it only if present).
- (c) No map was saved; nothing under `Content/` changed.
- Disk: 19 GB free before, 15 GB after (mostly engine shader DDC growth from the -nullrhi run; nothing in the worktree).

Deliverables: `infra/ci/bake.sh` (soak lock, lavapipe ICD, timeout, log in `Saved/CI/<stamp>-bake/`, non-zero when Lightmass completion or map save is missing; verified to fail correctly here), `infra/ci/build-mac.sh --bake <Map> <Quality>` (delegates to bake.sh with `VELOX_BAKE_PLATFORM=Mac`; not runnable here), `Velox.Maps.LightingBuilt` (passes, "checked 0 shipped maps"; covered by the `Automation RunTests Velox` filter in build.sh), `docs/content-bootstrap.md` "Lighting bake". Plan for L_DM1: Preview/Production bake on the Mac.
