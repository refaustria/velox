# M7 Maps — five blockouts (ADR 0008)

**Goal:** five blockouts per ADR 0008 (`docs/adr/0008-m7-five-maps.md`): Sacrarium, Nemus, Culmen, Fornax, Orbita, built in that order, each as `L_<Name>_Blockout` through one shared generator. "Blockout done" is ADR 0008 §5.3. The kit, art pass and Mac work follow after a map's blockout is done.
Concepts: `docs/maps/{sacrarium,culmen,orbita,nemus,fornax}.md`.

## Rules
- UE-heavy work is serial under the soak lock (`flock -o <scratchpad>/soak.lock`). At most **two** worktrees with a compiled module exist at a time: one "maps" integration worktree (generate, lint, FT, soak) and one C++ worktree for the hazard and bot track.
- Pure-Python work (S1-S5, and the Python parts of P-tasks) needs no editor and may run in parallel.
- Every task ends with `create_map.py --map dm1 --golden check` green. A `.umap` is committed only with a change of its layout module or generator output.
- Every agent checks `df -h /` first and reports DISK PAUSE below 5 GB. Raw soak logs go to `/mnt/oldroot/velox-scratch/<task>/`; repo evidence <= 5 MB per map.
- Blockouts are not added to `MapsToCook` or the public rotation; the lead does that when the first Mac playtest package is cut. Hazard tags change the net version: ship server and client downloads together on the next production deploy.
- A build task that fails an invariant after one layout iteration is split by the lead (escalation rule); B1 and B2 move to `dev-hard` on link-success failure.

## Waves and dependency graph
```
Wave 0 pipeline:  P1 -> P2 -> P3 -> P4         P5 (independent)
Hazard track:     H1 -> H2 (also needs P4) -> H3, H4 -> H5 -> H6    (parallel to waves 0-2; merged before B4)
Wave 1 specs:     S1 (needs P3); S2..S5 (need P4)     parallel, pure Python
Wave 2 builds:    B1 (S1,P5) -> B2 (S2,P5) -> B3 (S3,P5) -> B4 (S4,H5,H3) -> B5 (S5,H5,H3)    serial
                  B6 (architect) as each map's sets arrive
```

## Tasks (22: 13 dev, 7 dev-hard, 2 architect)
| Id | Task | Tier | Wave | Depends | File |
|---|---|---|---|---|---|
| P1 | DM1 golden dump before any generator change | dev | 0 pipeline | none | `01-p1-dm1-golden.md` |
| P2 | Split the generator, shim, --golden | dev | 0 pipeline | P1 | `02-p2-split-generator.md` |
| P3 | Ramp/Stairs extensions, yaw in veloxmap, per-map clip | dev | 0 pipeline | P2 | `03-p3-ramp-stairs-yaw.md` |
| P4 | Oct, Hazard record, LOS, link-end, drop and pad-clearance rules, free-edge report | dev | 0 pipeline | P3 | `04-p4-oct-hazard-rules.md` |
| P5 | FtMapReachability, fts.sh rows, --bots N | dev | 0 pipeline | none | `05-p5-map-reachability-ft.md` |
| H1 | Last attacker, tags, settings, VeloxHazardRules | dev-hard | hazard track | none | `06-h1-last-attacker.md` |
| H2 | AVeloxHazardVolume, UVeloxHazardSubsystem, L_HazardTest, FtHazard | dev-hard | hazard track | H1, P4 | `07-h2-hazard-volume.md` |
| H3 | Feed labels, runner metrics, nav-lint exemption | dev | hazard track | H2 | `08-h3-hazard-metrics.md` |
| H4 | Edge-aware overlay and hop landing guard | dev-hard | hazard track | H2 | `09-h4-edge-overlay.md` |
| H5 | Airborne recovery, pad discipline, hazard escape | dev-hard | hazard track | H4 | `10-h5-airborne-recovery.md` |
| H6 | Design review of H1-H5 | architect | hazard track | H5 | `11-h6-hazard-review.md` |
| S1 | Sacrarium spec module (pure Python) | dev | 1 spec modules (parallel) | P3 | `12-s1-sacrarium-spec.md` |
| S2 | Nemus spec module (pure Python) | dev | 1 spec modules (parallel) | P4 | `13-s2-nemus-spec.md` |
| S3 | Culmen spec module (pure Python) | dev | 1 spec modules (parallel) | P4 | `14-s3-culmen-spec.md` |
| S4 | Fornax spec module (pure Python) | dev | 1 spec modules (parallel) | P4 | `15-s4-fornax-spec.md` |
| S5 | Orbita spec module (pure Python) | dev | 1 spec modules (parallel) | P4 | `16-s5-orbita-spec.md` |
| B1 | Sacrarium blockout: build, bot iteration, evidence | dev | 2 build (maps WT, serial) | S1, P5 | `17-b1-sacrarium-build.md` |
| B2 | Nemus blockout: build, bot iteration, evidence | dev | 2 build (maps WT, serial) | S2, P5 | `18-b2-nemus-build.md` |
| B3 | Culmen blockout: build, bot iteration, evidence | dev-hard | 2 build (maps WT, serial) | S3, P5 | `19-b3-culmen-build.md` |
| B4 | Fornax blockout: build, bot iteration, evidence | dev-hard | 2 build (maps WT, serial) | S4, H5, H3 | `20-b4-fornax-build.md` |
| B5 | Orbita blockout: build, bot iteration, evidence | dev-hard | 2 build (maps WT, serial) | S5, H5, H3 | `21-b5-orbita-build.md` |
| B6 | Band rulings, one per map as its sets arrive | architect | 2 build (maps WT, serial) | B1-B5 | `22-b6-band-rulings.md` |

## Mac-only items
Lighting bakes, real draw-call and frame-time numbers, contrast shots and human playtests need the Mac Studio (about 2026-11-20). Nothing in "blockout done" waits for it.

## Later tasks (headline only, no files yet; detailed when scheduled)
- **K1** (dev) Kit themes in `kit.py`, `create_kit_materials.py`, `import_kit.py`; palette classes; custom-primitive-data tint; four shared core pieces.
- **K2** (dev) `plan_emissive`, `audit_map.py --emissive`, `infra/ci/lfs_budget.sh`.
- **K3-K7** (dev) One theme kit each (pieces, eight instances, textures, sky cubemap).
- **A1-A5** (dev; Culmen, Orbita: dev-hard) Art mode per map: `mat -> MI` table, tile policy, lights and captures from records.
- **M1** (human + dev) Mac Studio: Production bakes, `stat` numbers from each map's worst view, contrast shots, playtest checklists.
