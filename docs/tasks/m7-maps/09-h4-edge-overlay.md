# 09 (H4) — Edge-aware overlay and hop landing guard

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H2
**Wave:** hazard track

## Goal
Edge probe in the combat overlay (`EdgeProbeCm` 150, mirrored lateral, drop lateral/retreat) and the hop landing guard, both active only when `UVeloxHazardSubsystem::HasHazards()`.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotCombat.*`
- `Source/Velox/Bots/VeloxBotPathFollowingComponent.*`
- `Source/Velox/Bots/VeloxBotSettings.h`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §3.6 items 1-2, ADR 0005 §5/§10 (D17, D18)
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `FtBotEdge` green
- [ ] BotRun x 3 on `L_DM1_Blockout` inside its bands (proves DM1 behaviour unchanged by the `HasHazards()` gate)
- [ ] New tunables in `UVeloxBotSettings`, no magic numbers
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
