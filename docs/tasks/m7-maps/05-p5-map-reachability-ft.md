# 05 (P5) — FtMapReachability, fts.sh rows, --bots N

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** none
**Wave:** 0 pipeline

## Goal
Make the DM1 reachability FT map-generic: `velox.bots.FtMapReachability [WalkExempt=<item ids>]` with the old command name kept as an alias, plus a `--bots N` option in `ffa.sh` and `nettest.sh` so soak sizes of 3/4/6/8 bots are possible.

## Scope — files/modules the agent may touch
- `Source/Velox/Tests/VeloxMapReachabilityFt.cpp` (new; replaces the DM1-only FT source)
- `infra/ci/fts.sh`
- `infra/ci/ffa.sh`
- `infra/ci/nettest.sh`
- `infra/ci/README.md`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.6, §5.2
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `fts.sh` 7/7 as before via the alias
- [ ] Stored DM1 soak sets `--reeval` unchanged
- [ ] Build green (Editor + Game); runs under the soak lock
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
