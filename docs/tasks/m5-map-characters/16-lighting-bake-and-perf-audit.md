# 16 — `audit_map.py`, tightened `Velox.Maps.LightingBuilt`, lighting bake of `L_DM1` (Mac), cook, default map

**Milestone:** M5 Map + characters
**Agent:** part A `dev` (VM); part B the human on the Apple Silicon Mac, then `dev` (VM) for verification
**Status:** todo
**Depends on:** part A: 05 (the audit tool can start right after the blockout), 15 (`L_DM1`); part B: 15, part A, the Apple Silicon Mac; ADR 0006 §5, §8 and **E0.1**

## Goal
`Tools/editor/audit_map.py` exists (static budgets, collision diff, protrusion, lighting-built check with the E0.1 (d) figures, LFS total; JSON output consumed by `Velox.Maps.DM1Budget`). `Velox.Maps.LightingBuilt` proves a real bake (≥ 1 lightmap and a volumetric lightmap), not just a registry. `L_DM1` is baked at Production quality **on the Apple Silicon Mac by the human** (ADR 0006 E0.1: the VM cannot bake, the Intel Mac cannot render), the `.umap` and its `_BuiltData` are committed, the map cooks, `L_DM1` joins `MapsToCook` and `ServerDefaultMap` moves to it. Part A never blocks on the Mac; M5 acceptance blocks on part B.

## Scope — files/modules the agent may touch
- Part A: `Tools/editor/audit_map.py` (new), `Source/Velox/Tests/VeloxMapTests.cpp` (`Velox.Maps.LightingBuilt` tightened; `Velox.Maps.DM1Budget` reads `Saved/Audit/L_DM1.json` if present, else skips with a log), `infra/ci/build.sh` (run the audit after the build when the map exists; non-fatal until `L_DM1` is in `MapsToCook`), `docs/content-bootstrap.md` (audit section; the bake section says exactly what the human runs), `docs/tasks/m5-map-characters/notes/16-report.md`
- Part B (human): `Content/Maps/DM1/L_DM1.umap` + `Content/Maps/DM1/L_DM1_BuiltData.uasset` (baked; LFS). Then `dev`: `Config/DefaultGame.ini` (`+MapsToCook` L_DM1), `Config/DefaultEngine.ini` (`ServerDefaultMap`, only after decision 40 is confirmed at acceptance), `notes/16-report.md`

## Relevant ADRs / references
- ADR 0006 §5 (path B, Production for acceptance, `LightingBuilt` guard), **E0.1 (a)–(e)** (path A withdrawn; `MapsToCook` only with the bake; the three-figure pass condition; the split), §8 (budget table), §2 (`--diff-collision`)
- Task 01's verdict and `infra/ci/bake.sh` / `build-mac.sh --bake`; engine: `Engine/Source/Runtime/Engine/Classes/Engine/MapBuildDataRegistry.h` (`GetMeshBuildData`, `GetLevelPrecomputedVolumetricLightmapBuildData`, `FMeshMapBuildData::LightMap`)

