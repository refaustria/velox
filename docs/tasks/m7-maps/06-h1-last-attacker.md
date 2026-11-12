# 06 (H1) — Last attacker, tags, settings, VeloxHazardRules

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** none
**Wave:** hazard track

## Goal
Server-only last-attacker record on the health component (`FVeloxLastAttacker`), native tags `Velox.Damage.Hazard{,.Lava,.Void}`, settings `LavaDamagePerTick=25`, `LavaTickSeconds=0.25`, `HazardKillCreditSeconds=5.0`, and a world-free `VeloxHazardRules` for the credit decision. Hazard tags change the net version: note it.

## Scope — files/modules the agent may touch
- `Source/Velox/Combat/VeloxHealthComponent.*`
- `Source/Velox/Combat/VeloxHazardRules.h` (new, world-free)
- `Source/Velox/Combat/VeloxCombatSettings.h`
- `Source/Velox/Core/VeloxGameplayTags.*`
- `Source/Velox/Tests/`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §3.2, §3.3 (tunables, tags), Context 7 (net version)
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] Automation tests: window edge (inside/outside 5 s), self damage ignored, hazard damage ignored as attacker source, attacker gone (weak pointer invalid)
- [ ] Build green; existing combat tests unchanged
- [ ] Runs in the C++ worktree under the soak lock
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)
