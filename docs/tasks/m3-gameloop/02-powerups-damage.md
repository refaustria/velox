# 02 — Powerups (Quad, Regen, Battle Suit), damage math, MatchFrozen GE

**Milestone:** M3 Game loop
**Agent:** dev-hard
**Status:** todo
**Depends on:** 01; ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 2-3 h; Wave 2 (parallel with 03)

## Goal
Implement ADR 0004 implementation-notes task **`02-powerups-damage`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Powerups (Quad, Regen, Battle Suit), damage math, MatchFrozen GE. The ADR sections §3 except Haste movement are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/Items/VeloxPowerupEffects.*` - `UVeloxGE_Powerup` + four subclasses, `UVeloxRegenExecution`
- `Source/Velox/Combat/VeloxDamageMath.h`, `VeloxDamageExecution.*` - Battle Suit, Quad, MatchFrozen treated like Dead
- `Source/Velox/Combat/VeloxGameplayEffects.*` - `GE_SpawnInit` removes `Velox.Powerup`, decay skip, `UVeloxGE_MatchFrozen` + tag (used by 05)
- `Source/Velox/Combat/VeloxCombatSettings.*` - `QuadDamageFactor`, `BattleSuitDirectDamageScale`, `Regen*`, `HasteFireRateScale`
- `Source/Velox/Weapons/VeloxGameplayAbility_Fire*.cpp`, `VeloxProjectile.*` - `DamageScale`/Quad at fire (hitscan server tag, projectile `bQuad`)
- `Source/Velox/Weapons/VeloxCombatFeedbackComponent.*` - body tint per powerup tag
- `Source/Velox/Tests/VeloxCombatTests.cpp`, `VeloxItemTests.cpp`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§3 except Haste movement; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] Extension of an active powerup via `ModifyActiveEffectStartTime` (verified) with fallback.
- [ ] Extended `Velox.Combat.DamageMath` green (Quad x3 rows incl. self: 300 raw -> 150 self -> 1250 x 3 cm/s knockback; Battle Suit: radius -> 0/0 with knockback 1250, direct 100 -> 50 -> vs 100 armor -17/-33; Regen table 50 -> 65 -> ... -> 110 cap, 150 -> 155).
- [ ] `Velox.Items.PowerupStacking` green (extension arithmetic, lost on `GE_SpawnInit` - ASC test world like `FDeathTestWorld`).
- [ ] Headless: Quad pickup -> `VeloxShot` damage x3 on a dummy; Regen 50 -> 65 per second in the log.
- [ ] If 05 is not merged yet, `UVeloxGE_MatchFrozen` is defined here (05 adopts it), or vice versa; coordinate via the lead.
- [ ] `infra/ci/build.sh` green (`[ci] CI OK`, all `Velox.*` tests).

## Standing rules
- The nettest port is assigned by the lead; pass it via `nettest.sh --port <N>`. Do not pick your own. Soak only on an idle machine.
- Unity-build safe names: no file-scope `using namespace`, no duplicate file-local helper names across `.cpp` files (use anonymous namespaces with distinctive names).
- Tunables live in data assets (`UVeloxItemDefinition`, `UVeloxGameModeDefinition`, `UVeloxCombatSettings`, `UVeloxMovementSettings`, `UVeloxUISettings`, `UVeloxGameUserSettings`) or `.ini`, never as magic numbers.
- Every change is covered by a test (automation test or functional test).
- No commit by the agent; the lead commits. Leave the project compiling; end with `infra/ci/build.sh` showing `[ci] CI OK` (and `nettest.sh` where named). State explicitly if Unreal was unavailable.
- Report ADR disagreements instead of deviating.

## Verification steps
1. `infra/ci/build.sh` -> `[ci] CI OK`.
2. The headless/automation/soak runs named in the acceptance criteria, with the lead-assigned nettest port.

## Report back
Standard format for the agent type (result, what changed, verification with commands + output tail, open issues). Include a design summary for `architect` review. Note explicitly if Unreal was not available to compile.

## Notes
- Engine facts to verify in 5.8 source: `UAbilitySystemComponent::ModifyActiveEffectStartTime(Handle, StartTimeDiff)` (fallback if missing in 5.8: remove and re-apply with `Remaining + PowerupSeconds`); `FGameplayEffectSpec::CapturedSourceTags` (captured from the source ASC when the spec is made).
- Mixed replication: owner holds the full spec, everyone else gets the tag (`MinimalReplicationTags`). Powerups are lost on death without being dropped (open question 6).
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
