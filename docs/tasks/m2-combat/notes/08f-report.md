# Task 08f: move-carried projectile shot, agent report (dev-hard, 2026-10-29)

Base e0f491b. Not committed. `infra/ci/build.sh`: VeloxEditor and Velox build; VeloxServer/VeloxClient are SKIPPED on the installed engine. 73 tests, 0 failures (`Saved/CI/20261029-233440-4114951`). Soaks ran on port 7837 on a loaded machine (load 5–7).

## Design as built
- `FVeloxMoveShot {ShotSeq u8, WeaponIndex 4 bits (SerializeInt 16), FireMoveTimeStamp f32, Origin NetQuantize100, Dir NetQuantizeNormal}` with `Serialize(FArchive&)`. `FVeloxNetworkMoveData` adds `bHasShot` + `Shot` after the engine's fields, behind 1 presence bit, for every move type. The container's extra slot gets it with no extra code. Measured cost: **1 bit without a shot, 157 bits with one**.
- Client: `ActivateAbility` (after `Quantize`) calls `QueueMoveShot` with the RPC's quantized values (RL/PG: splash + ProjectileClass). `SetMoveFor` → `ConsumePendingMoveShot` → `bSavedHasShot`/`SavedMoveShot` and `bForceNoCombine`. `CanCombineVeloxState` returns false when either move has a shot. `IsEventMove` includes it, so slot 1/2 selection follows unchanged. `Clear` resets both fields. `PrepMoveFor` restores nothing.
- Server: `MoveAutonomous` (authority) broadcasts `OnServerMoveShot(Shot, ClientTimeStamp, DeltaTime)` before `Super`, only for move data that is this component's own container (`AsVeloxMoveData`). It broadcasts `OnServerMoveProcessed(ClientTimeStamp)` after `Super`.
- `UVeloxWeaponInventoryComponent` binds in `AVeloxCharacter::InitAbilityActorInfo` (authority) and unbinds in `UnPossessed`. `HandleServerMoveShot` runs B35 steps 1–6: owned + splash + ability, not dead/frozen, seq not recorded, payload finite; `CheckCost` and `ServerConsumeMoveShotToken` are skipped when the RPC overtook the move and is pending. It clamps against the move start, runs `ValidateOrigin` against the live eye, then `ComputeWorldFlight` + `ScheduleOwnerSelfSplash(false)`. Quad comes from the move timeline at the fire stamp, as `LaunchServerShot` and the client do today. The ring holds 8 records, cleared in `ResetToLoadout` and on rebind.
- RPC (`OnServerTargetData`): `bConsumeBucket = !recorded`. The paths:
  - Recorded → adopt: the record's origin, dir, stamp and event go into the launch. The RPC values are cross-checked (`VeloxMoveShotMismatch`). Log `source=move late_ms=`.
  - No record and the stream has not passed the stamp → **pending**. The ability stays active; the inventory's per-move hook resumes it, adopting if the move created a record, else `source=rpc`. A timeout of `MaxRewindSeconds` also resumes it. The launch runs inside `FScopedPredictionWindow(activation key)`.
  - Otherwise → today's path with `source=rpc late_ms=` (`VeloxShotLate` when > 1/60 + 40 ms).
- Unadopted records expire after `FireRateJitterToleranceSeconds + MaxRewindSeconds` (or on ring eviction) and log `VeloxMoveShotOrphan`, with a per-binding counter.
- `velox.combat.DebugHoldShotRpc <ms> <slot> [count]` (non-shipping, `VeloxWeaponCheats`), hooked through `UVeloxGameplayAbility_Fire::DebugHoldServerTargetData` in both fire abilities.

## Tests
- `Velox.Movement.SavedMoveCombine`: new shot rows (both directions, event move, `Clear`).
- New `Velox.Movement.SavedMove.MoveShotOldMove`: a fire move wins slot 1 over a newer ordinary move; its predecessor takes slot 2; a newer splash move takes slot 1; an acked fire move is not resent.
- New `Velox.Movement.SavedMove.MoveShotSerialize`: bit counts; a new/old/extra container round trip with every field exact (quantized values survive the second round trip); a stale pending flag is cleared; `AsVeloxMoveData`.
- New `Velox.Combat.MoveShotServerPath`:
  - Move first then RPC: adopted, one event at fire + flight, seq unresolved, one token, ammo once.
  - RPC first then move: pending, then adopted, same end state.
  - RPC only, after the stream passed the stamp: `source=rpc`, late check.
  - Ignored move-shots (not owned / no ammo / empty bucket), with the later RPC on the rpc path.
  - Pending timeout; orphan expiry; reset.

