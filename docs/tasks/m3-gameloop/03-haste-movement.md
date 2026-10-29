# 03 — Haste through the predicted move simulation

**Milestone:** M3 Game loop
**Agent:** dev-hard
**Status:** done (merged f32cff9; architect-reviewed, docs/reviews/m3-gameplay-review.md)
**Depends on:** 01 (parallel with 02); ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 2-3 h; Wave 2

## Goal
Implement ADR 0004 implementation-notes task **`03-haste-movement`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Haste through the predicted move simulation. The ADR sections §3 Haste (move timeline), §10 HasteRun soak are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Source/Velox/Movement/*` - `FVeloxPowerupMoveTimes`, response container + `FVeloxCorrectionState`, `ServerRefreshPowerupMoveTimes`, `IsPowerupActive`, `FVeloxMoveInput::SpeedScale` in the sim and `GetMaxSpeed`, `bSavedHasteActive`, `HasteSpeedScale` on `UVeloxMovementSettings` (+ bootstrap field), non-shipping cheat `velox.move.Haste <seconds>`
- `Source/Velox/Weapons/VeloxFireTiming.h`, `VeloxFireRateBucket.h`, `VeloxGameplayAbility_Fire.cpp` - fire-interval scale (client rule / server bucket rule)
- `Source/Velox/Character/VeloxCharacter.*`
- `Content/Data/MoveScripts/HasteRun.json`, `infra/ci/nettest.sh`
- `Source/Velox/Tests/VeloxMoveSimTest.cpp`, `VeloxSavedMoveTest.cpp`, `VeloxWeaponTests.cpp`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§3 Haste (move timeline), §10 HasteRun soak; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] `Velox.Movement.HasteScale` green (sim: wish-speed caps x 1.3, air strafe cap unchanged, `IsPowerupActive` wrap cases, saved moves across the end do not combine).
- [ ] `Velox.Weapons.HasteFireRate` green (interval / 1.3 on the client rule, bucket refill on the server rule).
- [ ] M1/M2 soak list unchanged.
- [ ] `HasteRun` 5/5 per §10: pick up Haste, strafe the corridor: speed >= 1.3 x baseline on client and server logs, at most 1 non-echo correction <= 30 cm in `[t_pickup, t_pickup + 0.3]`, 0 corrections in the 2 s around expiry.
- [ ] `architect` review pass (movement and response container touched).
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
- Engine facts to verify: `FCharacterMoveResponseDataContainer::Serialize` (ack and correction share one serializer), `UCharacterMovementComponent::MinTimeBetweenTimeStampResets`.
- Movement state enters only through saved moves and the response container (ADR 0002 A17/A18/A24, M1 netcode lessons: chain derived state, partition invariance). Soak only on an idle machine; port assigned by the lead.
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
