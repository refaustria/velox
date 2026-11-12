# 11 (H6) — Design review of H1-H5

**Milestone:** M7 Maps (ADR 0008)
**Agent:** architect
**Status:** open
**Depends on:** H5
**Wave:** hazard track

## Goal
One-pass design review of the hazard and bot-edge work, defects ranked, D/G amendments written.

## Scope — files/modules the agent may touch
- `docs/adr/reviews/0008-hazard-review.md` (new)
- `docs/adr/0008-m7-five-maps.md` (Amendments section only)
- `docs/adr/0004-*.md`, `docs/adr/0005-*.md` (D-numbers, only if the review writes any)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — ADR 0008 §3 in full; ADR 0004 §6; ADR 0005 §5/§10
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] Review file with ranked defects
- [ ] D/G amendments written where needed; each defect fixed by a proposed follow-up task or explicitly accepted
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
