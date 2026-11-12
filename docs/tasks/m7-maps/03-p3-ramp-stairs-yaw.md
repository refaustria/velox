# 03 (P3) — Ramp/Stairs extensions, yaw in veloxmap, per-map clip

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
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
- [ ] `--selftest` covers four ramp directions, `z0`, `fill`, Y stairs, 20 and 30 degree ramps
- [ ] Test maps regenerate with unchanged actor counts
- [ ] DM1 golden check green
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
