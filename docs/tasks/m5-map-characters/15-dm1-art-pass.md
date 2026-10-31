# 15 — DM1 art pass: `create_dm1_map.py --mode art` → `L_DM1`

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 06/07 (blockout accepted with bands), 14 (kit), 16's `audit_map.py` may be developed in parallel but must exist before acceptance; ADR 0006 §2, §5

## Goal
`L_DM1` is generated from the same layout table as the blockout: every box becomes kit instances, the boxes stay as hidden clip geometry, static lights and reflection captures come from the layout's `Light` records, the sky cubemap is set, and the map plays identically to the blockout (collision diff empty, soaks PASS with the DM1 bands).

## Scope — files/modules the agent may touch
- `Tools/editor/create_dm1_map.py` (`--mode art`), `Tools/editor/maps/dm1_layout.py` (the `Light` records and kit mapping hints only — no geometry changes without re-running the blockout gates), `Tools/editor/veloxmap.py` (kit placement helpers: `kit_fill_box`, `clip_box`, `static_point_light`, `static_spot_light`, `sphere_reflection_capture`, `sky_light(cubemap)`), `Content/Maps/DM1/L_DM1.umap` (LFS). **Not** `Config/DefaultGame.ini`: `L_DM1` enters `+MapsToCook` only with the baked map in task 16 part B (ADR 0006 E0.1 c), otherwise `Velox.Maps.LightingBuilt` turns the VM build red during the Mac wait.
- `docs/tasks/m5-map-characters/notes/15-report.md`

## Relevant ADRs / references
- ADR 0006 §2 (art mode, clip-first, protrusion rule, `--diff-collision`), §5 (light rules: all Static, SkyLight with `TC_Sky_DM1`, 6–8 sphere captures at 128, volumetric lightmap cell 200, Lightmass settings), §8 (budgets), §7 (same bands)
- `Tools/editor/README.md` (nav conventions; the kit must not export to nav — `NoCollision` meshes do not)

## Acceptance criteria
- [ ] `--mode art` writes `L_DM1` with: one kit-instance set per blockout box (floors tiled, walls by 400 cm segments, doors from the door pieces, ramps/stairs from the ramp/stairs pieces, trims where the layout says), the blockout boxes as `Clip_*` (`BlockAll`, hidden, nav-affecting), pads/items/spawns/links/nav bounds/KillZ identical to blockout mode, `Light` records as Static point/spot lights (intensity and colour from the layout; no Stationary/Movable), SkyLight static with `TC_Sky_DM1`, 6–8 `SphereReflectionCapture`, `WorldSettings` Lightmass settings per §5 (3 bounces, quality 1, level scale 1, compressed lightmaps, volumetric lightmap detail cell 200).
- [ ] `audit_map.py --diff-collision L_DM1_Blockout L_DM1` → no differences (blocking boxes, gameplay actors, transforms within 1 cm) and `--protrusion` → no kit instance intrudes > 5 cm into walkable space.
- [ ] `infra/ci/navlint.sh /Game/Maps/DM1/L_DM1` → `clusters=0`; `FT_DM1_Reachability` also runs on `L_DM1` (parametrise the FT by map or duplicate the test actor) and passes.
- [ ] Budgets from `audit_map.py L_DM1` within ADR §8 (draw-call proxy: ≤ 25 distinct meshes × ≤ 8 materials; tri count estimate; lightmap MB; texture MB; components with res > 64 ≤ 120).
- [ ] Soaks on `L_DM1` with the DM1 bands: BotRun ×3, BotDuel ×3 PASS; FFA ×10 PASS (idle host, soak lock). `DM1Run` PASS on `L_DM1`.
- [ ] Lighting is **not** baked in this task (task 16 part B, on the Apple Silicon Mac — ADR 0006 E0.1); the map runs in fallback-C state (unbuilt preview lighting) for the soaks here and loads headless with `-game -nullrhi` clean. `L_DM1` is **not** added to `+MapsToCook`.

## Verification steps
1. Generate both modes; `audit_map.py --diff-collision …` and `--protrusion`.
2. `navlint.sh`, `fts.sh`.
3. Soaks as listed; keep the `Saved/CI` paths.
4. `du -sh Content/Maps/DM1`, `df -h /`.

## Report back
Standard format; the audit table, the soak summary lines, the kit instance count.

## Notes / outcome
