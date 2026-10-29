# 08f — Move-carried projectile shot: the self-splash is scheduled from the move stream (B35 / A36)

**Milestone:** M2 Combat (follow-up)
**Agent:** dev-hard
**Status:** done (merged affac1c; 79 tests; RocketJumpRun/Hold pass)
**Depends on:** ADR 0003 B35, ADR 0002 A36, review `docs/reviews/m2-late-rpc-review.md` (rulings 1–4, defects 1 and 7); 08c/08d merged. Independent of 08g/08h (merge order free; 08g changes the same `ValidateOrigin` signature — rebase, do not fold).

## Goal
A lost fire packet no longer breaks a rocket jump. The fire move of RL/PG carries `FVeloxMoveShot` (seq, weapon, fire stamp, quantized origin and direction); the server registers the owner's self-splash from the move stream before that move is simulated, and the GAS RPC (which may arrive one resend later) adopts the record and only spawns the projectile, pays ammo, plays the cue and finalises the switch. Round 4 of `Saved/CI/evidence-rj-late-fire/nettest/round-4/` (move repaired in 16 ms, RPC 237 ms late, 9 + 6.5 cm corrections, half apex) must become a pass.

## Scope — files/modules the agent may touch
- `Source/Velox/Movement/VeloxNetworkMoveData.h/.cpp` — `FVeloxMoveShot` (+ `NetSerialize`-style `Serialize(FArchive&)`), `FVeloxNetworkMoveData` field + fill + serialise (presence bit after the engine's fields, before nothing else; the container's extra slot serialises the same struct so it comes for free), `FSavedMove_Velox::SavedMoveShot`/`bSavedHasShot` captured in `SetMoveFor` (`bForceNoCombine`), `CanCombineVeloxState`, `IsEventMove`, `Clear`.
- `Source/Velox/Movement/VeloxMovementComponent.h/.cpp` — `QueueMoveShot` (autonomous proxy only, single slot, a second queue before the move is created overwrites and logs `Warning`), `ConsumePendingMoveShot` for `SetMoveFor`, `OnServerMoveShot` delegate broadcast at the top of the authority branch of `MoveAutonomous` (before `Super`), `OnServerMoveProcessed(float Stamp)` delegate after `Super` (authority).
- `Source/Velox/Weapons/VeloxGameplayAbility_FireProjectile.h/.cpp` — client: `QueueMoveShot` after `Data->Quantize()` (l.147); server `LaunchServerShot` remote branch (l.304–344): adopt the record (origin/dir/stamp cross-check, no second `ScheduleOwnerSelfSplash`, launch fields from the record) or keep a pending RPC shot; `OnServerTargetData` passes `bConsumeBucket = !recorded`; logs `source=move|rpc late_ms=`.
- `Source/Velox/Weapons/VeloxGameplayAbility_Fire.h/.cpp` — `ServerConsumeMoveShotToken(Now, Definition)` (public wrapper of the bucket), `ValidateServerShot(..., bool bConsumeBucket)`.
- `Source/Velox/Weapons/VeloxShotValidation.h/.cpp` — `Validate(..., bool bConsumeBucket)`.
- `Source/Velox/Weapons/VeloxWeaponInventoryComponent.h/.cpp` — server consumer: bind/unbind on possession change (via `AVeloxCharacter::InitAbilityActorInfo` or the existing possession hook), `ServerMoveShots` ring (8), `PendingRpcShots`, `HandleServerMoveShot`, `HandleServerMoveProcessed`, expiry (`VeloxMoveShotOrphan`), clear in `ResetToLoadout`; logs `VeloxMoveShot`, `VeloxMoveShotIgnored reason=`, `VeloxMoveShotMismatch`, `VeloxShotLate`.
- `Source/Velox/Character/VeloxCharacter.cpp` — only the binding call, if the inventory cannot reach the CMC otherwise.
- `Source/Velox/Weapons/VeloxProjectile.h/.cpp` — none expected; `FVeloxProjectileLaunch` already carries `SelfSplashEventId/OwnerMovement/SelfSplashOrigin`.
- `Source/Velox/Weapons/VeloxWeaponCheats.cpp` (or the projectile ability's non-shipping block) — server cvar `velox.combat.DebugHoldShotRpc <ms> <slot> [count=1]`: the next `count` target-data RPCs of that weapon slot are processed `ms` later via a world timer (hold inside `OnServerTargetData` of both fire abilities: stash the handle data, re-enter the same function). Non-shipping.
- `Source/Velox/Tests/VeloxMovementNetTests.cpp` (or the existing saved-move/net-data test file), `Tests/VeloxWeaponTests.cpp` — rows below.
- `infra/ci/nettest.sh` — RocketJumpRun: assert `source=move` on the rocket's `VeloxProjectileShot`, `late_ms` reported, `VeloxShotLate` 0, `VeloxMoveShotOrphan` 0; the `|client_ts − last_ts|` rule only for `source=rpc`; new script name `RocketJumpRunHold` = RocketJumpRun with `velox.combat.DebugHoldShotRpc 300 5 1` in `sexec` (not in the default list; `--scripts RocketJumpRunHold`). `infra/ci/README.md` one paragraph.
- `docs/tasks/m2-combat/notes/08f-report.md`.
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` B35 (the spec), B25, B26, B32; `docs/adr/0002-movement-component-design.md` A36, A29, A32, A35.
- `docs/reviews/m2-late-rpc-review.md` §2 rulings 1–4, §3 defects 1 and 7, §4 interactions.
- Engine: `CharacterMovementComponent.cpp` `ReplicateMoveToServer` l.8907–9090 (`CurrentTimeStamp += DeltaTime` → `SetMoveFor` → `PerformMovement` → `PostUpdate`), `ServerMove_PerformMovement` l.9967–10060 (`VerifyClientTimeStamp` → `CurrentClientTimeStamp = ClientTimeStamp` → `MoveAutonomous`).

## Acceptance criteria
- [ ] `FVeloxNetworkMoveData` round trip: without a shot the Velox part costs 1 bit; with a shot ≤ 160 bits and every field survives (seq, index 0–15, stamp exact, origin to 1 cm, dir to the engine's normal quantisation); the extra old-move slot carries it too.
- [ ] `SavedMoveCombine`: a move with a shot combines with nothing (both directions); `ChooseOldMove`: a fire move beats an ordinary newer move for slot 1 and its predecessor takes slot 2 (`ChooseExtraOldMove`); a splash move newer than the fire move takes slot 1 (A35 unchanged).
- [ ] Offline server-path test (template `Velox.Combat.ProjectileServerPath`): a move with a shot processed before the RPC → one event scheduled at `FireMoveTimeStamp + flight`, the bucket consumed once, `ServerUnresolvedSelfKnockbackSeqs` contains the seq; then the RPC → projectile spawned, no second event, ammo paid once, `source=move`. Reverse order (RPC first, move 1 frame later) → same end state, `source=move`. RPC only with the fire stamp passed by the move stream → `source=rpc`, `VeloxShotLate` when late by > 1/60 + 40 ms. Move-shot for a non-owned weapon / no ammo / empty bucket → ignored with reason, nothing scheduled, the later RPC schedules (`source=rpc`).
- [ ] `RocketJumpRun --repeat 20` at the default 100 ms / 2 % on an idle machine: 20/20; every run `source=move`, `VeloxShotOriginSnap` 0, `VeloxShotTimeClamp` 0, `VeloxShotLate` 0, `VeloxMoveShotOrphan` 0, server/client `VeloxSelfSplash` equal (seq, ts, points, dist ≤ 1 cm), 0 non-echo corrections in the window (one ≤ 5 cm tolerated).
- [ ] `RocketJumpRunHold` (forced loss: the server holds the RL target data 300 ms) ×5: 5/5 with the same assertions and `late_ms ∈ [280, 360]` on the `VeloxProjectileShot` line; apex ≥ 2 × jump height.
- [ ] FireRun `--lag 125` ×3 report-only: no regression against 08c (0 rejections except as noted by 08h, hits ≥ 9/10); the hold cvar is not used there (08g does).
- [ ] Default soak list `--repeat 3` 3/3; `infra/ci/build.sh` green for all targets; no commit.

## Verification steps
1. `infra/ci/build.sh` → tests green (53 + new).
2. `infra/ci/nettest.sh --scripts RocketJumpRun --repeat 20` (idle machine, `--port` free) → 20/20; grep the server logs for `source=move`, `VeloxShotLate`, `VeloxMoveShotOrphan`, `VeloxShotOriginSnap`.
3. `infra/ci/nettest.sh --scripts RocketJumpRunHold --repeat 5` → 5/5, quote one `VeloxProjectileShot … source=move late_ms=` line.
4. `infra/ci/nettest.sh --repeat 3` (default list) → 3/3.

## Report back
Standard format. Quote: the round-trip bit counts, one full RocketJumpRunHold server log excerpt (`VeloxMoveShot` → `VeloxSelfSplash role=server` → `VeloxProjectileShot source=move late_ms=`), the 20/20 summary line, and the measured upstream bytes/s delta if `stat net` was captured (optional). State any departure from B35/A36 as a numbered list for the architect.

## Notes / outcome
(Filled by the agent or the lead after completion.)
