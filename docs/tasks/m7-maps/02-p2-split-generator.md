# 02 (P2) — Split the generator, shim, --golden

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** P1
**Wave:** 0 pipeline

## Goal
One generator for all maps: record types move to `maps/records.py`, `L` becomes a parameter everywhere, CLI `create_map.py --map <slug> [--mode] [--validate-only] [--golden check|write] [--selftest]`, `create_dm1_map.py` becomes a shim, `VELOX_DM1_MODE` is still honoured.

## Scope — files/modules the agent may touch
- `Tools/editor/mapgen.py`
- `Tools/editor/create_map.py` (new)
- `Tools/editor/create_dm1_map.py` (becomes a 10-line shim)
- `Tools/editor/maps/records.py` (new)
- `Tools/editor/maps/dm1_layout.py` (import line only)
- `Tools/editor/README.md`
- `infra/ci/build.sh` (golden check before the build)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.1, §1.2, §1.5
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `create_map.py --map dm1 --golden check` byte-identical to P1's golden
- [ ] Regenerated DM1 maps: `audit_map.py --dump-actors` diff against P1 evidence empty and `--diff-collision` empty; committed `.umap` files unchanged
- [ ] `infra/ci/build.sh` runs the golden check for every map that has a golden file
- [ ] Engine run (regeneration) under the soak lock
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
