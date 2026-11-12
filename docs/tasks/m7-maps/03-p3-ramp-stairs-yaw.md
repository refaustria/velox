# 03 (P3) — Ramp/Stairs extensions, yaw in veloxmap, per-map clip

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (engine gate deferred)
**Depends on:** P2
**Wave:** 0 pipeline

## Goal
Add `Ramp(dir, deg, z0, fill)`, `Stairs(axis, z0, tread, riser)`, `mapgen.ramp_frame`/`stairs_frame`, yawed `veloxmap.ramp`/`stairs`, `clip_walls(mode=)` and `CLIP_H` as a default argument fed from the spec module. New fields are trailing with defaults; the three test-map scripts keep their old call signatures.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/records.py`
- `Tools/editor/mapgen.py`
- `Tools/editor/veloxmap.py`
- `Tools/editor/create_map.py`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.3 (Ramp, Stairs, veloxmap, clip), §1.2
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [x] `--selftest` covers four ramp directions, `z0`, `fill`, Y stairs, 20 and 30 degree ramps
- [ ] Test maps regenerate with unchanged actor counts (deferred: engine reinstall (lead); the three scripts call `vm.ramp/stairs/clip_walls` with unchanged signatures and defaults)
- [x] DM1 golden check green
- [x] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
- `Ramp(id, x0, y0, y1, rise, dir="+x", deg=None, z0=None, fill="solid")`, `Stairs(..., axis="x", z0=None, tread=None, riser=None)` in `maps/records.py`; `rise` is the height gained above `z0` (identical to the top height for DM1).
- `mapgen.ramp_frame(L, r)` / `stairs_frame(L, s)` / `ramp_yaw` / `ramp_surface_z` are the only readers of the fields; expand_*, validator (angle < 45.5, per-ramp `<id>_deg` figure only when `deg` is set so the DM1 golden is unchanged; `fill="none"` demands >= 160 clear under the high end and >= 170 wide), cover, art plan, audit boxes and spawning all go through them. The full under-slab passage test of D41 is left to P4.
- `veloxmap.ramp(..., yaw=)`, `ramp_center`, `stairs(..., axis, direction, z0)`, `stairs_boxes`, `clip_walls(..., mode="box"|"walls"|"none")`, `CLIP_H` fed by `configure(clip_h=)` from the spec module's `CLIP_H` (`create_map.py` passes it). Spec `CLIP_MODE` (+ `CLIP_HALF`, optional `CLIP_WALL_T`) spawns perimeter clip via `mapgen.spawn_perimeter_clip`; absent (DM1) = CLIPS records only; `expected_counts` adds 5/4/0.
- `create_map.py --selftest` now runs `selftest_p3()` (four directions, z0, fill, Y stairs, 20/30 deg, plates, veloxmap arithmetic under a stubbed `unreal`) and `selftest_spawn_smoke()` (build_map / build_art_map against a mocked editor).
- Deferred: regenerate the three test maps in UE and compare actor counts: `UnrealEditor-Cmd <Velox.uproject> -run=pythonscript -script=Tools/editor/create_movement_test_map.py ...` (likewise item / combat test map) under the soak lock.
