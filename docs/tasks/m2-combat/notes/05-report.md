# Task 05 agent report (2026-10-25, merged as feae626)

build.sh all four targets: 41 tests, 0 failures (`Saved/CI/20261025-031837-2712751`). nettest full list 1/1 on port 7805 (`Saved/CI/20261025-032022-2716178`). Online rocket jump at 100 ms / 2 %: two runs `apex_cm=713.2 ratio=6.26 health=122->78 corrections=0 PASS`; CSV in `docs/adr/evidence/m2-05-rocketjump-net-100ms-2pct.csv`, script `m2-05-rj_net.sh`.

## Design as built
- `AVeloxProjectile`: replicates StartLocation, LaunchVelocity, SpawnServerTime, ShotSeq, Def once (`COND_InitialOnly`), `DORM_DormantAll`, no movement replication, NetUpdateFrequency 10, NetPriority 2.5, 100 m cull. Server forward-simulates `min(ping/2, ProjectileForwardPredictionMax)` as a sweep; a hit in that window explodes immediately (projectile never replicates). Flight = world-free `FVeloxProjectileSim::Step` (constant velocity) everywhere.
- Fake projectile: owner spawns a local non-replicated fake from the quantized target data (NetSerialize round trip), sweeps world + rendered proxy capsules; real projectile hidden on the owner; authoritative impact cue ends the fake (hook in `UVeloxCombatFeedbackComponent`). Other clients dead-reckon and stop visually at the world.
- Splash: server-side, current pose, no lag-comp rewind. Direct hit, then Q3 radius damage to every lag-comp-registered pawn except the direct victim; zero-point hits count (B3); LOS via three Weapon-channel traces (centre + both cap-sphere centres). All through `GE_Damage`. Impact cue in `FScopedPredictionWindow(ASC, FPredictionKey(), false)`, `RawMagnitude` = splash radius.
- Self-knockback channel: `FVeloxPredictedKnockback{Velocity, Seq}` on the autonomous proxy only; captured in `SetMoveFor`, forbids combining, assigned in `PrepMoveFor` iff `ShouldReplayPredictedKnockback`; `ConsumePendingKnockback` applies it before the chained channel. `ConfirmedSelfKnockbackSeq` snapshotted at check time (A18), one optional byte in corrections; server sets it to Seq−1 when a projectile is fired with nothing unresolved (int8 range). The queuing move is an event/important move (A29/A32). Live pending prediction saved/restored around replay.

## Departures from the ADR (architect to rule)
1. **Owner self-splash timed by the owner's moves, not by the server explosion.** Forward sim would make the server explosion land ~0.1 s (~12 moves) earlier than the client's prediction → ~70 cm correction on a floor rocket. Both ends evaluate the owner's splash at the end of the first owner move with timestamp ≥ `FireMoveTimeStamp + worldFlightTime`, same quantized origin/direction, via new hook `UVeloxMovementComponent::AddMoveTimedEvent` (live and server moves only, never in replay). Client predicts the impulse; server applies `GE_Damage` (self, radius) then resolves the shot. If the real projectile explodes elsewhere (pawn hit) the event is cancelled and the owner is handled in that explosion (correction accepted). Deadline FwdSim + MaxRewindSeconds forces a pending event if the owner stops moving. Needs target data subclass `FGameplayAbilityTargetData_VeloxProjectileShot` with float `FireMoveTimeStamp` (+4 B), server-clamped to last processed move time ± MaxRewindSeconds. Result: RocketJumpRun expectation becomes zero corrections.
2. Pawn hits use hit-shape math, not sphere collision vs Pawn (sphere is NoCollision; world is a Weapon-channel sweep).
3. Origin validation is a copy of the B15 rules in the projectile ability (`TraceShot` validation is private; lag comp untouched).
4. `VeloxCombatFeedbackComponent.cpp` got two small edits (impact-cue hook to `AVeloxProjectile::OnImpactCue`, splash-radius debug sphere).
5. New definition field `ProjectileRadiusCm` (default 2, also match tolerance); `ProjectileClass` narrowed to `TSubclassOf<AVeloxProjectile>`.

## Known residuals
- Two own predictions landing in the same move share the newer sequence number.
- Client/server pawn Z differed by 0.05 cm at the event; a radius-points truncation boundary could rarely flip one point (12.5 cm/s).
- World sweep assumed deterministic across processes (bit-equal in both runs).

## Hand-offs
- 04b: when the base gains spacing validation, `OnServerTargetData` in the projectile ability should call the new base check (today `ConsumeServerFireToken` only). `NextFireTime` comes from base `AdvanceNextFireTime`.
- 06: server self-splash `GE_Damage` runs inside the owner's `PerformMovement`, so a lethal rocket jump fires `OnOutOfHealth` mid-move; `HandleDeath` must tolerate that (defer `DisableMovement` to after the move). Death should cancel pending move-timed events.
- 08: RocketJumpRun can wrap `velox.combat.RocketJumpTest [pitch] quit` (logs PASS/FAIL, corrections, max cm); expect 0 non-echo corrections.
- Not run: the headless `FireTest` command line for RL/PG (the `ProjectileDummy` test exercises the same ability path).
