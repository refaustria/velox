# 30 (H11) — Bots fail JUMP links across gaps

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H8 (merged). Runs in parallel with H10 (different code path: link traversal, not combat); merge carefully.
**Wave:** bot track (lead-added 2026-11-13 from B2, docs/tasks/m7-maps/18-b2-nemus-build.md notes). **Blocks B4 (Fornax lava hops are JUMP links) and B2b.**

## Goal
B2 found that bots fail the two JUMP links across the 400 cm log breaks on L_Nemus_Blockout almost always (0/33 in FFA ×10 at 3 bots, 2/47 at 6 bots, 0/11 BotRun); all `link_timeout` stuck strikes sit on them. A probe with 300 cm gaps still gave 0/29, so the cause is bot-side, not map geometry. Root-cause and fix JUMP link execution generally (run-up, take-off point, jump timing, air control toward the link end, link-end acceptance), so gap jumps reachable by Velox movement succeed. Check that `JUMP_FAST` links (Culmen, Nemus) work the same way.

Evidence: docs/adr/evidence/0008-nemus-blockout/, raw logs under /mnt/oldroot/velox-scratch/m7-nemus/.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/*` (path following, link traversal, maneuver), `Source/Velox/Tests/VeloxBot*` (+ a new FT with a 300 and a 400 cm gap, e.g. on a generated test map or a new small one via `Tools/editor/create_*_test_map.py`), `infra/ci/fts.sh` rows
- this file

## Acceptance criteria
- [ ] Root cause stated with log evidence
- [ ] New FT: bot crosses a 300 cm and a 400 cm gap JUMP link in ≥ 9/10 seeded tries each
- [ ] Nemus FFA ×10 at 3 bots: JUMP link success ≥ 90 %, stuck within limit (re-run only that set; full B2b re-soak is a separate task)
- [ ] DM1 BotRun ×3 behaviour PASS; FtBotEdge/LavaAvoid/LavaEscape/KnockOff unchanged green
- [ ] build + full automation + fts.sh green; DM1 golden OK

## Notes / outcome
(Filled by the agent.)
