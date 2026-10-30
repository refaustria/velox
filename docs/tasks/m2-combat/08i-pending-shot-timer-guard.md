# 08i — Pending fire RPC: instance-owned timeout, one schedule per sequence, ammo at acceptance (B38)

**Milestone:** M2 Combat (follow-up)
**Agent:** dev-hard
**Status:** done (merged; PlasmaRun 3/3, 80 tests)
**Depends on:** ADR 0003 B38 (spec), B35, B25; review `docs/reviews/m2-late-rpc-fixes-review.md` (ruling 1c/1d, defects 1, 4, 5); 08f/08g/08h merged (`affac1c`).

## Goal
A projectile sequence is scheduled on the server exactly once, whatever order and timing the fire RPC and the fire move arrive in. Today the pending RPC's 0.30 s timeout is a local `FTimerHandle` that is never cleared, so for the plasma gun (0.1 s interval) a stale timer can resume a *later* pending shot early on the rpc path; its fire move then arrives, finds neither record nor pending RPC, pays a second token and schedules a second self-splash for the same sequence (≈ 4 % of plasma shots on a 120 Hz client at 60 Hz send). The same double schedule happens after an honest > 0.30 s stall. Also: the ammo cost of a pending shot moves to the moment the RPC is accepted (the predicted cost can otherwise rebound for one PlayerState update), and a second target-data RPC while pending is logged instead of silently dropped.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxGameplayAbility_FireProjectile.h/.cpp` — `FTimerHandle PendingServerShotTimer` + `uint8 PendingServerShotTimerSeq` (members), armed in the pending branch of `OnServerTargetData` (l.343–346), cleared in `LaunchPendingServerShot`, `CancelPendingServerShot`, `EndAbility`; `OnPendingServerShotTimeout` resumes only when `bHasPendingServerShot && PendingServerShot.ShotSeq == PendingServerShotTimerSeq`; `bPendingAmmoPaid`: `ApplyAmmoCost` called before `return` in the pending branch, skipped in `LaunchServerShot` when set (reset with the pending state); the rpc branch of `LaunchServerShot` (`source=rpc`, l.498–536) calls `Inventory->NoteRpcLaunchedShot(ShotSeq)`; l.282 early return logs `VeloxShotRpcIgnored seq= reason=pending` at Verbose.
- `Source/Velox/Weapons/VeloxWeaponInventoryComponent.h/.cpp` — `RpcLaunchedShotSeqs` (`TArray<uint8, TInlineAllocator<8>>`, ring of 8, cleared in `ClearMoveShots`), `NoteRpcLaunchedShot(uint8)`, `WasShotLaunchedByRpc(uint8) const`; `HandleServerMoveShot` step (2): `reason=rpc_done` before the ammo/token step (l.788–808).
- `Source/Velox/Movement/VeloxMovementComponent.cpp` — `ServerBeginSelfKnockbackShot` (l.319–326): `AddUnique`.
- `Source/Velox/Tests/VeloxProjectileTest.cpp` — rows below in `Velox.Combat.MoveShotServerPath` (or a sibling test using `FProjectileWorld`).
- `infra/ci/nettest.sh`, `infra/ci/README.md` — new script `PlasmaRun` (not in the default list): the client holds PG fire for 5 s from the FireRun start pose at the default 50 ms one-way / 2 % (reuse `velox.combat.FireTest`'s plumbing or a `FireRun.json`-style script with `fire_hold`); analysis: every `VeloxProjectileShot seq=` has exactly one `VeloxMoveShot` with the same seq and `source=move`; per seq exactly one server `VeloxSelfSplash` (or none if the ball never hit near the owner — assert ≤ 1); 0 `VeloxShotLate`, 0 `VeloxMoveShotOrphan`, 0 `VeloxProjectileShotPendingTimeout`, 0 `VeloxMoveShotIgnored reason=rpc_done` (none expected without a stall; report the count), 0 rejections. If the client cannot be made to run at 120 Hz with 60 Hz sends in the soak, say so in the report (the dual-move split is the trigger; `p.NetClientUpdateFrequency`/`t.MaxFPS` are the knobs).
- `docs/tasks/m2-combat/notes/08i-report.md`.
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` B38 (spec), B35, B25 (sequence rule), B32.
- `docs/reviews/m2-late-rpc-fixes-review.md` §1 rulings 1b–1d, §2 defects 1, 4, 5, §3 (retrigger: engine `AbilitySystemComponent_Abilities.cpp` l.1836–1842 ends with `bWasCancelled = false`).

## Acceptance criteria
- [ ] Stale-timer row: RPC seq 1 pending (no move yet), then its move → launched (`source=move`); RPC seq 2 pending; advance world time past seq 1's timeout without a move → seq 2 **still pending**, `GetNumMoveTimedEvents` unchanged; then seq 2's move → launched `source=move`, one event per seq, tokens = capacity − 2.
- [ ] Late-move-shot row: RPC seq 3 pending, no move for > `MaxRewindSeconds` → timeout → `source=rpc`, one event, `VeloxProjectileShotPendingTimeout` logged; then the fire move with the seq-3 move-shot arrives → `VeloxMoveShotIgnored reason=rpc_done`, still one event, tokens unchanged, `ServerUnresolvedSelfKnockbackSeqs` contains 3 exactly once (add a test accessor `GetNumServerUnresolvedSelfKnockbackShots()` or count via `IsServerSelfKnockbackShotUnresolved` after one `ServerResolveSelfKnockbackShot(3)` → false).
- [ ] Ammo row: RPC seq 4 goes pending → rockets already `Ammo − AmmoPerShot` before any move; the move then launches it → ammo unchanged (paid once). Cancel row: RPC seq 5 pending, `ResetToLoadout` → shot dropped, no refund (documented).
- [ ] `ServerBeginSelfKnockbackShot(7)` twice → unresolved once; `ServerResolveSelfKnockbackShot(7)` → empty.
- [ ] Existing rows of `MoveShotServerPath` (move first / RPC first / RPC only / ignored / stopped stream / orphan / reset) unchanged and green; `Velox.Combat.ProjectileServerPath` green.
- [ ] `RocketJumpRun --repeat 10` 10/10 and `RocketJumpRunHold --repeat 3` 3/3 (no regression); `PlasmaRun --repeat 3` 3/3 with the assertions above.
- [ ] `infra/ci/build.sh` green for all targets; no commit.

## Verification steps
1. `infra/ci/build.sh` → tests green (79 + new rows).
2. `infra/ci/nettest.sh --scripts RocketJumpRun --repeat 10`; `--scripts RocketJumpRunHold --repeat 3`; `--scripts PlasmaRun --repeat 3`. Grep the server logs for `rpc_done`, `PendingTimeout`, `VeloxMoveShotOrphan`, duplicate `VeloxSelfSplash role=server … seq=` per seq.
3. Default list `--repeat 3` → 3/3.

## Report back
Standard format. Quote: the stale-timer and late-move-shot rows' output, one `PlasmaRun` summary line with the per-seq counts, the client frame/send rate the soak ran at. State any departure from B38 as a numbered list for the architect.

## Notes / outcome
(Filled by the agent or the lead after completion.)
