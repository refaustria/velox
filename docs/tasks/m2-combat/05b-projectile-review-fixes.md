# 05b — Projectile review fixes: per-life shot sequence, shared validation, hit confirm, event cancellation

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** todo
**Depends on:** 05, 04b (review `docs/reviews/m2-05-06-projectiles-death-review.md` §2 defects 1, 3, 4, 5, 6, 11, 12; ADR 0003 amendments B25, B26, B27). Must land before 08. Independent of 04c and 06b.

## Goal
Close the sequence-number hole in the recorded self-knockback channel (a predicted rocket jump is dropped from a replay whenever `ShotSeq` is more than 127 ahead of `ConfirmedSelfKnockbackSeq` — a fresh life or 6 s of lightning gun before a rocket — and a correction is in flight), make the projectile ability validate shots through the 04b machinery and finalise pending switches, confirm rocket/plasma hits to the shooter, give the death flow a cancel-all for move-timed events, and make the fire time-stamp clamp wrap-aware and logged.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxWeaponInventoryComponent.h/.cpp` — `ResetShotSeq()`; called from `ResetToLoadout` (server) and from the owning client when the ASC avatar changes
- `Source/Velox/Character/VeloxCharacter.cpp` — `InitAbilityActorInfo`: on the autonomous proxy, when the avatar actually changes, `Inventory->ResetShotSeq()`; `HandleDeath` step 2 and `ApplyCorpseState`: `Movement->CancelAllMoveTimedEvents()`
- `Source/Velox/Movement/VeloxMovementComponent.h/.cpp` — `ServerNoteShot(uint8 Seq)` (advances `ConfirmedSelfKnockbackSeq` to the newer of itself and `Seq` while `ServerUnresolvedSelfKnockbackSeqs` is empty), `int32 CancelAllMoveTimedEvents()`, remove the dead `!bApplyingCorrection` test in the event hook (or comment why it stays)
- `Source/Velox/Weapons/VeloxGameplayAbility_FireHitscan.cpp` — accepted path calls `Movement->ServerNoteShot(Valid.ShotSeq)` for remote shooters
- `Source/Velox/Weapons/VeloxShotValidation.h/.cpp` — `Validate` accepts `FGameplayAbilityTargetData_VeloxProjectileShot` (base checks + finite `FireMoveTimeStamp`, reason `time`; `FVeloxValidatedShot::FireMoveTimeStamp`); new `ValidateOrigin(const UWorld&, const APawn& Shooter, const FVector& ClientOrigin, float MaxOriginErrorCm, const TCHAR*& OutSnapReason) -> FVector` holding the B15 rule
- `Source/Velox/Net/VeloxLagCompensationSubsystem.cpp` — `TraceShot` step 1 calls `ValidateOrigin` (behaviour unchanged, one implementation)
- `Source/Velox/Weapons/VeloxGameplayAbility_FireProjectile.cpp` — `OnServerTargetData` → `ValidateServerShot` → `FinalisePendingSwitch` → `LaunchServerShot`; `LaunchServerShot` uses `ValidateOrigin`; wrap-aware clamp of `FireMoveTimeStamp` (a difference above `0.5 × Movement->MinTimeBetweenTimeStampResets` means a time-stamp reset: accept the client value) and `VeloxShotTimeClamp seq= client_ts= last_ts= clamped_ts=` whenever the clamp acts; `RocketJumpTest` settle time = `SpawnProtectionSeconds + 1 s` from the settings instead of the constant 3.0
- `Source/Velox/Weapons/VeloxProjectile.h/.cpp` — `ServerExplode`: `ClientConfirmHit(ShotSeq, total, Kill)` on the owner's `AVeloxPlayerController` when at least one non-owner victim was damaged (sum of health+armor removed, clamped 255; Kill if any victim's health reached 0); `FVeloxProjectileExplosionInfo` gains `ConfirmedDamage`/`bKill`
- `Source/Velox/Tests/VeloxCombatTests.cpp` (`KnockbackSeq`), `Source/Velox/Tests/VeloxWeaponTests.cpp` (`ShotValidation`, `Inventory`), `Source/Velox/Tests/VeloxProjectileTest.cpp` (`ProjectileDummy`)

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` — §3, §5, §7, B15, B18, B19, B25, B26, B27
- `docs/adr/0002-movement-component-design.md` — A17, A29, A32
- `docs/reviews/m2-05-06-projectiles-death-review.md` — §2 defects 1, 3, 4, 5, 6, 11, 12; §3 (why the sequence hole is real)
- Engine: `CharacterMovementComponent.cpp` l.8864–8867 (client time-stamp reset), l.9395–9408 (server follows), l.10029 (`CurrentClientTimeStamp` set before the move)

## Acceptance criteria
- [ ] `Velox.Combat.KnockbackSeq` gains and passes: fresh life (seq 1 vs confirmed 0 → replay), 200 shots noted via `ServerNoteShot` then a rocket at 201 (confirmed 200 → replay), the same without noting (documents the old hole: `ShouldReplayPredictedKnockback(201, 0)` is false), `ServerNoteShot` ignored while a self-knockback shot is unresolved, `CancelAllMoveTimedEvents` returns the count and leaves none.
- [ ] `Velox.Combat.Inventory`: `ResetToLoadout` resets the shot counter (next `AllocateShotSeq()` returns 1).
- [ ] `Velox.Combat.ShotValidation`: projectile struct accepted with a finite stamp, rejected with `time` on NaN/±inf stamp; `ValidateOrigin` rows: within tolerance (unchanged), NaN (snap), > `MaxOriginErrorCm` (snap); the occlusion row stays in the lag-comp test where a world exists.
- [ ] `Velox.Combat.ProjectileDummy`: the RL direct hit and the floor splash record `ConfirmedDamage > 0`; the rocket-jump shot (owner only) records `ConfirmedDamage == 0` and no confirm.
- [ ] A pending-switch RL shot on the server (listen-world test or `FireTest` after a slot press within `SwitchTimingToleranceSeconds`) leaves `CurrentIndex` on RL right after the shot.
- [ ] `Velox.GameMode.DeathFlow` still green; the B8 step log for step 2 includes the cancelled-event count.
- [ ] `infra/ci/build.sh` → `[ci] CI OK` (all `Velox.*` tests); M1 `nettest.sh` full list unchanged (one run); online `velox.combat.RocketJumpTest -90 quit` at 100 ms / 2 % twice: `PASS`, `corrections=0`, no `VeloxShotTimeClamp` line on the server.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`.
2. `infra/ci/nettest.sh` (default list) → all PASS.
3. Online rocket jump per `docs/adr/evidence/m2-05-rj_net.sh` (100 ms / 2 %), twice; grep the server log for `VeloxShotTimeClamp` (none) and `VeloxProjectileShot` (`client_ts` within one move of `last_ts`).

## Report back
Standard dev-hard format; state the exact place the client resets its shot counter and why it cannot run twice for one life.

## Notes / outcome
(Filled by the agent or the lead after completion.)
