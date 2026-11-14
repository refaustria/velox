# 37 (H15) — Fight stand-off dwell in open areas

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H14 (merged; both touch Bots/*)
**Wave:** bot track (lead-added 2026-11-14 from H13, docs/tasks/m7-maps/33-h13-void-edge-deaths.md notes)

## Goal
After H13, Orbita FFA ×10 at 4 bots fails only `max_dwell_s` 6 (limit 5) in 2 runs: two bots in a fight stand-off in the middle of the hub, not at an edge. The 4 Hz stand-off rule (D30 / task 06b) does not catch them. Make bots break stand-offs in open areas generally (strafe circle, reposition, or give up the fight goal) so dwell stays ≤ 5 s on every map, without making them passive. Evidence: /mnt/oldroot/velox-scratch/m7-h13/.

## Scope
- `Source/Velox/Bots/*` (combat / stand-off logic), `Source/Velox/Tests/VeloxBot*` (+ an FT reproducing the hub stand-off if feasible), `infra/ci/fts.sh` rows, B5 evidence/notes, this file

## Acceptance criteria
- [ ] Root cause with log evidence (why the 4 Hz stand-off rule misses it)
- [ ] Orbita FFA ×10 at 4 bots: max_dwell ≤ 5, all invariants PASS, hazard gate PASS, coverage ≥ 36/37 → tick B5 (e)
- [ ] Fornax FFA ×10 at 3 bots: max_dwell ≤ 5 (H14 saw 6 s once, east gallery, item goal — check whether the same stall logic or a separate item-goal stall); hazard gate still PASS
- [ ] Sacrarium FFA ×10 at 6 bots and DM1 BotRun ×3: no dwell/stuck regression vs H10/H13 numbers; kills per round not lower than before (bots must not become passive)
- [ ] build + automation + fts.sh green

## Notes / outcome
(Filled by the agent.)
