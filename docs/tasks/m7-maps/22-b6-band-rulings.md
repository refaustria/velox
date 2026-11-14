# 22 (B6) — Band rulings, one per map as its sets arrive

**Milestone:** M7 Maps (ADR 0008)
**Agent:** architect
**Status:** open
**Depends on:** B1-B5
**Wave:** 2 build (maps WT, serial)

## Goal
Derive bands for each map from its blockout soak evidence by the E3 method, as sets arrive; band `hazard_death_self` for Fornax and Orbita, replacing the provisional gates.

## Scope — files/modules the agent may touch
- `infra/ci/bands/L_<Name>.sh` (+ `L_<Name>_Blockout.sh` sourcing it)
- `docs/adr/0008-m7-five-maps.md` (G-amendments)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §5.3 (bands paragraph and hazard_death_self note), §3.5; ADR 0006 E3 method
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] Band file per map, derived by the E3 method
- [ ] `hazard_death_self` banded for Fornax and Orbita
- [ ] G-amendments recorded in ADR 0008
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Lead notes for B6
- ffa.sh flags `allunreach_air` (bot airborne with no reachable goal after a launch) in about half the Nemus B2b runs; not an invariant — decide whether to band it (2026-11-14).

- Fornax item coverage in FFA ×10 at 3 bots is 27–28/30 with different minor items missing per set (B4, B4b). Rule whether a Duel map's FFA coverage gate should count minor items, or move them (2026-11-14).

## Notes / outcome
(Filled by the agent or the lead after completion.)
