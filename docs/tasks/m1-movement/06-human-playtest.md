# 06 — Human playtest checklist and sign-off

**Milestone:** M1 Movement
**Agent:** dev
**Status:** todo
**Depends on:** 02–05, Mac UE 5.8 install

## Goal
Implement ADR 0002 task **`06-human-playtest`** exactly as specified in `docs/adr/0002-movement-component-design.md` → "Implementation notes for dev-hard", item 6. The ADR section references in that item (§1 tuning table, §2 override table, §3 FVeloxMoveSim, §5 prediction, §7 tests) are the specification; this file only pins scope and acceptance.

## Scope — files/modules the agent may touch
- `docs/playtests/m1-movement-checklist.md` (new)

## Relevant ADRs / references
- `docs/adr/0002-movement-component-design.md` (primary), `docs/adr/0001-project-and-module-structure.md` §3
- Engine source for API verification: `/opt/UnrealEngine-5.8/Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`
- Rules for all M1 tasks: never bypass the saved-move path; no engine-field magic numbers outside `SetMovementSettings`; report ADR disagreements instead of deviating.

## Acceptance criteria
- [ ] Checklist per ADR item 6: Mac PIE with dedicated server + 2 clients at `Net PktLag=100 PktLoss=2`; strafe corridor to ≥ 1.4× ground speed, bunny hop across the three gaps, all pads, ramp slide 30°/45°, stairs at full speed, `TestKnockback` rocket-jump height, no visible correction jitter.
- [ ] Human sign-off recorded in `STATUS.md` (M1 done criterion).

## Verification steps
1. `infra/ci/build.sh` ends with `[ci] CI OK` (Editor + Game compile, all `Velox.*` automation tests green).
2. Task-specific checks listed above, with command output in the report.

## Report back
Standard format for the agent type; include the design summary (dev-hard) so `architect` can review without the diff.
