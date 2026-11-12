# 08 (H3) — Feed labels, runner metrics, nav-lint exemption

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
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
- [ ] `navlint.sh L_HazardTest` PASS with `hazard_m2 > 0`; `NavLintIgnore` still forbidden
- [ ] Runners print the three metrics
- [ ] DM1 `--reeval` unchanged
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
