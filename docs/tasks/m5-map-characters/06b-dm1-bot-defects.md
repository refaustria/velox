# 06b — DM1 bot defects found by task 06 (fight stand-offs, airborne progress strikes, mid-air link outcomes, 2-D arrival)

**Milestone:** M5 First real map + characters
**Agent:** dev-hard
**Status:** done
**Depends on:** task 06 (`06-dm1-bot-iteration.md`, `notes/06-report.md`), ADR 0005 (§4, §5, §10, D18, D27, D28, D30), ADR 0006 §6, §7, E0.3, E2

## Goal
Task 06 left L_DM1_Blockout not green on the map-independent invariants for reasons that are bot defects, not layout:
fight stand-offs (a Fight goal standing 10-22 s, the dwell cap and most `fight_stall` strikes), the D18 progress rule
striking in the air, link maneuvers judged `missed` when the flight was not the link's, and a 2-D arrival test. When
this is done the four are fixed with unit rows, the runner ignores one engine log line, the optional dock N drop move is
evaluated, and BotRun x3, BotDuel x3 and FFA x10 on L_DM1_Blockout meet the invariants on a quiet host while L_ItemTest
stays inside its D51 bands.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotController.{h,cpp}` — stand-off rule, flight pause / ground-only expiry of the progress rule, 3-D arrival
- `Source/Velox/Bots/VeloxBotManeuver.{h,cpp}`, `Source/Velox/Bots/VeloxBotPathFollowingComponent.cpp` — link outcome classification
- `Source/Velox/Tests/VeloxBotTests.cpp`, `Source/Velox/Tests/VeloxBotFunctionalTest.cpp` — unit rows
- `Config/DefaultGame.ini` — the new tunables (`[/Script/Velox.VeloxBotController]`)
- `infra/ci/nettest.sh`, `infra/ci/ffa_summary.py`, `infra/ci/ffa_set.py`, `infra/ci/ffa.sh`, `infra/ci/README.md` — new not-attempt reasons, engine-noise ignore, report-only `fight_yields`
- `Tools/editor/maps/dm1_layout.py`, `Content/Maps/DM1/L_DM1_Blockout.umap` — optional finding 5 (dock N drops on the door axis)
- `docs/adr/evidence/0006-dm1-blockout-06b/` — evidence; this task file

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §4, §5, §10, D18, D27, D28, D30
- `docs/adr/0006-first-map-and-characters.md` §6, §7, E0.3, E2
- `docs/tasks/m5-map-characters/notes/06-report.md`, `docs/adr/evidence/0006-dm1-blockout/`

## Acceptance criteria
- [x] Finding 1: a stand-off rule (number in config) ends Fight stand-offs; rule explained below for the architect.
- [x] Finding 2: the progress rule never strikes airborne; flights straddling the window do not strike (unit rows).
- [x] Finding 3: a link flight that is not the link's (begun in the air, re-launched by an external impulse) is not `missed`; genuine misses stay `missed` (unit rows).
- [x] Finding 4: arrival is 3-D with a vertical tolerance = nav agent height (unit rows).
- [x] Finding 6: exactly the `LogClass: Error: StructProperty FDataflowToolNodeSnapshot::Date` engine line is not counted as `errors=`.
- [x] L_DM1_Blockout (`--map`): BotRun x3, BotDuel x3, FFA x10 meet left_arena / world_kill / no_nav_point / giveup 0, Σ stuck <= 5 per bot set and the FFA stuck bands, max dwell <= 5 s (E2 exclusions), link success >= 90 % per type with >= 30 attempts (>= 95 % observed reported), coverage 35/35.
- [x] L_ItemTest regression: BotRun x3, BotDuel x3, FFA x3 inside the D51 bands.
- [x] `build.sh` green, `fts.sh` 8/8; navlint and FtDm1Reachability green after the layout change.
- [x] Evidence under `docs/adr/evidence/0006-dm1-blockout-06b/` (D48 (4)(ii) recipe), host load and foreign UE processes recorded per set.

## Verification steps
1. `infra/ci/build.sh` -> `CI OK`, all tests successful; `infra/ci/fts.sh --port 8201` -> `OK 8/8`.
2. `infra/ci/navlint.sh /Game/Maps/DM1/L_DM1_Blockout` -> `clusters=0 lowclear=0 result=PASS`.
3. `infra/ci/nettest.sh --map L_DM1_Blockout --scripts BotRun --repeat 3 --keep-going` (and BotDuel), `infra/ci/ffa.sh --map L_DM1_Blockout --repeat 10`.
4. The same three without `--map` (L_ItemTest; FFA `--repeat 3`).

## Report back
Standard dev-hard report.

## Notes / outcome

**Outcome (dev-hard, 2026-11-01, branch `m5-06b-dm1-bot-defects`, code commit 8e95bfd).** All four bot defects are fixed with unit rows. On L_DM1_Blockout, FFA x10, BotRun x3 and BotDuel x3 meet every map-independent invariant on a quiet host. The L_ItemTest regression is inside the D51 bands. `build.sh`: 119 tests, CI OK. `fts.sh`: OK 8/8. Evidence: `docs/adr/evidence/0006-dm1-blockout-06b/` (README).

**Rule for finding 1 (fight stand-off; for architect review).** This extends D30 and uses its reaction unchanged. While the goal is Fight (not forced, not on a link, not already suppressed), the controller samples the bot's feet every 0.25 s. The window is the last `FightStandoffSeconds` (3 s). The rule fires when every sample in that window lies within `FightStandoffRadiusCm` (150 cm, the soak dwell radius) of the bot's current position. When it fires: `FightSuppressedUntil = T + FightStallCooldownSeconds` (4 s) and an immediate route evaluation. The goal decision then does not see the enemy and routes to an item, or roams if no item can be chosen. Perception, aim and fire are untouched, as in D30. The rule writes no stuck strike, only a `VeloxBotFightYield` line (counted report-only as `fight_yields=` by both runners). A goal change or a yield clears the window. Airborne time counts, because dodge-hopping in place is no progress.
It covers the cases D30's speed rule misses: a pathless fight at a ledge corner where dodge hops reset the 2 s grounded-slow clock; a chase whose last seen point is reached (`arrived=1`) or unreachable; a target on another level. A fight that strafes out of the circle never fires. Why a sliding window instead of an anchor: in the first probe, an anchor placed on the way in delayed the yield to 5.7 s.
Numbers: 3 s keeps the worst stand-off at 3 to 4 s against the 5 s cap. 150 cm matches the dwell measure. On DM1 FFA x10 the rule fired 17 times (0 to 6 per run) and `supp1` stayed at 0 to 8 % (D30 expected < 5 %; run 8 shows 8.0 % with 6 yields). On L_ItemTest it never fired.

**Finding 2.** `UpdateStuckProgress` gains `bOnGround` and `bFlight`. A window only expires on a ground tick (a bunny hop touches the ground once per hop, so D18's slab hopper still strikes). While airborne longer than the bot's own hop (`2 x JumpVelocity / |g|` + `StuckProgressFlightSlackSeconds` 0.15 = 0.825 s), the window pauses without the D27 cap and the start distance follows the bot. DM1 FFA x10 had 0 `progress` strikes (task 06 ffa6: 3, all `ground=0`).

**Finding 3.** Two new maneuver results, both counted as not-an-attempt (`aborted`), like `replaced` / `below_start` / `ineligible` (extends E0.3 b). `air_start`: the traversal began in flight and never touched the ground before landing away from End, and the link did not launch it. `relaunched`: an external impulse arrived mid-flight away from End. The first impulse of a pad traversal is the pad's own, and the rocket jump's splash is its own. A drop re-launched at its end level (`HeightAboveEndCm <= EndAcceptanceCm / 2`) or within End acceptance is `ok`. A traversal that started on the ground and lands off the end surface is still `missed`. DM1 FFA x10 had 18 `relaunched` and 0 `air_start`.

**Finding 4.** `VeloxBotRules::IsAtGoal` checks the 2-D acceptance radius plus `GoalMoveToleranceCm`, and `|feet z - target z| <= nav AgentHeight` (140). It is used for the sticky arrival in `EnsureMove`, for `AlreadyAtGoal`, and for move completion. A projected goal that is reached while the goal itself is on another level counts as a failed move (`result=not_at_goal`).

**Finding 5 (optional) was tried and reverted.** Moving the dock N drop links to x = ±350 (after regeneration navlint gave clusters=0 and FtDm1Reachability passed) made those drops worse in a 3-run probe: 8 of 10 missed, against 5 of 5 ok at ±450 in the probe before. The bots drift inward from the walkways and clip the door jamb. The layout is unchanged.

**Finding 6.** `ENGINE_NOISE_RE` (`LogClass: Error: StructProperty FDataflowToolNodeSnapshot::Date `) is excluded from the `Error:` counts in `nettest.sh` (bot rounds and the respawn rule) and from `errors=` in `ffa_summary.py`. The message did not appear in this task's runs, so the ignore has not been exercised live.

**Before -> after (DM1).** FFA x10 (task 06 ffa6, contended host -> this task, quiet host): Σ stuck 7 -> 3, max dwell 10 -> 5 s, Drop 89.2 % -> 94.2 % (131/139), JumpPad 100 % -> 100 % (92/92), coverage 35/35 -> 35/35, errors 0. BotRun x3: stuck [0,0,1] -> [0,0,0], max dwell [3,3,3] -> [3,3,3], Drop 97.9 % -> 100 % (43/43). BotDuel x3: stuck [1,3,0] -> [0,0,0], max dwell [4,2,4] -> [2,2,3], Drop 97.1 % -> 96.8 % (30/31). Observed Drop on DM1 FFA is 94.2 %, below the 95 % that ADR 0006 E0.3 b asks DM1 to observe. The remaining 8 misses are spread over 6 records: dock N 3+1, slab W/E 1+2, A ledge 1.

**Deviations.** The first L_ItemTest BotRun x3 failed round 2 with a 9 s dwell. It was an Item goal under fire, 16 cm from a ramp segment target that did not advance; the D27-bounded progress rule struck at about 6 s. None of the changed rules applies there (the bot was grounded, the goal was Item, and it was not at the goal). The set was re-run once under the E0.3 b rule and passed 3/3. The first `navlint.sh` run used its default port 7777, not 8201-8209.
