# 07 — Bot soak in nettest, budget assertion, playtest checklist, roadmap ticks

**Milestone:** M4 Bots
**Agent:** dev
**Status:** todo
**Depends on:** 05, 06; ADR 0005 §13, §14

## Goal
Bots run under the standard headless net soak with a human client at 100 ms / 2 %: `BotRun` (FFA with 3 bots and a playing client) and `BotDuel` (two bots duel, spectating client) are in `nettest.sh`'s default list with pass rules, the budget is asserted, and the human has a Mac playtest checklist. ROADMAP M4 boxes are ticked where the evidence exists.

## Scope — files/modules the agent may touch
- `infra/ci/nettest.sh` — `BotRun`, `BotDuel` (map/URL options per ADR §14, `?SpectatorOnly=1` client for `BotDuel`, `-VeloxWatch=<s>` or `velox.bots.Watch` client mode, pass rules, report-only client corrections for `BotRun`), default list; `infra/ci/README.md`
- `Content/Data/MoveScripts/BotWatch.json` (new: one 90 s row, no input)
- `Source/Velox/Bots/VeloxBotCheats.cpp` — `velox.bots.Watch [seconds]` (client: logs kill-feed entries, phase changes, pickup events by bots; quits after N seconds); `Source/Velox/Movement/VeloxScriptedInputComponent.cpp` — only if the watch mode needs the quit path shared
- `docs/playtests/m4-bots-checklist.md` (new), `ROADMAP.md` (M4 ticks with evidence), `STATUS.md`
(Anything not listed is out of scope.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §13 (budget), §14 (soak rules, tooling)
- `docs/tasks/m1-movement/05-debug-tooling-and-net-soak.md`, `docs/tasks/m3-gameloop/04-item-test-map.md` (how scripts and rules are added), `infra/ci/nettest.sh` (structure, per-script map selection)

## Acceptance criteria
- [ ] `BotRun`: server `L_ItemTest?game=FFA?Bots=3?BotDifficulty=Medium`, playing client with `BotWatch` 90 s; pass iff nav built, ≥ 1 kill by a bot, ≥ 10 `VeloxPickup by=<bot>`, ≥ 2 `VeloxBotLink result=ok`, `VeloxBotStuck ≤ 2`, no `Error:`/`ensure`, `VeloxBotBudget mean_us ≤ 300`; client corrections reported, not judged.
- [ ] `BotDuel`: server `L_ItemTest?game=Duel?Bots=2?BotsFill=1?BotDifficulty=Hard`, spectator client 120 s; pass iff `phase=Countdown` → `Live` without a human, ≥ 3 frags, ≥ 1 Mega take by a bot within 3 s of its respawn, budget as above.
- [ ] `nettest.sh --scripts BotRun,BotDuel --repeat 3` 3/3 on an idle machine; the full default list (now 13 scripts) passes once.
- [ ] `docs/playtests/m4-bots-checklist.md`: Mac PIE dedicated server + 2 clients + 4 bots at 100 ms / 2 %: bot movement readability, strafe-jumping in the corridor, pad use, rocket jumps (Hard), duel timing of Mega/RA, difficulty feel Easy→Expert, `AddBot`/`KickBot` from a client, scoreboard `BOT`, kill feed names, no visible bot teleports/snaps; sign-off block.
- [ ] ROADMAP M4 criteria ticked with the evidence paths; STATUS updated.

## Verification steps
1. `infra/ci/nettest.sh --port <N> --scripts BotRun,BotDuel --repeat 3 --keep-going` → 3/3; paste the result tails.
2. `infra/ci/nettest.sh --port <N>` (default list) once.

## Report back
Standard dev format; include the soak numbers (kills, pickups, link traversals, budget mean/max) per round.

## Notes / outcome
(Filled by the agent or the lead after completion.)
