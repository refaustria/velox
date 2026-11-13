# 33 (H13) — Bots fall off narrow decks into the void + B5b Orbita re-soak

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H12 (merged), H11 (merged; B5 ran before it)
**Wave:** bot track (lead-added 2026-11-13 from B5)

## Goal
B5 measured 182 self void deaths in FFA ×10 at 4 bots on L_Orbita_Blockout (gate Σ ≤ 10) and 10/19/22 per BotDuel round (gate 2). 42 were at the Pons Fractus jump gap (Jump 5 ok vs 137 aborted — B5 ran before H11's JUMP fix; re-measure first). The rest fall off the narrow decks (Hortus, Horreum, Armamentarium: 27 each) while hopping or strafing; 94 had an item goal, 85 a fight goal; airborne recovery fires but does not save them. Fix the edge behaviour (H4 overlay, H8 hop guard, H5/H8 recovery) so bots stay on narrow decks; then B5b: the full Orbita soak sets with ADR 0008 §5.3 (e) + hazard gate.

## Scope
- `Source/Velox/Bots/*`, `Source/Velox/Tests/VeloxBot*`, `infra/ci/fts.sh` rows, `docs/adr/evidence/0008-orbita-blockout/`, the B5 task file notes, this file

## Acceptance criteria
- [ ] Root cause per death class (gap / deck edge hop / deck edge strafe / knockback) with log evidence
- [ ] Orbita FFA ×10 at 4 bots: Σ hazard_death_self ≤ 10, credited reported; all map-independent invariants PASS; coverage ≥ 36/37
- [ ] BotDuel ×3 on Orbita: hazard_death_self ≤ 2 per round (behaviour; cost windows: A/B rule)
- [ ] FtBotEdge/LavaAvoid/LavaEscape/KnockOff/GapJump/Targets unchanged green; DM1 BotRun ×3 behaviour PASS; build + automation + fts.sh green
- [ ] B5 (e) ticked if Orbita passes

## Notes / outcome
(Filled by the agent.)
