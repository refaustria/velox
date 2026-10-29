# 11 — Gauntlet melee weapon (optional)

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** todo
**Depends on:** none (optional, parallel); ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 1 h; Wave 1

## Goal
Implement ADR 0004 implementation-notes task **`11-gauntlet`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Gauntlet melee weapon (optional). The ADR sections ADR 0004 task 11 (deferred from ADR 0003 to M3); weapon system per ADR 0003 are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- Weapon definition and hitscan ability (`Source/Velox/Weapons/*`), `DA_Weapon_Gauntlet` via the content script, tests
- Weapon table: append, index 0 stays stable
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (ADR 0004 task 11 (deferred from ADR 0003 to M3); weapon system per ADR 0003; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] Slot 1, always in the loadout, no ammo, 50 dmg, 0.4 s, range 80 cm (Q3 32 u), range from the definition.
- [ ] `FireRun`-style headless hit on a dummy at 60 cm, miss at 120 cm.
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
Standard format for the agent type (result, what changed, verification with commands + output tail, open issues). Note explicitly if Unreal was not available to compile.

## Notes
- Optional task: the lead may drop it. Touch nothing outside the weapon files listed by the lead on dispatch; report if it needs more.
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
