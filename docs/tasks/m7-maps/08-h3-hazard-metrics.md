# 08 (H3) — Feed labels, runner metrics, nav-lint exemption

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (notes filled by M7 H9, 2026-11-14)
**Depends on:** H2
**Wave:** hazard track

## Goal
Kill-feed labels for the hazard tags, runner metrics `hazard_touch`, `hazard_death_credited`, `hazard_death_self` per bot and per set, `VeloxNavCoverage` skipping samples inside a hazard volume (reported as `hazard_m2=`), and verification that a band file with no `BAND_*` value stays report-only.

## Scope — files/modules the agent may touch
- `Source/Velox/UI/VeloxHUDFormat.h`
- `Source/Velox/Bots/VeloxNavCoverage.cpp`
- `infra/ci/nettest.sh`
- `infra/ci/ffa_summary.py`
- `infra/ci/ffa_set.py`
- `infra/ci/lib.sh`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §3.5 (metrics, nav-lint, band file), §3.3 (tags, log line)
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [x] `navlint.sh L_HazardTest` PASS with `hazard_m2 > 0`; `NavLintIgnore` still forbidden
- [x] Runners print the three metrics
- [x] DM1 `--reeval` unchanged
- [x] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
Code merged earlier (hazard review F15: the file was never filled). Recorded by M7 H9 (2026-11-14):
- **DM1 `--reeval` unchanged.** Stored DM1 BotRun/BotDuel round `/mnt/oldroot/velox-scratch/m7-h4/20261113-004246-317275/nettest` re-evaluated with the
  current `nettest.sh --reeval <dir> --map L_DM1_Blockout` and diffed against H3's stored pre-metrics output (`m7-h3/reeval-base.txt`): the only differences
  are the new `hazard_touch=0 hazard_death_credited=0 hazard_death_self=0` lines (2 round lines, 2 set lines); every verdict, band and number is byte-identical
  (the stored round still FAILs cost, as before). Output: `docs/adr/evidence/0008-hazard-track/h9-dm1-reeval.txt`.
- Metrics print per round and per set (`nettest.sh`), per run and per set (`ffa_summary.py`, `ffa_set.py`), from `VeloxHazard` / `VeloxKill` lines, per victim label.
  Judged since M7 H9 only when a band file sets `BAND_HAZARD_SELF_*` (ADR 0008 5.3 gate).
