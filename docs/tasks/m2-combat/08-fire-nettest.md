# 08 — FireRun and RocketJumpRun soak scripts

**Milestone:** M2 Combat
**Agent:** dev
**Status:** todo
**Depends on:** 04, 05, 07

## Goal
Implement ADR 0003 implementation-notes task **`08-fire-nettest`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Content/Data/MoveScripts/FireRun.json`, `RocketJumpRun.json`, `Source/Velox/Movement/VeloxScriptedInputComponent.*` (fire/weapon-select events), `infra/ci/nettest.sh`, `infra/ci/README.md`

## Acceptance criteria
- [ ] `CrouchRun` added to the default `nettest.sh` script list.
- [ ] FireRun: ≥ 9/10 rail hits on a strafing dummy at 100 ms / 2 %, 0 rejected MG shots; RocketJumpRun: ≤ 1 non-echo correction ≤ 5 cm (A17 acceptance); 5/5 rounds.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
