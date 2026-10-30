# 06 — Bot item routing: value vs travel time, respawn timing, duel camping, goal hysteresis

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** done with open items (merged; notes/06-report.md; follow-up 04b)
**Depends on:** 04; ADR 0005 §2, §7. Parallel with 05 (file ownership in the README).

## Goal
Bots choose items by Q3-style value / travel time with respawn timing through `UVeloxItemRegistrySubsystem`, re-evaluate on a cadence and on take events, keep goals with hysteresis, switch nav filters when rocket-jump eligibility changes, and in Duel camp the next major item when nothing else is worth going for.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotItemRouting.{h,cpp}` (new: candidate pre-scoring, top-K `FindPathSync`, `FVeloxItemScoringInput` assembly from the PlayerState attributes / inventory / registry, `OnPickupTaken` hook, Duel camping target), `Bots/VeloxBotController.cpp` — `UpdateGoal(Dt)` only (06 owns it; 05 owns `UpdateCombat`/Act), `Bots/VeloxBotSubsystem.cpp` — path-query counter for the budget line only
- `Source/Velox/Tests/VeloxBotTests.cpp` — `Decision` rows exercising the wiring-adjacent pure functions (own section)
- `Source/Velox/Tests/VeloxBotFunctionalTest.cpp` — `FT_BotItemRouting` (optional if the headless acceptance run below is scripted as a functional test instead; either is fine)
(Anything not listed is out of scope. `Items/` is read-only through the ADR 0004 §9 API.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §2 (`ChooseGoal`, hysteresis, Roam/camping), §7 (`Need`/`Score`, candidates, travel time, timing, camping), §4 (filter switching), §13 (≤ 4 path queries per evaluation, staggered)
- `docs/adr/0004-game-loop-items-modes-ui.md` §1, §2 (standing pawn retakes on respawn), §9 (bot API, `BotBaseValue` tiers), C10

## Acceptance criteria
- [ ] Every evaluation runs at most `PathQueryCandidates` `FindPathSync` calls; evaluations are staggered so no server frame runs two bots' evaluations; `OnPickupTaken` on the goal item re-evaluates immediately.
- [ ] `FVeloxItemScoringInput` filled from the live PlayerState attributes (`UVeloxHealthSet`, `UVeloxAmmoSet`), inventory (`OwnsIndex`), the item definition and the registry (`IsAvailable`, `GetTimeUntilRespawn`); unreachable items (partial path) score 0.
- [ ] Headless `L_ItemTest?listen?game=FFA?Bots=3?BotDifficulty=Medium` for 120 s: every `bMajorItem` taken at least once by a bot (`VeloxPickup item=… by=<bot name>`), `VeloxBotStuck` ≤ 2, `VeloxBotBudget mean_us ≤ 300`.
- [ ] Headless `L_ItemTest?listen?game=Duel?Bots=2?BotsFill=1?BotDifficulty=Hard` for 180 s: the match reaches `Live` without a human; Mega taken within 3 s of a respawn at least twice (timing), camping visible in `velox.bots.Debug` output (`goal=Roam item=Health.Mega`).
- [ ] Filter switching: a Hard bot below `MinHealthForRocketJump` with a rocket-jump route re-queries with `_Hop` (log `VeloxBotFilter`).
- [ ] `infra/ci/build.sh` green.

## Verification steps
1. The two headless runs above with `-ExecCmds="velox.bots.Debug 1"`; grep `VeloxPickup`, `VeloxBotStuck`, `VeloxBotBudget`, `VeloxMatch phase=`.
2. `infra/ci/build.sh`.

## Report back
Standard dev-hard format with a design summary for `architect` review (evaluation cost, timing behaviour observed, any registry API gap).

## Notes / outcome
(Filled by the agent or the lead after completion.)