## Acceptance criteria — part A (VM, `dev`)
- [ ] `audit_map.py <Map> [--diff-collision A B] [--protrusion] [--lighting] [--json out]`: loads the map headless (`pythonscript` commandlet), reports per-mesh triangle counts (`LODResources[0]` via `unreal.StaticMesh.get_num_triangles(0)` or the editor library), distinct meshes/materials, lightmap resolutions and MB estimate (`Σ res² × 4 B × 1.3`), texture MB (all textures referenced by the map's materials), components with lightmap res > 64, translucent materials, `git lfs ls-files -s` total; `--lighting` prints `registry=<0|1> lightmapped=<n>/<components> vlm=<0|1>` and passes only with all three positive (E0.1 d); exits non-zero on any budget breach of ADR §8; writes `Saved/Audit/<Map>.json`.
- [ ] `Velox.Maps.LightingBuilt` tightened per E0.1 (d): for every non-exempt `MapsToCook` map, `MapBuildData != nullptr` **and** ≥ 1 `UStaticMeshComponent` of the persistent level whose `MapBuildDataId` resolves via `UMapBuildDataRegistry::GetMeshBuildData()` to an `FMeshMapBuildData` with non-null `LightMap` **and** `GetLevelPrecomputedVolumetricLightmapBuildData(Level->LevelBuildDataId) != nullptr`; logs `lightmapped=<n>/<components> vlm=<0|1>` per map. Still passes today (`checked 0 shipped maps`). Negative check: temporarily add `L_ItemTest` to `MapsToCook` in a scratch copy of the ini (or remove it from the exempt list in a local build) → the test FAILs with the three figures; revert.
- [ ] `Velox.Maps.DM1Budget` passes against the JSON of the unbaked `L_DM1` (budgets do not depend on the bake) and skips cleanly when the file is absent.
- [ ] Cook dry run of the unbaked map: `UnrealEditor-Cmd "$VELOX_PROJECT" -run=cook -targetplatform=Linux -map=/Game/Maps/DM1/L_DM1 -iterate -unattended -nopause -nosplash -nullrhi -stdout` succeeds (lighting is not required to cook); cooked size recorded; `Saved/Cooked` pruned. `L_DM1` is **not** added to `MapsToCook` in part A.
- [ ] `docs/content-bootstrap.md` "Lighting bake": the exact Mac commands for the human (`git lfs pull`, `infra/ci/build-mac.sh --bake /Game/Maps/DM1/L_DM1 Production`, expected 1–3 h, what to commit: `L_DM1.umap` and `L_DM1_BuiltData.uasset`, how to check with `audit_map.py --lighting` before pushing), and why the VM cannot (E0.1 evidence, one paragraph).

## Acceptance criteria — part B (human bake, then `dev` verification)
- [ ] The human runs the Production bake on the Apple Silicon Mac and commits/pushes the baked `.umap` + `_BuiltData` (LFS) on the M5 branch; wall time and the `LogLightmassSolver` completion line go into `notes/16-report.md`.
- [ ] `dev` on the VM: `audit_map.py --lighting /Game/Maps/DM1/L_DM1` → `registry=1 lightmapped=<all static mesh components with res > 0>/<components> vlm=1`; add `+MapsToCook=(FilePath="/Game/Maps/DM1/L_DM1")`; `infra/ci/build.sh` → `Velox.Maps.LightingBuilt` and `Velox.Maps.DM1Budget` pass.
- [ ] Headless `L_DM1 -game -nullrhi` logs no "lighting needs to be rebuilt" (`LogStaticLightingSystem` / `MapCheck`) and the soaks of task 15 still PASS with the DM1 bands (one BotRun round and one FFA run as a smoke; the baked data must not change gameplay actors — `audit_map.py --diff-collision L_DM1_Blockout L_DM1` still empty).
- [ ] Cook of the baked map succeeds; cooked size recorded; `Saved/Cooked` pruned; `df -h /` ≥ 15 GB free.
- [ ] `ServerDefaultMap=/Game/Maps/DM1/L_DM1` only when the lead confirms decision 40 at acceptance; `ffa.sh` and the default `nettest.sh` list still pass (they name their maps explicitly).

## Verification steps
1. Part A: `audit_map.py /Game/Maps/DM1/L_DM1 --protrusion --diff-collision /Game/Maps/DM1/L_DM1_Blockout /Game/Maps/DM1/L_DM1 --lighting --json Saved/Audit/L_DM1.json` → table, `registry=1 lightmapped=0/… vlm=0` (unbaked), exit 0 on budgets.
2. Part A: `infra/ci/build.sh` → both map tests listed; the negative check of `LightingBuilt` recorded and reverted.
3. Part A: the cook command; `du -sh Saved/Cooked/Linux`; prune.
4. Part B: after the human's push, `git lfs pull`, `audit_map.py --lighting`, `MapsToCook`, `infra/ci/build.sh`, headless load, cook, smoke soak under the soak lock.

## Report back
Standard format per part; the audit table, the three lighting figures before and after the bake, bake wall time (from the human), cooked size, free disk.

## Notes / outcome
