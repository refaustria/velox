# 04h — Duel camping as a precedence rule with a lead horizon

**Milestone:** M4 Bots
**Agent:** dev
**Status:** done
**Depends on:** 04g merged on `m4-bots` (it owns `Source/Velox/Tests/*` until then); ADR 0005 D39 (the rule and the values), D16 (`DuelMinItemScore` stays the trip floor), §7 (camp point, timing path), D13 (hysteresis), D36 (the `BotDuel` bands)

## Goal
A Hard dueller with the Mega or RA down and coming up within 20 s stands at it instead of fetching a weapon it barely wants across the room. Today the camp branch of `VeloxBotDecision::ChooseGoal` is reached only when all four path candidates score below `DuelMinItemScore 3`, which with combat never happens (D39 evidence: 0 of 153 route lines `camp=1`, lowest chosen score 5.0). After this task camping is a precedence decision: a major down with `Lead = TimeUntilRespawn − TravelSeconds ≤ DuelCampMaxLeadSeconds` beats any item scoring under `DuelCampOverScore` (with the D13 hysteresis while already camping). `DuelMinItemScore` keeps its meaning and value.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotDecision.{h,cpp}` — `FVeloxBotDecisionInput` gains `float CampOverScore = 0.f; float CampMaxLeadSeconds = 0.f;` (0 = the plain §7 rule, as the other Duel knobs do). `ChooseGoal`: after the Fight branch and `ItemWithHysteresis`, in Duel: `Camp = DuelCampItem(In)` restricted to `Lead ≤ CampMaxLeadSeconds` (when `CampMaxLeadSeconds > 0`); `bCampCurrent = In.Current.Type == Roam && In.Current.ItemId == Camp->CandidateId`; `Bar = CampOverScore × (bCampCurrent ? Params.GoalSwitchHysteresis : 1)`; camp iff `Camp && (!Item || Item->Score < Bar)`; else `Item` if any; else the existing Roam fallthrough. Keep `DuelCampItem`'s tie rule (smallest lead, then id). Update the header comment of `ChooseGoal` (the §2 rule text).
- `Source/Velox/Bots/VeloxBotItemRouting.{h,cpp}` — `UVeloxBotItemRouter` config `DuelCampOverScore = 7.f` (ClampMin 0) and `DuelCampMaxLeadSeconds = 20.f` (ClampMin 0, Units s) next to `DuelMinItemScore`; copied into `In.CampOverScore` / `In.CampMaxLeadSeconds` where `In.bDuel` is set (both evaluation paths, lines ≈ 697 and ≈ 850). The route line is unchanged (`camp=` already prints the choice); add `lead=%.1f` after `respawn_in=` so the soak can report camp leads.
- `Source/Velox/Tests/VeloxBotTests.cpp` — `Velox.Bots.Decision` rows listed under Acceptance; adapt the existing "Duel camping: nothing scores" row only by filling the two new input fields (7 and 20 leads must still camp the Mega).
- `docs/tasks/m4-bots/04h-duel-camp-gate.md` — the outcome section.
(Anything not listed is out of scope. `Config/DefaultGame.ini` is not touched: the router section does not exist there and the header defaults are the values. No change to `ApplyMinScore`, `CampPoint`, the path-query "camp slot", the controller or the follower.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` D39 (rule, values, the reasoning behind 7 and 20, the rows), §7 (camp point `CampOffsetCm`, the timing path `MaxWaitSeconds × ItemTimingSkill` that takes over for the last 8 s at Hard), D16, D22 (why the junk band sits at 3.5–7), D13 (`GoalSwitchHysteresis`), D36 (`BotDuel` bands)
- Evidence: `scratchpad/a07/out/20261030-090550-714328/nettest/BotDuel-server.log`, `…-091124-723280/…` (the candidate lists on every `VeloxBotRoute` line), `scratchpad/campcheck.py` (the what-if over logged evaluations)
- `Source/Velox/Bots/VeloxBotDecision.cpp` `ChooseGoal` / `DuelCampItem` (the current branch order), `VeloxBotItemRouting.cpp` ≈ 890–1000 (where `In` is filled and the route line is written)

