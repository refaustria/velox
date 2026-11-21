# 06 (H1) — Last attacker, tags, settings, VeloxHazardRules

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done
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
- [x] Automation tests: window edge (inside/outside 5 s), self damage ignored, hazard damage ignored as attacker source, attacker gone (weak pointer invalid)
- [x] Build green; existing combat tests unchanged
- [x] Runs in the C++ worktree under the soak lock
- [ ] `create_map.py --map dm1 --golden check` green at the end (not available yet: P1/P2 not merged on m7-maps; `create_dm1_map.py --validate-only` green instead)

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
dev-hard, 2026-11-12, branch `wip/m7-06-h1-last-attacker`.

- `FVeloxLastAttacker { TWeakObjectPtr<AVeloxPlayerState> Attacker; double ServerTime; FGameplayTag DamageTag; }` is a USTRUCT,
  `UPROPERTY(Transient)`, on `UVeloxHealthComponent`. It is server only, not replicated, and reset in Initialize/UninitializeWithAbilitySystem (new life).
- It is written in `HandleDamaged` (the health set's `OnDamaged`, authority) before `OnDamaged` is re-broadcast, with `UWorld::GetTimeSeconds()`,
  via `NoteDamageForAttackerCredit(Event, Now)`. Instigator → player state (direct, or pawn's PS); victim PS = ASC owner (fallback: owner pawn's PS).
- Rules in `Combat/VeloxHazardRules.h` (world-free): `ShouldRecordAttacker` = other player AND no `Damage.Self` AND neither `Damage.Hazard*`
  nor `Damage.Environment` AND (health loss OR armor loss OR knockback). `IsCredited` = record AND attacker valid AND age < window (strict;
  exactly 5.0 s has expired; negative age counts as 0; window <= 0 credits nobody). Rejected events never clear the record.
- `GetCreditedAttacker(double Now, float WindowSeconds)` for H2; callers pass `HazardKillCreditSeconds`.
- Tags `Velox.Damage.Hazard`, `.Hazard.Lava`, `.Hazard.Void` (native). **Net version changes** (ADR 0007 F2 tag hash): new builds are
  incompatible with deployed 0.6.1 clients/server; ship server and client together on the next production deploy.
- Settings: `LavaDamagePerTick=25`, `LavaTickSeconds=0.25`, `HazardKillCreditSeconds=5.0` (category Velox|Hazards).
- For H2: Quad is applied at fire (`VeloxPowerups::GetOutgoingDamageScale` in the hitscan ability and projectile), not in
  `VeloxDamageExecution.cpp`, so a hazard spec with `DamageScale 1` is not Quad-scaled; FtHazard still has to prove it.
- Tests: `Velox.Combat.HazardRules` (world-free) and `Velox.Combat.LastAttacker` (two-player private world, real GAS path).
- Verification: `infra/ci/build.sh` under the soak lock: VeloxEditor and Velox built, VeloxServer/VeloxClient SKIPPED (installed engine),
  automation 142/142 Success (log `Saved/CI/20261112-202813-3304276`). A requested re-run after the lead restart could not start:
  `/opt/UnrealEngine-5.8` is no longer on the machine. Sources are unchanged since the green run.
