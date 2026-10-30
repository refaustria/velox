# 04i — Floor void under the yellow-armour ramp: seal it on L_ItemTest, one on-mesh extent, D17 fallback below step height

**Milestone:** M4 Bots
**Agent:** dev (part A, content) + dev-hard (part B, rule) — two dispatches, part A first
**Status:** todo
**Depends on:** 04g merged on `m4-bots` and its acceptance soak done on the unchanged map (part A changes the map, part B touches 04g's files); ADR 0005 D41 (evidence, content rule, the three rule changes), D17 (off-mesh recovery), D35 (the segment check's projection), 04e (`FILL_NAV_KINDS`)

## Goal
A bot can no longer end up walking at floor level under the thin yellow-armour ramp with every nav projection snapping to the ramp surface above it (Duel round 2 of the 07 smoke: 10 strikes, a burst and an 18 s dwell at (10, 1172)–(135, 1200), D41). Part A seals the void on `L_ItemTest` so the test map obeys the content rule every map follows from M5 on. Part B makes the bot rules honest about it on any map: one vertical extent for "am I on the mesh" everywhere (controller, router, D35 check), and a D17 fallback target that is never above the bot's feet.

## Scope — files/modules the agent may touch
Part A (dev):
- `Tools/editor/create_item_test_map.py` — in `build_ramp()`: three fill boxes of kind `platform` under `Ramp_YellowArmor` (folder `YellowArmor`, names `Ramp_Fill_1..3`), 400 wide in Y like the ramp, 200 long in X, each box from Z 0 to the ramp underside height at its low-X end: X [−400, −200] top 33, [−200, 0] top 86, [0, 172.7] top 140 (underside = (X + 600) × tan 15° − 20 / cos 15°; residual clearance under the ramp < 60 cm everywhere; nothing under X < −400 where the clearance is already < 55). Update the module docstring and the expected actor counts (`"platform": 4 → 7`).
- `Content/Maps/L_ItemTest.umap` (and its built nav data if saved) — regenerated with the script, headless editor on this machine as 04e did; LFS.
- `docs/tasks/m4-bots/04i-under-ramp-void.md` — outcome.
Part B (dev-hard):
- `Source/Velox/Bots/VeloxBotSettings.{h,cpp}`, `Config/DefaultGame.ini` — `OnMeshHalfHeightCm = 100.0` under `[/Script/Velox.VeloxBotSettings]` (Category `Bots|Navigation`, ClampMin 10, Units cm).
- `Source/Velox/Bots/VeloxBotController.cpp` — `UpdateOffMeshRecovery`: the `bOnMesh` test projects with `FVector(AirborneProjectRadiusCm-equivalent 100, 100, OnMeshHalfHeightCm)` instead of `INVALID_NAVEXTENT`; the `nearest` fallback rejects a point with `Location.Z > Feet.Z + StepHeight` (log `result=no_nav_point` as today when nothing is left; the attempt counter and the D17 terminal escape are unchanged). Update the comment that says the default query extent is what a path query's start projection uses — that is still true for `MoveTo`, which is why the controller must not issue goal moves while `bOffMeshRecovery` (it does not today; keep it so).
- `Source/Velox/Bots/VeloxBotItemRouting.{h,cpp}` — `GroundProjectHalfHeightCm` removed; the ground projection reads `UVeloxBotSettings::Get().OnMeshHalfHeightCm`.
- `Source/Velox/Bots/VeloxBotPathFollowingComponent.cpp` — the D35 segment check's `ProjectPointToNavigation` uses the same extent (one line; D35's rule is unchanged).
- `Source/Velox/Tests/VeloxBotFunctionalTest.cpp` — `velox.bots.FtOffMesh` case 3 "under a thin walkable surface": stage a 20 cm thick static box 170 cm above open floor (e.g. centred (1500, −500, 180), 600 × 600), rebuild nav at runtime (as `FtSegmentBlocked` stages its geometry), place the Medium peaceful bot beneath it with a forced Item goal; assert: no `VeloxBotOffMesh … recovered` while the feet are inside the slab's footprint, every logged recovery target Z ≤ feet Z + step height, the bot outside the footprint and `recovered` within 10 s, goal reached within 25 s. `Source/Velox/Tests/VeloxBotTests.cpp` — one `Velox.Bots.ControllerRules` row if the fallback's Z rule can be expressed world-free (optional).
- `docs/tasks/m4-bots/04i-under-ramp-void.md` — outcome.
(Anything not listed is out of scope. Part A does not touch C++; part B does not touch the map. The 100 cm slot north of the ramp top stays: it is a real-map shape and the D17 bed.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` D41 (the full evidence chain and the three rule changes), D17 (recovery design, `no_nav_point`, `giveup`), D35 (04g's check; its projection extent is what part B switches), D32 (`AgentHeight 140` stays), 04e notes (`fill_collision_underneath_for_navmesh`, how `L_ItemTest` was regenerated headless)
- Evidence: `scratchpad/a07/out/20261030-091124-723280/nettest/BotDuel-server.log` 08:12:36–08:12:57 (Celer: `VeloxBotOffMesh … target=nearest to=(12.75, 1330, 170.99)`, `recovered at=(12.75, 1337.79, 2.15)`, nine strikes at X 10.41 / 135.14, `VeloxBotRoute … offmesh` at the same spots)
- `Tools/editor/create_item_test_map.py` `build_ramp()` (ramp construction: `RAMP_X0 −600`, `RAMP_LEN 800`, 15°, `thick 20`, top platform from X 172.7), `FILL_NAV_KINDS`
- `Source/Velox/Bots/VeloxBotController.cpp` `UpdateOffMeshRecovery` (≈ 757–900), `VeloxBotItemRouting.cpp` `BeginEvaluation` (the ground/airborne projection)

## Acceptance criteria
Part A:
- [ ] `velox.bots.DumpNav` at (10, 1190, 10), (135, 1198, 10) and (−300, 1200, 10) finds no poly and no projection within (100, 100, 100); the ramp surface and the top platform keep their polys (`FtNavigation`, `FtItemRouting` unchanged, YA still taken in the FFA soak).
- [ ] `infra/ci/fts.sh` → `[fts] OK n/n`; one `BotRun` round and one `BotDuel` round on an idle machine: no `VeloxBotStuck` with Y in [1000, 1400] and X in [−600, 172] (under the ramp), YA taken ≥ 1 per round.
Part B:
- [ ] `FtOffMesh` case 3 passes three times headless; cases 1–2 unchanged.
- [ ] `Velox.Bots.*` tests green; Editor/Client/Server compile.
- [ ] Ten-run FFA set (04g's runner, D36 bands) on an idle machine on the head with 04g + part A + part B: every D36 band holds (Σ strikes ≤ 30, no run > 7, bursts 0, pocket strikes 0, dwell ≤ 8, link dt ≤ 8, Σ kills ≥ 70, `no_nav_point` 0, `giveup` 0, budget ≤ 300 on uncontended rounds). The `VeloxBotOffMesh … recovered` count and the `no_nav_point` count are reported with positions; a `no_nav_point` on `L_ItemTest` after part A is a bug report (nothing should be unreachable from the floor there).

## Verification steps
Part A: regenerate the map headless (04e's command), `velox.bots.DumpNav` the three points, `infra/ci/fts.sh --port <N>`, `infra/ci/nettest.sh --port <N> --scripts BotRun,BotDuel` once; paste the DumpNav output and the round summaries.
Part B: `infra/ci/ci.sh`; `infra/ci/fts.sh --port <N>` three times; the ten-run FFA set; paste `summary.out` and the set totals.

## Report back
Standard dev / dev-hard format. Part A: the three box transforms as placed, actor counts, DumpNav output. Part B: the extent value in effect at the three call sites, the FtOffMesh case-3 log tail (`to=` Z vs feet Z), the ten-run set numbers. Flag for the lead: any `giveup` (the terminal escape firing on a test map), any recovery target above the feet in any log, any strike inside a platform footprint.

## Notes / outcome
(Filled by the agent or the lead after completion.)