## Acceptance criteria
- [ ] `Velox.Bots.Decision` rows (all Hard params, Duel, no enemy visible, Mega unavailable and reachable unless stated): (1) Mega lead 15 s, best item RL score 4.5 → `Roam` with the Mega's id; (2) lead 15 s, YA score 8.75 → `Item` YA; (3) lead 30 s, RL 4.5 → `Item` RL; (4) current goal = camping the Mega, item score 8.0 → stays camping (8 < 7 × 1.25); same with 9.0 → `Item`; (5) enemy visible, lead 15 s, no item → `Fight` (camping never pre-empts a fight); (6) FFA (`bDuel false`), lead 15 s, no item → random roam point, `ItemId == INDEX_NONE`; (7) the existing "nothing scores → soonest major (7 s vs 18 s lead)" row passes unchanged with the new fields set to 7 / 20.
- [ ] `Velox.Bots.Decision` row: `CampOverScore 0` and `CampMaxLeadSeconds 0` reproduce today's behaviour (camp only when no item is choosable, any lead).
- [ ] `infra/ci/ci.sh` build + tests green (Editor, Client, Server targets compile; `Velox.Bots.*` all pass); `infra/ci/fts.sh` → `[fts] OK n/n` once.
- [ ] One `BotDuel` round on an idle machine (`infra/ci/nettest.sh --port <N> --scripts BotDuel`): `camp=1` route lines ≥ 1, their share of goal-change lines reported (expected 5–20 %), every `camp=1` line's `lead=` ≤ 20, the longest `camp=1` stretch per bot ≤ 28 s, `refused` 0, Mega takes ≥ 1 within 3 s. The strike/dwell bands may still fail on a head without 04i (the ramp void, D41) — report, do not tune.

## Verification steps
1. `infra/ci/ci.sh` → tests green; paste the `Velox.Bots.Decision` tail.
2. `infra/ci/fts.sh --port <N>` → `[fts] OK n/n`.
3. `infra/ci/nettest.sh --port <N> --scripts BotDuel` on an idle machine; paste the round summary and `grep -c camp=1` on the server log, plus the `lead=` values of the `camp=1` lines.

## Report back
Standard dev format. State the unit rows added, the `BotDuel` camp count / share / longest stretch, and whether the round was `CONTENDED` (D40) or clean. Flag for the lead: a camp stretch > 28 s (a camper that never converted to the timing path), any camp on a powerup with lead > 20 (the horizon not applied), `refused` > 0.

## Notes / outcome
- Implemented as specified (`CampOverScore` / `CampMaxLeadSeconds` in `FVeloxBotDecisionInput`, router config 7 / 20, `lead=` on the route line, `DuelCampItem` horizon). Unit rows D39 (1)-(7) plus the "both knobs 0" row in `Velox.Bots.Decision`; ci `build.sh` 96/96 green (Linux; Server/Client targets SKIPPED on the installed engine); `fts.sh` OK 7/7.
- Row (4) deviation: Hard `GoalSwitchHysteresis` is 1.3, not 1.25, so the camping bar is 7 x 1.3 = 9.1. The row uses 8.0 (stays) and 9.2 (-> Item); a 9.0 item still stays camped. ADR D39's "8 < 7 x 1.25" text is off (the lead may fix it).
- `BotDuel` round (port 7951, `CONTENDED` per D40: 21/26 windows over, other agents were running): 78 route lines, 3 `camp=1` (3.8 %, below the expected 5-20 %), all Celer, leads 12.4 / 8.1 / 3.4 (all <= 20), camp stretches ~2 s, ~1.1 s (-> Item Armor.Red), ~0.7 s (-> Fight); longest <= 28 s. refused 0, stuck 0, max_dwell_s 7, Mega takes 4 (within_3s=1). Strike/dwell bands pass this round. A clean idle-machine rerun is still needed for the set judgement.
