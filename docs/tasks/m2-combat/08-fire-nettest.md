# 08 — FireRun and RocketJumpRun soak scripts

**Milestone:** M2 Combat
**Agent:** dev
**Status:** todo
**Depends on:** 03b, 04, 05, 07

## Goal
Implement ADR 0003 implementation-notes task **`08-fire-nettest`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Content/Data/MoveScripts/FireRun.json`, `RocketJumpRun.json`, `Source/Velox/Movement/VeloxScriptedInputComponent.*` (fire/weapon-select events), `infra/ci/nettest.sh`, `infra/ci/README.md`

## Acceptance criteria
- [ ] `CrouchRun` added to the default `nettest.sh` script list.
- [ ] FireRun: ≥ 9/10 rail hits on a strafing dummy at 100 ms / 2 %, 0 rejected MG shots; RocketJumpRun: ≤ 1 non-echo correction ≤ 5 cm (A17 acceptance); 5/5 rounds.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Review notes from task 03 (architect, 2026-10-25; ADR 0003 amendments B13–B17 are binding; review `docs/reviews/m2-03-lag-compensation-review.md`)
What `FireRun` must assert from the server log (`VeloxShot seq= shooter= hit= rewind_ms= view_ms= hint= dist_cm= hits= rays= snap=`; run the server with `velox.lagcomp.Debug 1` so `VeloxLagCompRewind` per-pawn lines exist for diagnosis, and `velox.combat.LagComp 1`):
- **Hint mode:** `hint=stamp` on ≥ 9 of the 10 rail shots. The script aims at the dummy's rendered centre with `aim_err_cm ≤ 30`, so the centre ray intersects the rendered capsule (radius 37.5) and a hint must exist unless the lane is occluded; `hint=fallback` means the stamp pose fell outside `MaxRewind` (ping too high for the window) and `hint=none` means the client-side hint search failed — both are failures at 100 ms.
- **Rewind window:** every rail shot `rewind_ms ∈ [60, 250]`. Expected ≈ 160 ms at 100 ms RTT (one-way 50 + Linear steady-state lag 100 + update age ≈ 8); record the median in `STATUS.md`. A median < 100 or > 220 indicates a wrong `InterpolationDelay` or a dummy that never left rest (B12/B13).
- **Domain agreement:** per shot `|rewind_ms − view_ms| ≤ 60` ms, mean over the 10 shots ≤ 30 ms. `view_ms` is the world-time estimate the unhinted pawns get; `rewind_ms` is the exact stamp rewind. This is the only place the `ViewServerTime` formula is checked against ground truth — if it fails, fix `BuildShotTiming`, not the threshold.
- **Origin:** `snap=0` on all shots (the shooter is stationary or moving < 1400 cm/s; a snap at 100 ms means the fire RPC arrived more than two moves ahead of the move RPC — report, do not hide).
- **Hits:** `hits=1` on ≥ 9/10 rail shots (existing acceptance). MG 40 shots: `VeloxShotRejected` count 0; `hint` is not judged for the MG (spread may miss the rendered capsule).
- **Control round** (`velox.combat.LagComp 0`, reported *and* one hard rule): compensated hits must be strictly greater than control hits. A ±600 cm/s dummy moves ≈ 96 cm in 160 ms, well beyond the capsule radius, so a control round that hits as often as the compensated one means compensation is not acting. Log both counts.
- Also assert `VeloxLagCompRegister pawn=<dummy>` once and no `VeloxLagCompRewind … mode=current` for the dummy while `LagComp 1` (a `current` means the dummy had no history at fire time).
`RocketJumpRun` is unchanged by this review.

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
