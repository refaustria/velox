# 15 (S4) — Fornax spec module (pure Python)

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** P4
**Wave:** 1 spec modules (parallel)

## Goal
Write the Fornax layout as a spec module from its concept. Pure Python, no editor, no build; may run in parallel with the other spec tasks. Deviations from a concept's coordinates are allowed where the validator demands them (the graph and the rules are binding, ADR 0006 §3); list each in the task notes.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/fornax_layout.py`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.2 (spec contract), §1.3-1.4, §2.1 (theme pieces), §4 (Fornax decisions), §5.3; concept `docs/maps/fornax.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `create_map.py --map fornax --validate-only` green
- [ ] Hazard rules pass
- [ ] Island gaps 250, rim 60
- [ ] Deviations from the concept listed in the task notes
- [ ] Golden file is NOT written here (the B-task does it)
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
