# 05 — Projectiles, fake projectiles, recorded self-knockback channel

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** todo
**Depends on:** 04

## Goal
Implement ADR 0003 implementation-notes task **`05-projectiles-selfknockback`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxProjectile.*`, `VeloxProjectileSim.*` (new), RL/PG abilities, `Source/Velox/Movement/VeloxMovementComponent.*` + `VeloxNetworkMoveData.*` (EVeloxKnockbackSource, PendingPredictedKnockback {Velocity, Seq}, ConfirmedSelfKnockbackSeq in the response), tests

## Acceptance criteria
- [ ] Server-authoritative projectile replicated once then dormant; owner keeps the fake and hides the real one; constant-velocity sim; splash Q3 falloff with 3-point LOS; owner never direct-hit; self-damage ×0.5.
- [ ] Recorded channel per A17/ADR 0003 §2; `Velox.Combat.ProjectileSim`, `KnockbackSeq` green; M1 `nettest.sh` unchanged; architect review.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
