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

## Review notes from task 01 (architect, 2026-10-25; ADR 0003 amendments B1–B9 are binding)
- **B3:** the explosion applies damage to every registered pawn with `Dist < SplashRadius` and LOS even when `FVeloxDamageMath::RadiusPoints` returns 0 (Q3: 1 damage, no knockback at the edge). `Apply` handles `Damage == 0` after `01b`; do not skip zero-point victims in the projectile.
- Self knockback on the server goes through the same `GE_Damage` path (`bSelf = true`, `bRadius = true`, direction from `RadiusKnockbackDir`) and therefore through `AddKnockback(Server)`; the client's fake uses `FVeloxDamageMath::Apply` + `AddKnockback(Predicted, ShotSeq)` directly — same math, two channels (ADR §3).

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
