# 01 (P1) — DM1 golden dump before any generator change

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** none
**Wave:** 0 pipeline

## Goal
Freeze what the DM1 generator produces today so the pipeline split in P2 can be proven byte-identical. Produces the golden JSON from the UNMODIFIED `create_dm1_map.py` and the actor dumps of both committed DM1 maps.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/golden/dm1.json` (new)
- `Tools/editor/audit_map.py` (add `--dump-actors`)
- `docs/adr/evidence/0008-dm1-golden/` (new)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.5 (gates 1 and 3), §1.1
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `maps/golden/dm1.json` written from the unmodified generator (sorted validator `figure` lines, `all_solids`, art plan tuples, `expected_counts`, `art_counts`, every pad's simulated landing; floats via `repr`)
- [ ] `audit_map.py --dump-actors` exists (class, `Layout:` tag, transform, scale, mesh, materials, collision profile, nav flags per actor, sorted, text) and dumps of the committed `L_DM1_Blockout` and `L_DM1` are stored under the evidence directory
- [ ] No `.umap` changes; no generator behaviour change
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
