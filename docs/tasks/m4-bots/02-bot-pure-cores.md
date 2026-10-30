# 02 — Bot pure cores: steering, aim model, item value, weapon choice, decision, maneuver

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** todo
**Depends on:** ADR 0005 §2, §4 (maneuver state machine), §5, §7, §8, §9 (no code dependencies; parallel with 01 and 03)

## Goal
Every bot decision rule exists as a world-free function over plain structs with an injected `FRandomStream`, fully unit-tested, before any controller code is written. Task 04/05/06 wire these without changing them. Includes the ADR 0001 §4 mandated tests: aim-error distribution and item-value scoring.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotTypes.h` (new: `FVeloxBotDifficultyParams` POD with every field of the ADR §11 table, `FVeloxBotInput`, enums `EVeloxBotGoal`, `EVeloxNavLinkType` if 01 has not landed yet — coordinate names exactly with ADR §4; the lead merges)
- `Source/Velox/Bots/VeloxBotSteering.{h,cpp}` (new), `Bots/VeloxAimModel.{h,cpp}` (new), `Bots/VeloxItemValue.{h,cpp}` (new), `Bots/VeloxWeaponChoice.{h,cpp}` (new), `Bots/VeloxBotDecision.{h,cpp}` (new), `Bots/VeloxBotManeuver.{h,cpp}` (new: `FVeloxBotManeuver` state machine over `FVeloxManeuverInput { bOnGround, Speed2D, DistToStart, DistToEnd, bImpulseReceived, bSwitchDone, Time }` → `FVeloxManeuverOutput { WishToward (Start|End), bJumpHeld, bFire, PitchDeg, bDone, bAbort, Reason }`)
- `Source/Velox/Tests/VeloxBotTests.cpp` (new)
- All headers include only `CoreMinimal.h` (+ `Math/RandomStream.h`, `GameplayTagContainer.h` for the weapon tag, `Movement/VeloxMoveSim.h` for the steering test); no UObject, no world, no logging.
(Anything not listed is out of scope.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §2 (`ChooseGoal`), §4 (maneuvers, rocket-jump sequence), §5 (`Steer`, `OptimalStrafeAngleDeg`), §7 (`Need`, `Score`), §8 (`FVeloxAimModel::Update`, `LeadPoint`, `ShouldFire` inputs), §9 (`Pick`, preferences), §11 (parameter names and preset values), §14 (test list)
- `docs/adr/0002-movement-component-design.md` §1 (Q3 accelerate clamp — the strafe optimum derives from it), §3 (`FVeloxMoveSim::StepFrame` for the closed-loop steering test), §7 (`StrafeJump` test bound)
- `docs/adr/0003-combat-gas-and-lag-compensation.md` §4 (weapon ranges, projectile speeds), §7 (`FVeloxProjectileSim` for the lead test)

## Acceptance criteria
- [ ] Signatures as in ADR §5/§7/§8/§9 (names may be refined; report changes).
- [ ] `Velox.Bots.StrafeSteering`: closed loop with `StepFrame` (125 ticks at 1/125 s from (800,0) toward +X, Hard params) reaches ≥ 1.25 × `MaxGroundSpeed` with |heading error| ≤ 10°; 90° goal change at 1000 cm/s turns within 60 ticks and never drops below 950 cm/s; walker params never exceed `MaxGroundSpeed + 1`; corner 60° → `bJumpHeld` false; arrive slowdown scales `WishScale` to 0 at the target.
- [ ] `Velox.Bots.AimError`: 20 000 steps at 1/60 s, static target: per-axis sample std within 10 % of σ, mean within 0.05 σ, lag-1 autocorrelation within ±0.05 of `exp(−Dt/τ)`, first aim change after target appearance within `[0.7, 1.3] × ReactionSeconds`. `Velox.Bots.AimLead`: 1000 cm, 600 cm/s perpendicular, rocket 2250 cm/s → projectile passes within 10 cm of the target at impact time. `Velox.Bots.AimTurnRate`: 180° in `180/MaxTurnRate ± 1 tick`.
- [ ] `Velox.Bots.ItemValue`: the ADR §14 rows (Mega at 100 vs 50 health, RA at 200 armor = 0, timing skill 0 vs 1, monotone in travel time, owned weapon = 0.3 × ammo need, powerup beats Mega at equal travel, deterministic ordering).
- [ ] `Velox.Bots.WeaponChoice`: ADR §9 rows. `Velox.Bots.Decision`: ADR §14 rows. `Velox.Bots.Maneuver`: rocket-jump sequence (wait for switch, run-up `RunUpSeconds`, one tick with pitch −80 + jump + fire, abort after 0.3 s without impulse, done on landing near end), jump-fast refuses below 0.9 × required speed.
- [ ] `infra/ci/build.sh` green.

## Verification steps
1. `infra/ci/build.sh` → the new `Velox.Bots.*` tests listed in the automation report, all passing.
2. Paste the sampled aim statistics (std, mean, autocorrelation) from the test log into the report.

## Report back
Standard dev-hard format; list every signature that differs from the ADR and why.

## Notes / outcome
(Filled by the agent or the lead after completion.)
