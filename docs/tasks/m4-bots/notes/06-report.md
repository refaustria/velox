# M4-06 bot item routing — agent report (2026-10-30)
build.sh 92 tests in the worktree (base without 05), 0 failures (`Saved/CI/20261030-034732-275739`). FT `velox.bots.FtItemRouting quit` PASS (Full → rockets; health 60 → filter Hop within 0.07 s, rockets unreachable, switch to Mega; max 1 path/frame). FFA 3 Medium bots 120 s ×4: all 7 majors taken every run; `VeloxBotStuck` 0/3/2/5 (criterion ≤ 2 failed in 2 runs). Duel dedicated Hard 2 bots 180 s: Mega within 3 s of respawn in most cycles (0.006–2.5 s), one run stranded bot off-mesh.
- `UVeloxBotItemRouter` (server world subsystem); `AVeloxBotController::UpdateGoal` → `UpdateBot` → Unchanged / NewGoal / Roam. Rules in `VeloxBotItemRouting`. Evaluation as a state machine: begin (facts, straight-line pre-score, ≤ PathQueryCandidates: current/camp item, Duel next major, best pre-scores) → one `FindPathSync` per frame → final frame scores + `ChooseGoal`. One bot evaluates at a time, router ≤ 1 step/frame.
- Triggers: cadence, goal lost, respawn/take of a camped item, nav-filter change (`VeloxBotFilter`). Travel = max(cost, length)/speed; partial/failed = 0, 5 s skip cache. Duel camping `CampOffsetCm` 200. `In.Fight` defaults `// 04b`.
## Open
1. Stuck: 11/15 at the inner corners of L_ItemTest's central cross; nav `AgentRadius` 37.5 = capsule radius → paths hug corners. Fix: agent radius ~45 or follower corner offset.
2. Duel with `?listen` → host takes a slot; acceptance needs dedicated server.
3. Bot stranded off-mesh (duel6, (−2221, 1540), ~250 cm up) with no recovery; routing now skips off-mesh evaluations (`VeloxBotRoute offmesh`).
4. Refinements (architect): `DuelMinItemScore` 3, `DuelMajorItemMinNeed` 0.8, `CommittedScore` (gate widened by travel at commit), refusal guard 1.5 s/10 s, airborne start projection.
5. 05 merge hooks: `FillEnemyInput` (`// 05 merge`) from `GetCombatState()`; path-query hook → `RecordBotTick`; controller's own MoveTo not counted.
6. FT console-only → add to 04b's `fts.sh`.
