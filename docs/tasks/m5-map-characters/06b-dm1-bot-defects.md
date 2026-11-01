# 06b — DM1 bot defects found by task 06 (fight stand-offs, airborne progress strikes, mid-air link outcomes, 2-D arrival)

**Milestone:** M5 First real map + characters
**Agent:** dev-hard
**Status:** in progress
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
- [ ] Finding 1: a stand-off rule (number in config) ends Fight stand-offs; rule explained below for the architect.
- [ ] Finding 2: the progress rule never strikes airborne; flights straddling the window do not strike (unit rows).
- [ ] Finding 3: a link flight that is not the link's (begun in the air, re-launched by an external impulse) is not `missed`; genuine misses stay `missed` (unit rows).
- [ ] Finding 4: arrival is 3-D with a vertical tolerance = nav agent height (unit rows).
- [ ] Finding 6: exactly the `LogClass: Error: StructProperty FDataflowToolNodeSnapshot::Date` engine line is not counted as `errors=`.
- [ ] L_DM1_Blockout (`--map`): BotRun x3, BotDuel x3, FFA x10 meet left_arena / world_kill / no_nav_point / giveup 0, Σ stuck <= 5 per bot set and the FFA stuck bands, max dwell <= 5 s (E2 exclusions), link success >= 90 % per type with >= 30 attempts (>= 95 % observed reported), coverage 35/35.
- [ ] L_ItemTest regression: BotRun x3, BotDuel x3, FFA x3 inside the D51 bands.
- [ ] `build.sh` green, `fts.sh` 8/8; navlint and FtDm1Reachability green after the layout change.
- [ ] Evidence under `docs/adr/evidence/0006-dm1-blockout-06b/` (D48 (4)(ii) recipe), host load and foreign UE processes recorded per set.

## Verification steps
1. `infra/ci/build.sh` -> `CI OK`, all tests successful; `infra/ci/fts.sh --port 8201` -> `OK 8/8`.
2. `infra/ci/navlint.sh /Game/Maps/DM1/L_DM1_Blockout` -> `clusters=0 lowclear=0 result=PASS`.
3. `infra/ci/nettest.sh --map L_DM1_Blockout --scripts BotRun --repeat 3 --keep-going` (and BotDuel), `infra/ci/ffa.sh --map L_DM1_Blockout --repeat 10`.
4. The same three without `--map` (L_ItemTest; FFA `--repeat 3`).

## Report back
Standard dev-hard report.

## Notes / outcome
(Filled at the end.)
