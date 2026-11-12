# 10 (H5) — Airborne recovery, pad discipline, hazard escape

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H4
**Wave:** hazard track

## Goal
In-flight steering toward the nearest nav point at or below the bot's height, pad discipline on `JUMP_PAD` links, and the lava escape that runs ahead of `UpdateOffMeshRecovery`; gated by `HasHazards()`.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotController.*`
- `Source/Velox/Bots/VeloxBotManeuver.*`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §3.6 items 3-4, ADR 0005 (D17)
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `FtBotLavaEscape` green
- [ ] A bot knocked off the test platform edge with 300 cm/s lands back in >= 8 of 10 seeded trials
- [ ] DM1 bands unaffected (BotRun x 3 spot check)
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
