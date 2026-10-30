# Task 08i: pending shot timer guard, agent report (dev-hard, 2026-10-30)

Base `m3-gameloop` (beda770). Not committed. `infra/ci/build.sh`: VeloxEditor and Velox build; VeloxServer/VeloxClient SKIPPED (installed engine). 80 tests, 0 failures (`Saved/CI/20261030-004931-20691`). Soaks on port 7852.

## Design as built (ADR 0003 B38)
- **Timer owned by the instance.** `UVeloxGameplayAbility_FireProjectile` has `FTimerHandle PendingServerShotTimer` and `uint8 PendingServerShotTimerSeq`. They are armed in the pending branch of `OnServerTargetData`. `ClearPendingServerShotTimer()` runs in `LaunchPendingServerShot`, `CancelPendingServerShot` and `EndAbility`. `OnPendingServerShotTimeout` launches only when `bHasPendingServerShot && PendingServerShot.ShotSeq == PendingServerShotTimerSeq`.
- **Ammo paid when the RPC is accepted.** `ApplyAmmoCost` runs in the pending branch, inside the RPC's prediction window, and sets `bPendingAmmoPaid`. `LaunchServerShot` skips the cost while the flag is set. The flag resets after the pending launch, on cancel, and on drop. A dropped pending shot is not refunded: `VeloxProjectileShotDropped … ammo_paid=1`.
- **Rpc-launched guard.** The rpc branch of `LaunchServerShot` (`source=rpc`) calls `Inventory->NoteRpcLaunchedShot(Seq)`. The inventory keeps `RpcLaunchedShotSeqs`, an 8-entry ring cleared in `ClearMoveShots`. `HandleServerMoveShot` step (2) ignores such a sequence with `reason=rpc_done`, after `duplicate` and before payload, ammo and token.
- **Idempotent begin.** `ServerBeginSelfKnockbackShot` uses `AddUnique`.
- **Second RPC while pending is logged.** A second target data while pending logs `VeloxShotRpcIgnored seq=<second> pending_seq=<pending> reason=pending` at Verbose.
- **Order fix in `ResetToLoadout`, found by the cancel row.** `ClearMoveShots()` now runs before the spec removal loop. When the engine removes the spec of a fire ability with a waiting shot, it ends that ability with `bWasCancelled = false`. The `!bWasCancelled` branch then *launched* the old life's shot at reset.
  - In production the death flow cancels abilities first (B27 step 2), so this was latent.
  - It was a real bug for any `ResetToLoadout` without a preceding death, e.g. a mode reset.

## Tests: `Velox.Combat.MoveShotPendingGuard` (new, `VeloxProjectileTest.cpp`, `FProjectileWorld`)
- **Idempotent:** begin(7) twice, resolve(7) once → not unresolved; `ServerNoteShot(8)` → confirmed 8 (the set is empty).
- **Stale timer (PG):**
  - Seq 1 is pending, then its move → `source=move`.
  - 0.1 s later seq 2 is pending.
  - World time goes past seq 1's timeout but stays before seq 2's → seq 2 is still pending and nothing is scheduled.
  - Seq 2's move → `source=move`.
  - Result: one event per seq, no timeout line even 0.35 s later.
  - The bucket equals a reference `FVeloxFireRateBucket` fed the same two consumptions at the same times.
- **Late move-shot (RL seq 3):** pending → timeout → `source=rpc`, with one timeout line and one event. The seq-3 fire move then gives `reason=rpc_done`: no record, still one event, bucket unchanged (tokens and `LastTime`). After one resolve(3), seq 3 is no longer unresolved.
- **Ammo (RL seq 4):** rockets are `Ammo − AmmoPerShot` while pending. A second target data (seq 9) is logged and ignored. The move launches seq 4 → ammo unchanged.
- **Cancel (RL seq 5):** while pending, ammo is paid. `ResetToLoadout` → dropped, never launched, nothing scheduled, ring cleared, no later timeout.
- **Ring:** 9 notes → the oldest is evicted.
- Log capture: a local `FOutputDevice` with `GLog->Flush()` before reading. Without the flush the threaded log had not dispatched the line yet.
- Existing `MoveShotServerPath` and `ProjectileServerPath` rows are unchanged and green.

