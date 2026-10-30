# 07 — Bot soak in nettest, budget assertion, playtest checklist, roadmap ticks

**Milestone:** M4 Bots
**Agent:** dev
**Status:** todo
**Depends on:** 05, 06, 04b, and task 04c (D13 fight balance, D18 hop guard, D15 combat-events FT, D14/D16/D17 fixes — `docs/reviews/m4-05-06-04b-review.md` §3); ADR 0005 §13, §14, D13–D19

## Goal
Bots run under the standard headless net soak with a human client at 100 ms / 2 %: `BotRun` (FFA with 3 bots and a playing client) and `BotDuel` (two bots duel, spectating client) are in `nettest.sh`'s default list with pass rules, the budget is asserted, and the human has a Mac playtest checklist. ROADMAP M4 boxes are ticked where the evidence exists.

## Scope — files/modules the agent may touch
- `infra/ci/nettest.sh` — `BotRun`, `BotDuel` (map/URL options per ADR §14, `?SpectatorOnly=1` client for `BotDuel`, `-VeloxWatch=<s>` or `velox.bots.Watch` client mode, pass rules, report-only client corrections for `BotRun`), default list; `infra/ci/README.md`
- `Content/Data/MoveScripts/BotWatch.json` (new: one 90 s row, no input)
- `Source/Velox/Bots/VeloxBotCheats.cpp` — `velox.bots.Watch [seconds]` (client: logs kill-feed entries, phase changes, pickup events by bots; quits after N seconds); `Source/Velox/Movement/VeloxScriptedInputComponent.cpp` — only if the watch mode needs the quit path shared
- `docs/playtests/m4-bots-checklist.md` (new), `ROADMAP.md` (M4 ticks with evidence), `STATUS.md`
(Anything not listed is out of scope.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §13 (budget), §14 (soak rules, tooling), D13 (fight balance band and the one tuning knob), D14 (`max_fire_us` report), D15 (`VeloxBotHear`/`VeloxBotDodge` counts), D16 (`refused` = 0), D17 (`VeloxBotOffMesh` metrics), D19 (`fts.sh OK n/n`)
- `docs/reviews/m4-05-06-04b-review.md` §4 (what this task measures and why)
- `docs/tasks/m1-movement/05-debug-tooling-and-net-soak.md`, `docs/tasks/m3-gameloop/04-item-test-map.md` (how scripts and rules are added), `infra/ci/nettest.sh` (structure, per-script map selection)

## Acceptance criteria
- [ ] `infra/ci/fts.sh` → `[fts] OK 6/6` three consecutive times on the idle machine (D19: FtItemPickup, FtNavigation, FtStrafeCorridor, FtItemRouting, FtOffMesh, FtCombatEvents).
- [ ] `BotRun`: server `L_ItemTest?game=FFA?Bots=3?BotDifficulty=Medium` with `-ExecCmds="velox.bots.Debug 1"`, playing client with `BotWatch` 90 s; **pass iff** nav built, ≥ 2 kills by bots, ≥ 25 `VeloxPickup by=<bot>` (D13 band; was ≥ 10 before the fight-score ruling), ≥ 1 `VeloxPickup item=Health.Mega by=<bot>`, ≥ 2 `VeloxBotLink result=ok`, `VeloxBotStuck ≤ 2`, `VeloxBotOffMesh … result=no_nav_point` = 0 and `result=giveup` = 0, `VeloxBotRoute … refused` = 0, no `Error:`/`ensure`, `VeloxBotBudget mean_us ≤ 300`; across the 3 repeats every `bMajorItem` (Mega, RA, YA, 4 powerups) is taken by a bot at least once. **Reported, not judged:** client corrections, `max_us` / `max_path_us` / `max_fire_us` per 5 s window, `paths=/s`, `VeloxBotRouteStats max_deferred_frames`, `VeloxBotHear` and `VeloxBotDodge` counts, `VeloxBotOffMesh … recovered` count, and the `goal=` histogram of the `VeloxBot` debug lines (Fight share at Medium expected 30–60 %; `fire=noammo` share noted).
- [ ] `BotDuel`: server `L_ItemTest?game=Duel?Bots=2?BotsFill=1?BotDifficulty=Hard` with `velox.bots.Debug 1`, spectator client 120 s; pass iff `phase=Countdown` → `Live` without a human, ≥ 3 frags, ≥ 1 Mega take by a bot within 3 s of its respawn, ≥ 1 `VeloxBotRoute … camp=1` line, budget / off-mesh / refused rules as above; reported: `VeloxBotDodge` count (Hard bots facing rockets), Mega takes within 3 s per respawn cycle.
- [ ] `nettest.sh --scripts BotRun,BotDuel --repeat 3` 3/3 on an idle machine; the full default list (now 13 scripts) passes once.
- [ ] **Tuning knob (D13), the only one this task may turn:** if `BotRun` misses the pickup band, `FightBaseScore` moves in steps of 2 within [8, 16] in `Config/DefaultGame.ini` — down when pickups < 25, up when kills by bots < 2 in 90 s — and the final value with its three-round numbers goes in the report. Any other balance change (bias, `FullStrength`, `WeakWeaponScale`, item tiers, cadence) goes back to `architect`.
- [ ] `docs/playtests/m4-bots-checklist.md`: Mac PIE dedicated server + 2 clients + 4 bots at 100 ms / 2 %: bot movement readability, strafe-jumping in the corridor, pad use, rocket jumps (Hard), duel timing of Mega/RA, difficulty feel Easy→Expert, `AddBot`/`KickBot` from a client, scoreboard `BOT`, kill feed names, no visible bot teleports/snaps; sign-off block.
- [ ] ROADMAP M4 criteria ticked with the evidence paths; STATUS updated.

## Verification steps
1. `infra/ci/fts.sh --port <N>` three times → `[fts] OK 6/6` each.
2. `infra/ci/nettest.sh --port <N> --scripts BotRun,BotDuel --repeat 3 --keep-going` → 3/3; paste the result tails.
3. `infra/ci/nettest.sh --port <N>` (default list) once.

## Report back
Standard dev format; per round: kills by bots, pickups by bots (total and per major item), link traversals, `VeloxBotStuck`, off-mesh recoveries, refused count, hear/dodge counts, budget `mean_us` / `max_us` / `max_path_us` / `max_fire_us` / `paths=/s` / `max_deferred_frames`, the `goal=` histogram; the final `FightBaseScore` if it was moved. Flag for the lead: `max_fire_us` > 2000 µs in more than a quarter of the windows (D14 → Combat profiling task), any `refused` (D16 → `Need`/pickup-rule bug), any `giveup` (D17), `max_deferred_frames` > 30 (review §3 minor 8).

## Notes / outcome
(Filled by the agent or the lead after completion.)
