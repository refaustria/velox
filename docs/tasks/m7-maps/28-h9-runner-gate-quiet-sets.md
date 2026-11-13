# 28 (H9) — follow-ups from the H6 hazard review

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** H8
**Wave:** hazard track (lead-added 2026-11-13 from `docs/adr/reviews/0008-hazard-review.md` §Follow-up tasks)

## Goal
F9 (hard `hazard_death_self` gate in runners / band files per ADR 0008 §5.3), F15 (fill H3 task notes, record DM1 `--reeval`), then the F3 and F4 runs on a quiet host after H8 is merged. Defect details, rulings and proposed amendments (G1–G7, D52–D55, C12) are in `docs/adr/reviews/0008-hazard-review.md` — read it first.

## Scope — files/modules the agent may touch
`infra/ci/{nettest.sh,ffa.sh,ffa_summary.py,ffa_set.py,lib.sh}`, `infra/ci/bands/*`, `docs/tasks/m7-maps/08-h3-hazard-metrics.md`, evidence under `docs/adr/evidence/0008-hazard-track/`, this file

## Acceptance criteria
- [ ] hazard gate judged in a dry run on a stored set; default nettest list, BotRun ×3 and BotDuel ×3 on L_ItemTest, FFA ×10 on L_DM1 inside their bands with per-link JumpPad numbers; one cost-judged set on a hazard map; Sacrarium BotRun ×3 (L_Sacrarium_Blockout) on a quiet host; per-map BotWatch idle-client park point (BotWatch.json parks it at (-2700,-1700,100), outside the Nemus and Fornax footprints → 5 world kills per BotRun round there; read the point from the map spec or a per-map override, DM1 unchanged); investigate the start-up segfault seen once in an H11 Nemus FFA run (log missing, see docs/tasks/m7-maps/30-h11-jump-gap-links.md notes). Quiet host = load1 < 1.5 for the run; if other projects keep the host busy, wait/retry with the nettest load gate and record the load.

## Notes / outcome
(Filled by the agent.)