Test output (server lines):
```
VeloxMoveShot seq=1 weapon=DA_Weapon_PlasmaGun fire_ts=2.000000 move_ts=2.008333 … rpc_first=1
VeloxProjectileShot seq=1 weapon=DA_Weapon_PlasmaGun … source=move late_ms=8.3
VeloxMoveShot seq=2 weapon=DA_Weapon_PlasmaGun fire_ts=2.100000 move_ts=2.108333 … rpc_first=1
VeloxProjectileShot seq=2 weapon=DA_Weapon_PlasmaGun … source=move late_ms=8.3
VeloxProjectileShotPendingTimeout seq=3 fire_ts=3.100000 (no owner move reached the fire stamp)
VeloxProjectileShot seq=3 weapon=DA_Weapon_RocketLauncher … source=rpc late_ms=0.0
VeloxMoveShotIgnored seq=3 weapon=2 (DA_Weapon_RocketLauncher) move_ts=3.108333 reason=rpc_done
VeloxShotRpcIgnored seq=9 pending_seq=4 reason=pending
VeloxProjectileShotDropped seq=5 reason=cancelled ammo_paid=1 (fire RPC waiting for its move)
```

## Soaks (port 7852)
- **`PlasmaRun --repeat 3`: 3/3** (`Saved/CI/20261030-005157-24879`). Every round:
  - `pg_seqs=50 fired=50 moveshots=50 source_move=50 splash_1=50`
  - `late=0 orphan=0 pending_timeout=0 rpc_done=0 rejected=0`
  - `rpc_first` = 48 / 44 / 48 (the pending path), `move_dt_8ms=50`, `moves_per_rpc=1.78`.
  - Client at `t.MaxFPS 120`, about 67 RPCs/s (engine 60 Hz send plus the event and fire moves that force a send), so the dual-move split is active on every fire move.
- **`RocketJumpRun --repeat 10`: 10/10**, all `source=move` (`Saved/CI/20261030-005357-28089`).
- **`RocketJumpRunHold --repeat 3`: 3/3**, `late_ms` 283.4 / 316.7 / 333.4 (`Saved/CI/20261030-005911-36582`).
- **Default list `--repeat 3`: 3/3**, all 11 scripts PASS each round (`Saved/CI/20261030-010050-38951`).

## Departures and notes for the architect
1. **`PlasmaRun` aims at a floor point 2 m ahead,** so every server splash evaluates `hit=0` (no knockback, no death, sequences stay in one life).
   - The "≤ 1 server `VeloxSelfSplash` per seq" assertion is the double-schedule detector and works for hit=0 too.
   - A `hit=0` evaluation queues no knockback. Its move is therefore no event move and can merge on the wire, so server and client splash stamps differ by one 120 Hz frame for 49/50 shots. This is harmless, and `splash_ts_mismatch` counts only hit=1 sequences.
   - A plasma-climb variant (hit=1, needs health or armor so 50 shots don't kill) would also cover the stamp equality under a plasma stream. Proposed as a follow-up, not done.
2. **"tokens = capacity − 2" asserted against a reference bucket.** The stale-timer row needs world time to pass, and that refills the bucket. The row compares against a reference bucket fed the same two consumptions instead.
3. **The `ResetToLoadout` order change** (above) is outside the letter of the task but inside the scoped file. Without it the cancel row's "shot dropped" cannot hold.
4. **The accessor `GetNumServerUnresolvedSelfKnockbackShots` was not added** (the movement header is out of scope). The check uses resolve-once and then `IsServerSelfKnockbackShotUnresolved`, plus the `ServerNoteShot` confirmed-sequence probe for emptiness.
5. **New file `Content/Data/MoveScripts/PlasmaRun.json`** (the "FireRun.json-style script" the task allows).
