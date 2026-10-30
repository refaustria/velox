# 08j report

- `VeloxShotRpcHeld` gains `late_ms=`; `ComputeShotLateMs` moved to `UVeloxGameplayAbility_Fire` (public static; projectile tests still call it via inheritance).
- nettest FireRunHold / RocketJumpRunHold assert `late_final - late_pre ∈ [hold-50, hold+50]` and `late_pre <= RTT+150`.
- `Velox.Combat.RemoteSwitchTimeline` B41 rows: Haste 0.0603, no Haste 0.0833.
- FireRunHold 5/5: late_pre 0.0 on all 15 held rails, delta 291.7-350.0 (rounds 1-5). RocketJumpRunHold 5/5: late_pre 0.0, delta 266.7-333.4. FireRun + RocketJumpRun 3/3.
- Netsim produced no resent held RPC in these runs (late_pre always 0).
