# 04 (P4) — Oct, Hazard record, LOS, link-end, drop and pad-clearance rules, free-edge report

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (pure Python; engine gate deferred)
**Depends on:** P3
**Wave:** 0 pipeline

## Goal
Validator extensions: `Oct` (four-box octagon), `Hazard` record and `hazard` box kind, `mapgen.los` with `LOS_BLOCKED`/`LOS_CLEAR`, link ends on ground (>= 45 cm inside the edge, within 5 cm Z), drop rule E3 (d), `pad_arc_clearance` and `PAD_MAX_LAUNCH_SPEED`, the free-edge report. `veloxmap.drop_links()` is not called by `create_map.py`.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/records.py`
- `Tools/editor/mapgen.py`
- `Tools/editor/veloxmap.py`
- `Tools/editor/create_map.py`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.3 (Oct, Hazard, Box.kind hazard), §1.4 (all bullets), §3.4
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [x] `--selftest` has one passing and one failing fixture per rule
- [x] DM1 golden identical (new rules must not change DM1 output; rules DM1 would violate are gated by `RULES`/spec fields, not weakened)
- [x] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
- Records: `Oct(id, kind, mat, cx, cy, d, z0, z1, kit)`, `Hazard(id, type, x0..z1)` in `maps/records.py` with `HAZARD_TYPES`, `DEFAULT_RULES`, `LEGACY_RULES`. Spec fields: `OCTS`, `HAZARDS` (both optional lists), `RULES` (unknown key = error), `LOS_BLOCKED` / `LOS_CLEAR` (entries `(a, b)` or `(a, b, tol)`, tol = allowed fraction of pairs breaking the rule), `POINTS` (`name: (x, y[, floor_z])`, or a list of such tuples = group, labels `name[i]`), `MAJOR_IDS`, `FREE_EDGE_EXEMPT` (box ids), `MAP_PATH` / `ART_MAP_PATH` (now read by `blockout_path` / `art_path`).
- `RULES` keys: walkable_width, door_min/max, ramp_max_deg, pad_zone, pad_land_tol(_z), pad_arc_clearance (default 0 = today; Culmen 60), pad_max_launch_speed, link_end_ground, link_end_inset 45, link_end_z_tol 5, drop_rule, drop_clear 300, drop_wall_ahead 500, drop_pad_dist 300, oct_gap_min 120, free_edge_drop 45, free_edge_report, and the former DM1 literals `pad_count`, `link_counts`, `item_mix` (None = unchecked; other maps declare theirs).
- DM1 is gated by `LEGACY_RULES["DM1"]` (by MAP_ID): its RJ_West/RJ_East start in the 100 cm gap at x = +-650 and six drops break E3 (d); both rules are off for DM1 only, and its old pad/link/item literals live there. Golden byte-identical.
- Oct: four boxes via `veloxmap.octagon()` / `octagon_boxes()`; cylinders r = d/2 (LOS) and 0.541 d (pad arcs, gap rule `oct_gap_min`: gap 0 or >= 120). `Box.kind "hazard"`: no nav influence in `veloxmap.box`. `veloxmap.hazard()` is NOT written (needs the C++ class, task H2); `mapgen.spawn_hazards` calls it and fails clearly if absent; expected count key `hazardvolume`.
- `mapgen.los(L, a, b)` (all pairs clear), `los_pairs`, `check_los`, `longest_sightline`: eye 160 above the source floor to 60 above the target; ramp fills, free-ramp slabs, Octs. Without lists the DM1 rule stays (spawns -> majors blocked). Explicit lists add figures `<spawn>_sightline_m`, `longest_sightline_m`, `los_visible_pct a -> b`.
- Link ends (start and end of every Link): standing surface within 5 cm and a ring at 45 cm that is not below it. Drop E3 (d), pad arc clearance (`pad_arc_clearance_problem`), launch speed, free ramp passage under the slab (`ramp_passage`, figure `<id>_under_w`), free-edge report (`mapgen.free_edges`, CLI `--free-edges`, figure only with `RULES["free_edge_report"]`). `drop_links` is never called by create_map/mapgen (selftest scans the sources).
- Checked against S1's sacrarium layout (not committed here): it fails Drop_LandNE, Drop_LandSE (E3 d) and the start of Drop_LandNE_Arm (1600, 1340, 320: no standing surface).
- Deferred: engine gate (regenerate test maps / DM1, `audit_map.py --dump-actors` diff) under the soak lock: `UnrealEditor-Cmd <Velox.uproject> -run=pythonscript -script="<abs>/Tools/editor/create_map.py --map dm1 --mode blockout"` (deferred: engine reinstall (lead)).
