# 04 (P4) — Oct, Hazard record, LOS, link-end, drop and pad-clearance rules, free-edge report

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
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
- [ ] `--selftest` has one passing and one failing fixture per rule
- [ ] DM1 golden identical (new rules must not change DM1 output; rules DM1 would violate are gated by `RULES`/spec fields, not weakened)
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
