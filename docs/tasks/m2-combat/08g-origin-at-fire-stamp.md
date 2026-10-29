# 08g — Shot origin validated against the eye at `FireMoveTimeStamp` (B36)

**Milestone:** M2 Combat (follow-up)
**Agent:** dev-hard
**Status:** done (merged 81ea0de; 76 tests; FireRunHold late_ms window → architect)
**Depends on:** ADR 0003 B36, ADR 0002 A36 (eye ring paragraph), review `docs/reviews/m2-late-rpc-review.md` (ruling 5, defect 2); 08c/08d merged. Touches `ValidateOrigin` like 08f — rebase on whichever merges first.

## Goal
A shot whose reliable RPC arrives late (one resend ≈ RTT + 80 ms) is no longer snapped to the shooter's *current* eye (`err_cm=108.9` in the round-4 evidence). The server keeps the shooter's eye per processed move and validates the origin at the shot's own move stamp, for hitscan and projectiles; `late_ms` becomes visible in the shot logs. The hitscan rewind clamp is deliberately unchanged.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxShotTargetData.h/.cpp` — `FireMoveTimeStamp` moves to the base struct (serialised full precision after `ShotSeq`, before the hint); `FGameplayAbilityTargetData_VeloxProjectileShot` keeps its type and `ToString`, drops its own field.
- `Source/Velox/Weapons/VeloxShotValidation.h/.cpp` — `FVeloxValidatedShot::FireMoveTimeStamp` for both payloads; `ValidateOrigin(World, Shooter, Eye, ClientOrigin, Tol, OutReason)` overload (the world-free distance/occlusion logic against a given eye), old signature forwards with the live eye.
- `Source/Velox/Movement/VeloxMovementComponent.h/.cpp` — `FVeloxEyeSample`, 128-entry ring pushed after `Super::MoveAutonomous` on authority, `GetServerEyeAtMoveTime(float Stamp, FVector& OutEye) const` (exact / interpolated / newer → false / older → oldest / reset on a lower stamp); a world-free helper (`VeloxEyeRing::Lookup`) for the unit rows.
- `Source/Velox/Net/VeloxLagCompensationSubsystem.h/.cpp` — `FVeloxShotRequest::FireMoveTimeStamp`, `bHasFireMoveTimeStamp`; `TraceShot` l.650–676 resolves the eye through the shooter's `UVeloxMovementComponent` before the origin step; `late_ms` in `VeloxShot` and `VeloxShotOriginSnap`; the "either order" comment replaced; member default `MaxRewindSeconds` 0.30.
- `Source/Velox/Weapons/VeloxGameplayAbility_FireHitscan.cpp` — client payload gets `Movement->GetClientMoveTimeStamp()` (l.163–172); server passes it into the request (l.307–324); `ServerNoteShot` call unchanged.
- `Source/Velox/Weapons/VeloxGameplayAbility_FireProjectile.cpp` — `LaunchServerShot` origin step (l.285–292) uses the stamped eye (RPC fallback path; with 08f the move-shot path validates against the live eye at move time, which needs no ring).
- `Source/Velox/Tests/VeloxLagCompTest.cpp`, `Tests/VeloxWeaponTests.cpp` — rows below.
- `infra/ci/nettest.sh` — FireRun: assert `VeloxShotOriginSnap` 0 (already) and report the max `late_ms`; new script `FireRunHold` = FireRun with `velox.combat.DebugHoldShotRpc 300 7 3` (needs 08f's cvar; if 08f is not merged yet, implement the cvar here in the same form and 08f adopts it) — asserts: 0 `VeloxShotRejected`, 0 `VeloxShotOriginSnap`, the three held rails `late_ms ∈ [280, 360]`; their hits reported only.
- `docs/tasks/m2-combat/notes/08g-report.md`.
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` B36 (the spec), B15, B25, B26, B34; `docs/adr/0002-movement-component-design.md` A36 (eye ring).
- `docs/reviews/m2-late-rpc-review.md` §2 ruling 5, §3 defect 2.

## Acceptance criteria
- [ ] Unit rows (`VeloxEyeRing::Lookup`): exact stamp; stamp between two entries → linear interpolation; newer than the newest → false; older than the oldest → oldest; a push with a lower stamp resets the ring; 130 pushes keep the newest 128.
- [ ] Unit row (`ValidateOrigin` overload): pawn moved 180 cm since the stamped eye; client origin = the stamped eye → no snap; the same origin against the live eye → `reason=distance` (regression proof of the round-4 defect); occlusion trace runs from the stamped eye (a wall between the stamped eye and an origin 30 cm away → `reason=occluded`).
- [ ] Hitscan and projectile payloads carry `FireMoveTimeStamp`; a non-finite value → `reason=time` for both (existing projectile row extended to hitscan).
- [ ] FireRun at the default 50 ms one-way `--repeat 3`: 3/3, `VeloxShotOriginSnap` 0, max `late_ms` ≤ 40 on non-held shots; FireRun `--lag 125` ×3: `VeloxShotOriginSnap` 0, hits ≥ 9/10, 0 rejections (with 08h; without it, quote any rejection and attribute it to B37).
- [ ] `FireRunHold` ×3: 0 `VeloxShotOriginSnap`, 0 `VeloxShotRejected`, held rails `late_ms ∈ [280, 360]`; report their hit/miss and `rewind_ms` (expected clamped at 300).
- [ ] `RocketJumpRun --repeat 5` 5/5 (no regression); `infra/ci/build.sh` green; no commit.

## Verification steps
1. `infra/ci/build.sh` → tests green.
2. `infra/ci/nettest.sh --scripts FireRun --repeat 3`; then `--scripts FireRun --lag 125 --repeat 3`; then `--scripts FireRunHold --repeat 3`; grep `VeloxShotOriginSnap`, `late_ms=`.
3. `infra/ci/nettest.sh --scripts RocketJumpRun --repeat 5`.

## Report back
Standard format. Quote the held rails' `VeloxShot` lines (`late_ms`, `rewind_ms`, `hit`), the max `err_cm` seen in any `VeloxShotOriginSnap` over all runs (expected none), and the max `late_ms` on non-held shots.

## Notes / outcome
(Filled by the agent or the lead after completion.)

## Open for the architect (lead, 2026-10-29)
- FireRunHold fails only the `late_ms ∈ [280, 360]` window (B36 §9): values 275.0 (move-stream loss around the release, ~1.5 server frames) and 466.7 (held RPC also resent). Proposal: lower bound hold − 2 server frames, upper 360 + resend allowance (RTT + ~80 ms) when the held seq shows a resend. 0 snaps and 0 rejections in every round.
- 08g departure 1: stamp clamped to last move ± MaxRewindSeconds before the eye lookup (prevents checking against an eye up to ~1 s old; honest cost ≤ ~12 cm when late_ms > 300).
- Held rails miss (rewind clamped at 0.30, `hint=fallback`, 2/12 hit) → B35 "hitscan in the move stream" trigger; also STATUS question 19.
