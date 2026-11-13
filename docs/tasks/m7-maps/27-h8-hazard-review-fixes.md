# 27 (H8) — follow-ups from the H6 hazard review

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H6 review (merged), H7
**Wave:** hazard track (lead-added 2026-11-13 from `docs/adr/reviews/0008-hazard-review.md` §Follow-up tasks)

## Goal
F1, F2, F4 (caches), F5, F6, F7, F8, F11, F12, F13 and ruling 3 (pad zeroing removed: pad-arrive steering replaces zero input on hazard maps too; zero input only when on course). Defect details, rulings and proposed amendments (G1–G7, D52–D55, C12) are in `docs/adr/reviews/0008-hazard-review.md` — read it first.

## Scope — files/modules the agent may touch
`Source/Velox/Bots/*`, `Source/Velox/Combat/VeloxHazardVolume.*`, `Source/Velox/Bots/VeloxNavCoverage.cpp`, `Source/Velox/Tests/VeloxBot*Ft.cpp`, `Source/Velox/Tests/VeloxHazardFt.cpp`, `infra/ci/fts.sh` (new rows), this file

## Acceptance criteria
- [ ] build (Editor+Game) and full automation green; `fts.sh` green incl. a new knock-in-pad-flight case and FtBotKnockOff pad part 10/10; `navlint.sh L_HazardTest` PASS with `hazard_m2` equal to the volume boxes (36 m²); new `VeloxNav hazard_area` lint: count 0 on L_HazardTest and no Fornax item/spawn/link end inside a lava footprint; FtBotEdge, FtBotLavaAvoid seeds 1–5, FtBotLavaEscape unchanged green; DM1 golden OK. **Before B4.**

## Notes / outcome
(Filled by the agent.)
