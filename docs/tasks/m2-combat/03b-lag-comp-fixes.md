# 03b — Lag compensation fixes from the architect review

**Milestone:** M2 Combat
**Agent:** dev
**Status:** todo
**Depends on:** 03 (review `docs/reviews/m2-03-lag-compensation-review.md`, ADR 0003 Amendments B10–B17). Independent of 04; must be merged before 08.

## Goal
Apply defects 1–5 of the review (§2 of the review file is the authoritative list with file:line and fixes):
1. Ping fallback → `NetworkSimulatedSmoothLocationTime + 0.5 / SampleRateHz`, no `PlayerState` lookup (B13); fix the `.h` comment and `ADR`-quoted comment in `BuildShotTiming`.
2. Eye→origin occlusion trace on the `Weapon` channel in `TraceShot`; blocked → snap to the eye, log `VeloxShotOriginSnap … reason=occluded`; skipped when origin and eye agree within 1 cm (B15).
3. `FVeloxHitboxSample::bDiscontinuity` + `UVeloxCombatSettings::HitboxTeleportDistanceCm` (default 150, cvar mirror `velox.combat.HitboxTeleportDistanceCm`, bootstrap field `hitbox_teleport_distance_cm`); `PoseAtWorldTime`/`PoseAtStamp`/`MakeLiveSample` honour it; nearer-in-time pose across a flagged point, no history reset (B16).
4. NaN guards: `Request.Origin.ContainsNaN()` → snap (`reason=nan`); `RawDir.ContainsNaN()` → ray skipped.
5. `view_ms` field in the `VeloxShot` log line (B17). Optional: `FVeloxShotResult::Rays` inline allocator 12 (shotgun has 11 pellets).

## Scope — files/modules the agent may touch
- `Source/Velox/Net/VeloxLagCompensationSubsystem.h/.cpp`
- `Source/Velox/Combat/VeloxCombatSettings.h/.cpp` (one field + cvar), `Tools/editor/create_bootstrap_content.py` (one field)
- `Source/Velox/Tests/VeloxLagCompTest.cpp`

## Acceptance criteria
- [ ] `LagCompRewind`: new case — synthetic history with a 1000 cm jump between t = 0.5 and t = 0.5 + 1/60; query at 0.5 + 0.4/60 → pre-jump pose, at 0.5 + 0.6/60 → post-jump pose, same by stamp; a 40 cm step is still interpolated.
- [ ] `LagCompTrace`: new case — client origin 50 cm from the eye but behind the test wall → `bOriginSnapped`; a NaN origin → snapped; the `VeloxShot` line contains `view_ms=`.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected (no movement code touched).

## Report back
Standard dev format; list each defect with the change.
