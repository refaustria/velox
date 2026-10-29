# 01 — Items foundation: definitions, pickup actor, registry, grants

**Milestone:** M3 Game loop
**Agent:** dev-hard
**Status:** todo
**Depends on:** none (starts wave 1; builds on accepted ADR 0001-0003); ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 3 h; Wave 1

## Goal
Implement ADR 0004 implementation-notes task **`01-items-foundation`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Items foundation: definitions, pickup actor, registry, grants. The ADR sections §1, §2 (items, pickup resolution), with §10 tests and §9 registry API are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/Items/*` - `UVeloxItemDefinition`, `VeloxPickupRules.h` (+ `FVeloxPickupTimer`), `AVeloxPickup`, `UVeloxItemRegistrySubsystem`
- `Source/Velox/Weapons/VeloxWeaponInventoryComponent.*` - `Inventory->PredictOwned`, autoswitch hook (reads a `bAutoSwitchOnPickup` bool on the controller until task 09 wires the setting)
- `Source/Velox/Combat/VeloxGameplayEffects.*` - `UVeloxGE_Heal`, `UVeloxGE_ArmorGain`, `MakeAmmoGainSpec`; `PickupPredictionTimeout` on `UVeloxCombatSettings` if not yet present
- `Source/Velox/Core/VeloxGameplayTags.*` - native `Velox.Item.*` / `Velox.Powerup.*` tags
- `Tools/editor/create_item_content.py` - 21 `DA_Item_*` assets
- `Source/Velox/Tests/VeloxItemTests.cpp`
- Cheat `velox.items.Spawn <ItemName>` (in front of the player, for `L_CombatTest`)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§1, §2 (items, pickup resolution), with §10 tests and §9 registry API; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] `AVeloxPickup`: server grant, replication, dormancy, standing-on-respawn, `Reset`, client prediction + reconciliation, `PickupPredictionTimeout`.
- [ ] `Velox.Items.PickupRules` green (the §2 table: health 5 at 199 -> 200, 5 at 200 refused, 25 at 100 refused, 25 at 90 -> 100, Mega at 150 -> 200, shard at 199 -> 200, YA at 150 -> 200, RA at 200 refused, ammo at 200 refused, weapon ammo top-up `40 - 10 = 30`, already 60 -> +1, powerup 30 + remaining 10 -> 40).
- [ ] `Velox.Items.PickupTiming` green (take at t -> unavailable, `TimeRemaining` exact, available at exactly t + 35, initial delay honoured, `Reset`, `ItemRespawnScale`).
- [ ] Headless `L_CombatTest`: spawned Health.Medium refused at 100, taken at 50 -> 75, log `VeloxPickup`, respawns at +35 s, a standing pawn retakes.
- [ ] Rules are world-free functions with a unit test before they are wired; clients never modify attributes.
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
- Pickups are `bAlwaysRelevant`, `DORM_DormantAll` with `FlushNetDormancy()` on every state change; budget in §11.
- Engine fact to verify: `FGameplayEffectSpec::CapturedSourceTags` (captured from the source ASC when the spec is made) - relevant to the grant specs.
- Report ADR disagreements instead of deviating. Weapon pickup ammo rule defaults to the Q3 top-up (open question 5).
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