## Soaks (port 7837, loaded machine)
- RocketJumpRun `--repeat 20`: **20/20**, every run `source=move`. 18 rounds had `late_ms` 8.3–8.5. Two rounds had real fire-RPC losses at 216.7 and 250.0 ms and passed with 0 corrections. 0 `VeloxShotLate`/`Orphan`/`OriginSnap`/`TimeClamp` (`Saved/CI/20261029-225532-4051075`).
- RocketJumpRunHold `--repeat 5`: **5/5**, `late_ms` 300.0–333.4, apex 711.6–713.2 (ratio ≥ 6.25), C=0 (`Saved/CI/20261029-230643-4072862`).
- FireRun `--lag 125` ×3 (report-only):
  - Hits: 10/11, 11/11, 11/11. `rejected=0`.
  - `act_failed=1` in 2 runs: B18 switch tolerance, which is 08h's item.
  - The rewind clamp at 0.30 and `hint=stamp` 6–8/11 match question 19.
  - In run 1 the rocket had no confirm.
  - Logs: `Saved/CI/20261029-230932-4078019`.
- Default list `--repeat 3`: **3/3** (`Saved/CI/20261029-231301-4083638`).

Hold excerpt (server):
```
VeloxShotRpcHeld weapon=DA_Weapon_RocketLauncher slot=5 ms=300 t=15.974
VeloxMoveShot seq=1 fire_ts=1.742867 move_ts=1.751200 move_dt=0.008334 flight_ms=55.6 event_ts=1.798467 rpc_first=0
VeloxSelfSplash role=server reason=move seq=1 ts=1.805334 hit=1 dist=38.41 points=87
VeloxProjectileShot seq=1 fire_ts=1.742867 client_ts=1.742867 last_ts=2.047016 event_id=1 source=move late_ms=304.1
VeloxProjectileExplode seq=1 age_ms=55.6 self=move_timed
```

## Departures and notes for the architect
1. **Pending RPC path.** The RPC normally reaches the server *before* its fire move, because the ability's RPCs are queued ahead of the CMC's ServerMove in the same packet. In that case the RPC waits for the move, so the normal case is adopted, not `source=rpc`. B35 describes this case only as a fallback.
   - The wait applies only while the inventory has seen a processed move. Offline tests and the first move of a life take the old path at once.
   - The projectile spawns at the end of the move that carries the shot, about one move after RPC receipt.
   - Under the deferred prediction window, the client's predicted ammo GE could flicker once if key catch-up replicates before the server's ammo change. Not observed.
2. **`VeloxMoveShotOrphan` is logged at Log**, throttled through the per-player rejection limit (5/s; Verbose after that). B35 says Verbose; the soaks have to count it.
3. **The self-splash Quad comes from the move timeline at the fire stamp** (M3-03/A37), not from the ASC tag as B35's text says. This matches the client and today's `LaunchServerShot`.
4. **Hold hook in `VeloxGameplayAbility_FireHitscan.cpp`.** I added a 5-line hook there; the task asks for it in "both fire abilities", but the file is not in the scope list. 08g edits the same function, so expect a trivial rebase.
5. **New test location.** The new server-path test is in `Tests/VeloxProjectileTest.cpp` (where the template `FProjectileWorld` lives), not in `VeloxWeaponTests.cpp`.
6. **Test-only accessors.** Added `UVeloxMovementComponent::GetMoveTimedEventTime` / `IsServerSelfKnockbackShotUnresolved` and `UVeloxGameplayAbility_Fire::GetServerBucket`.
7. **Ignored shots are not fatal in the soak.** The move-shot's `ValidateOrigin` uses the existing live-eye signature, which is correct at the move start. B36 (08g) only changes the RPC path. `VeloxMoveShotIgnored` lines are a nettest note, not a failure; a missing `source=move` fails anyway.
8. **Stale comment outside scope.** `VeloxShotTargetData.h` still says the shot is "never inside moves (ADR 0002 A33)". This is now stale under B35/A36 and should be fixed with 08g, which touches that file.
