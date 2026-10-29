# 08g report — shot origin validated against the eye at `FireMoveTimeStamp` (ADR 0003 B36)

**Result:** done (dev-hard, 2026-10-29). Base `e0f491b`. Not committed.

## Design
- `FireMoveTimeStamp` moved into `FGameplayAbilityTargetData_VeloxShot`, serialised as a full float after `ShotSeq` and before the hint bit. The projectile subclass keeps its type, `NetSerialize` (it forwards to the base) and `ToString`. The hitscan client fills it from `GetClientMoveTimeStamp()`. `Validate` rejects a non-finite stamp with `time` for both payloads and copies it into `FVeloxValidatedShot` for both.
- `FVeloxEyeRing`: a fixed 128-entry ring of `FVeloxEyeSample {float Stamp; FVector Eye;}`, about 4 KB because `FVector` is double. It is pushed after `Super::MoveAutonomous` on the authority. A lower stamp resets the ring; an equal stamp replaces the newest entry. `VeloxEyeRing::Lookup` handles exact match, linear interpolation between neighbours, newer than newest (false), older than oldest (returns the oldest) and empty or NaN (false). `UVeloxMovementComponent::GetServerEyeAtMoveTime` wraps it.
- `VeloxShotValidation::ValidateOrigin(World, Shooter, Eye, ClientOrigin, Tol, OutReason)` is the overload; the old signature forwards with the live eye. `VeloxShotValidation::ResolveShotEye(Shooter, bHasStamp, Stamp, MaxRewind)` is used by `TraceShot` and `LaunchServerShot`. It returns the ring eye or the live eye, plus `late_ms = (GetServerClientMoveTimeStamp - Stamp) x 1000`.
- `FVeloxShotRequest` gains `FireMoveTimeStamp` and `bHasFireMoveTimeStamp`; these are set only on the remote-hitscan path. The member default `MaxRewindSeconds` is now 0.30. `late_ms` is appended to the `VeloxShot` and `VeloxProjectileShot` lines, and `late_ms`/`eye=ring|live` to `VeloxShotOriginSnap`. The rewind clamp is unchanged.
- `velox.combat.DebugHoldShotRpc <ms> <slot> [count=1]` (non-shipping) lives in `VeloxGameplayAbility_FireHitscan.cpp`. `OnServerTargetData` copies the handle, arms a world timer and re-enters itself, logging `VeloxShotRpcHold seq=`. `EndAbility` clears the timer.

## Departures
1. **Stamp is clamped before the ring lookup.** `ResolveShotEye` looks up the eye at `ClampFireMoveTimeStamp(stamp, last processed move, MaxRewindSeconds)`, not at the raw stamp. Without this, a client could choose an eye up to 1.07 s old (the "older -> oldest" rule). Honest cost: when `late_ms` exceeds 300, the eye comes from the clamp bound, at most about 20 ms (12 cm) off.
2. **FireRun rewind rule.** The upper bound of FireRun's rail `rewind_ms ∈ [60, 250]` rule is now `250 + max(0, late_ms)`. A rail whose RPC was resent needs exactly that much more rewind. Before the change, 3 of 6 FireRun rounds at 50 ms failed only on this rule, every time on a rail with `late_ms` 117–200.
3. **Hold command location.** The hold command is in the hitscan `.cpp` (hitscan only), not in `VeloxWeaponCheats.cpp` as 08f plans. When merging, keep one registration and one `Take(slot)` state, and call it from both `OnServerTargetData`s.
4. **Header touched outside the listed files.** `VeloxGameplayAbility_FireHitscan.h` gained two `FShotInput` fields and the hold timer members. `VeloxGameplayAbility_FireProjectile.h` had its own field removed, as the task requires.

## Verification
- `infra/ci/build.sh`: Editor and Velox targets built; Server and Client SKIPPED (installed engine); 71/71 tests passed. Logs: `Saved/CI/20261029-224241-4026133`. New tests: `Velox.Combat.EyeRing`. Extended tests: `ShotValidation` (overload rows with a wall; hitscan stamp `time` rows; `ResolveShotEye` fallback) and `TargetDataSerialize` (stamp is an exact float).
- **FireRun at 50 ms ×3.**
  - First set: 2/3 passed.
  - Rerun: 1/3 passed.
  - After departure 2: 3/3 passed (`Saved/CI/20261029-231036-4079728`).
  - Across all three sets: 11/11 rail hits in every round except one 10/11, 0 rejections.
  - `late_ms` is 0 on shots whose RPC was not resent. Resent shots and the next shot behind them measured 42–200.
- **FireRun `--lag 125` ×3, two sets.** Both sets fail on the 50 ms-specific rules (`rewind ≤ 250`, `stamp ≥ n−1`).
  - Origin snaps 0 and rejections 0 in both sets.
  - Rail hits 9/11, 11/11 and 10/11 in the first set, 11/11 ×3 in the rerun.
  - First set only: `ClientActivateAbilityFailed` in 2 rounds (3 lines) and 3 rails without a server line, which is the B37 switch timeline (08h).
- **FireRunHold ×3: 3/3.** 0 snaps, 0 rejections. Held rails, as `late_ms / rewind_ms / hit` (all `hint=fallback`):

  | Round | seq 1 | seq 2 | seq 3 |
  |---|---|---|---|
  | 1 | 321.3 / 300 / miss | 333.4 / 300 / miss | 316.7 / 300 / hit |
  | 2 | 333.4 / 300 / miss | 316.7 / 300 / miss | 283.4 / 300 / miss |
  | 3 | 308.4 / 300 / miss | 325.0 / 300 / miss | 308.4 / 300 / miss |

  A first FireRunHold set ran before the analysis branch was wired, so its PASS is void; its held lines still showed late_ms 302.5, 291.7 and 325.0 with no snap.
- **RocketJumpRun ×5: 5/5.** Splash lines equal.
- **Totals over 10 soak invocations:** 0 `VeloxShotOriginSnap` lines (so no `err_cm` was seen) and 0 `VeloxShotRejected`.

## Open
- Held rails miss 8 of 9 because the rewind is clamped at 300 ms. This is the trigger B35 names for "hitscan in the move stream". It is for the architect and the human to decide; question 19 (0.35) does not cover a 300 ms hold either.
