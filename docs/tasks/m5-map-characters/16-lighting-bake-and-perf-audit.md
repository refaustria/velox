# 16 — `audit_map.py`, lighting bake of `L_DM1`, cook, default map

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 01 (bake path verdict), 15 (`L_DM1`); the audit tool part can start right after 05; ADR 0006 §5, §8

## Goal
`Tools/editor/audit_map.py` exists (static budgets, collision diff, protrusion, lighting-built check, LFS total; JSON output consumed by `Velox.Maps.DM1Budget`). `L_DM1` is baked at Production quality by path A (VM) or path B (Mac, by the human), the `.umap` with build data is committed, `Velox.Maps.LightingBuilt` passes, the map cooks, and `ServerDefaultMap` moves to `L_DM1`.

## Scope — files/modules the agent may touch
- `Tools/editor/audit_map.py` (new), `Source/Velox/Tests/VeloxMapTests.cpp` (`Velox.Maps.DM1Budget` reads `Saved/Audit/L_DM1.json` if present, else skips with a log), `infra/ci/build.sh` (run the audit after the build when the map exists; non-fatal until `L_DM1` is in `MapsToCook`)
- `Content/Maps/DM1/L_DM1.umap` (baked; LFS), `Config/DefaultEngine.ini` (`ServerDefaultMap`), `Config/DefaultGame.ini` (`+MapsToCook`), `docs/content-bootstrap.md` (bake + audit section), `docs/tasks/m5-map-characters/notes/16-report.md`

## Relevant ADRs / references
- ADR 0006 §5 (paths A/B, Production for acceptance, `LightingBuilt` guard), §8 (budget table and what is measured where), §2 (`--diff-collision`)
- Task 01's verdict and `infra/ci/bake.sh`

## Acceptance criteria
- [ ] `audit_map.py <Map> [--diff-collision A B] [--protrusion] [--lighting] [--json out]`: loads the map headless (`pythonscript` commandlet), reports per-mesh triangle counts (`LODResources[0]` via `unreal.StaticMesh.get_num_triangles(0)` or editor library), distinct meshes/materials, lightmap resolutions and MB estimate (`Σ res² × 4 B × 1.3`), texture MB (all textures referenced by the map's materials), components with lightmap res > 64, translucent materials, presence and population of `UMapBuildDataRegistry` (`--lighting`), `git lfs ls-files -s` total; exits non-zero on any budget breach of ADR §8; writes `Saved/Audit/<Map>.json`.
- [ ] `Velox.Maps.DM1Budget` passes against the JSON of the final `L_DM1` (and skips cleanly when the file is absent).
- [ ] Bake: path A → `infra/ci/bake.sh /Game/Maps/DM1/L_DM1 Production` on an idle host under the soak lock (record wall time, peak RSS, `df` before/after); path B → the human runs `build-mac.sh --bake` and commits; either way `audit_map.py --lighting L_DM1` reports lightmaps for every static mesh component and a volumetric lightmap, and `Velox.Maps.LightingBuilt` passes with `L_DM1` in `MapsToCook`.
- [ ] Cook: `UnrealEditor-Cmd "$VELOX_PROJECT" -run=cook -targetplatform=Linux -map=/Game/Maps/DM1/L_DM1 -iterate -unattended -nopause -nosplash -nullrhi -stdout` succeeds; cooked size of the map and its dependencies recorded; `Saved/Cooked` pruned afterwards.
- [ ] `ServerDefaultMap=/Game/Maps/DM1/L_DM1`; `ffa.sh` and the default `nettest.sh` list still pass (they name their maps explicitly).
- [ ] Headless `L_DM1 -game -nullrhi` logs no "lighting needs to be rebuilt" (`LogStaticLightingSystem`/`MapCheck` lines) after the bake.

## Verification steps
1. `audit_map.py /Game/Maps/DM1/L_DM1 --protrusion --diff-collision /Game/Maps/DM1/L_DM1_Blockout /Game/Maps/DM1/L_DM1 --json Saved/Audit/L_DM1.json` → table + exit 0.
2. Bake as per the path; then `audit_map.py --lighting …`.
3. `infra/ci/build.sh` → `Velox.Maps.LightingBuilt` and `Velox.Maps.DM1Budget` pass.
4. Cook command; `du -sh Saved/Cooked/Linux`; prune; `df -h /` ≥ 15 GB free.

## Report back
Standard format; the audit table, bake path and timings, cooked size, free disk.

## Notes / outcome
