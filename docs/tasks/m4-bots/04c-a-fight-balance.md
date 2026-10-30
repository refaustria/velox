# 04c-a — Fight vs item balance (must-fix before merge)

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** todo
**Depends on:** 04b, ADR 0005 D13, docs/reviews/m4-05-06-04b-review.md (F1)

## Goal
Bots fight and still route major items, as in the Q3 reference. Implements ADR 0005 D13.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotItemRouting.{h,cpp}`: pure `WeaponStrength(...)` function, the weapon→ammo map in `GatherBotFacts`, the fill in `FinishEvaluation`
- `Source/Velox/Bots/VeloxBotDecision.{h,cpp}`: `GoalSwitchHysteresis` for Fight↔Item in both directions in `ChooseGoal`
- `Source/Velox/Bots/VeloxBotCoreTypes.h` / `VeloxBotSettings.{h,cpp}` if needed for new fields
- `Config/DefaultGame.ini`: FightBaseScore 12, FightFullStrength 200, FightWeakWeaponScale 0.25
- `Source/Velox/Tests/VeloxBotTests.cpp`: unit rows

## Acceptance criteria
- [ ] Unit rows: the 4 worked Medium cases from D13, both hysteresis directions, WeaponStrength (MG-only ≈ 0.2, SG 0.6, RL/RG 1)
- [ ] build.sh green; fts.sh OK 5/5
- [ ] FFA, 3 Medium bots, dedicated, 120 s ×3: pickups ≥ 30/run, Mega ≥ 1/run, kills ≥ 3/run
- [ ] -DisableAdaptiveUnity build succeeds
