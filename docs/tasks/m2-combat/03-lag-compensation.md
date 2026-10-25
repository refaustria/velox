# 03 — Server-side lag compensation subsystem

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** done (2026-10-25, review pending)
**Depends on:** 02

## Goal
Implement ADR 0003 implementation-notes task **`03-lag-compensation`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Source/Velox/Net/VeloxLagCompensationSubsystem.*` (new), `Source/Velox/Combat/VeloxHitboxComponent.*` (registration), `Source/Velox/Movement/VeloxMovementComponent.*` (expose SmoothingClientTimeStamp hint data), `Source/Velox/GameModes/VeloxPlayerController.*` (ViewServerTime + hint), tests

## Acceptance criteria
- [ ] Ring buffer per pawn (WorldTime + PawnStamp per sample), `TraceShot` rewinds hinted pawn by stamp and others by world time, MaxRewind 0.25 s, origin snap 75 cm.
- [ ] `Velox.Combat.LagCompRewind` (synthetic history incl. hint-by-stamp) green; architect review.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
