# 04c-a — Fight vs item balance (must-fix before merge)

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** partially done (merged), balance follow-up open
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

## Notes / outcome
dev-hard, 2026-10-30. Code + unit rows done; build.sh 95 green, fts 5/5, unity build OK. Also edited VeloxBotFunctionalTest.cpp ControllerRules expectations (3 lines, ini values).
FFA 3 Medium, 120 s, seeds 1–6: pickups 25/32/22/34/20/31 (mean 27, bar 30; 04b 14–21), Mega 0/1/1/0/0/0, kills 7–11, stuck 0–3.
Now leans to items: Fight 0.3–7 % of goals (07 expects 30–60 %). Bots hold MG ~70 % (respawn), MG threshold ≤ 7.5; Fight needs Bias×Fight > Item×1.3 under an Item goal. Pickup shortfall attributed to routing: three bots race for the same majors, evaluations target not-yet-up items, strafe overlay slows Item goals, Mega scores ~9–11 vs YA 15–28. Open for architect: contention-aware routing, empty-table WeaponStrength = 1.
