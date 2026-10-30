# 04d — Balance follow-up: Mega need, arsenal-scaled weapon need, routing speed under fire, airborne-start blacklist

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** done except D24 band (nav islands in platform boxes, see notes)
**Depends on:** 04c-a (merged), ADR 0005 D20–D26 (the 2026-10-30 follow-up ruling on D13); may run in parallel with 04c-b (disjoint files), but the two FFA soaks must not overlap on the machine

## Goal
Three Medium bots on `L_ItemTest` take the Mega every cycle, stop hoarding weapons once they are armed, route at full speed while an enemy is in sight, and never blacklist their best items because an evaluation started mid-flight on a jump pad. Implements ADR 0005 D21 (Mega need over `MaxHealth`), D22 (`OwnedWeaponStrength` → unowned-weapon need), D23 (`FightPathWeight 1.5`, data), D24 (no `Unreachable` from an airborne start, `start=` tag, root cause named) and the D20 diagnostics (`wstr=` in the debug line, `fight=` in the route line). Verified against the D20 bands with the 04c-a runner.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxItemValue.{h,cpp}` — `FVeloxItemScoringInput::OwnedWeaponStrength` (new, default 1), `Need`: hard-cap health deficit over `MaxHealth` (D21), unowned weapon `lerp(1, UnownedWeaponNeedFloor, OwnedWeaponStrength)` (D22; the floor arrives as a new field of `FVeloxItemScoringParams`, filled by `MakeParams` from `UVeloxBotSettings`, or as an input field — the agent picks the cleaner one and says which)
- `Source/Velox/Bots/VeloxBotItemRouting.{h,cpp}` — `FVeloxRoutingBotFacts::WeaponStrength` filled once per evaluation after `GatherBotFacts` (both in `BeginEvaluation` for the pre-score and `FinishEvaluation`), `MakeScoringInput` copies it; the evaluation record remembers whether the start was projected from an airborne pawn; `FinishEvaluation` adds to `S.Unreachable` only for a grounded start (D24); the `VeloxBotRoute` line gains `fight=%.2f` (`FightItemBias × FightScore`) and `start=ground|air`
- `Source/Velox/Bots/VeloxBotSettings.{h,cpp}`, `Config/DefaultGame.ini` — `UnownedWeaponNeedFloor=0.3` under `[/Script/Velox.VeloxBotSettings]`; new section `[/Script/Velox.VeloxBotCombatSettings]` with `FightPathWeight=1.5` (D23; the header default stays 0.5 — the ini carries the project value like the D13 values)
- `Source/Velox/Bots/VeloxBotController.cpp` — the `VeloxBot` debug line gains `wstr=%.2f` (the last evaluation's `WeaponStrength`, stored on the bot state by the router); nothing else in the controller (04c-b owns its stuck rule)
- `Source/Velox/Tests/VeloxBotTests.cpp` — rows in `Velox.Bots.ItemValue`, `Velox.Bots.ItemRouting`, `Velox.Bots.Decision` as listed below
- `Source/Velox/Bots/VeloxBotFunctionalTest.cpp` — only if `ControllerRules` or `FtItemRouting` expectations read the changed values (D13 precedent: three lines)
- Root cause of D24, **only if the fix is a one-liner** (e.g. `SetCanEverAffectNavigation(false)` on `AVeloxJumpPad`'s mesh in `Source/Velox/Movement/` or `Items/`, or a filter flag in `GetNavFilter()`); anything larger is reported with the evidence, not done
- The FFA runner: copy `scratchpad/a04c-a/ffa.sh` into your own scratchpad and fix its patterns (`DA_Item_Health_Mega`, `DA_Item_(Health_Mega|Armor_Red|Armor_Yellow|Powerup_[A-Za-z]+)`), add the `goal=` histogram, the armed fight share (`goal=fight` among `vis=1` lines with `wstr ≥ 0.5`), the count of all-candidates-`!unreachable` evaluations split by `start=`. It stays in the scratchpad (07 brings the soak into `nettest.sh`).
(Anything not listed is out of scope: no changes to `FightBaseScore`/`FullStrength`/`WeakWeaponScale`, the bias or hysteresis tables, item tiers, cadence, `PathQueryCandidates`, the follower, the combat overlay's logic.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` — D4 (fight score), D13 (values, `WeaponStrength`, hysteresis), D16 (`Unreachable`, airborne projection), **D20–D26** (this task's spec, worked numbers and bands), §7 (`Need`/`Score`)
- `docs/tasks/m4-bots/04c-a-fight-balance.md` "Notes / outcome" (the symptom), `docs/reviews/m4-05-06-04b-review.md` §1a and minor 10
- Soak logs of 04c-a: `/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad/a04c-a/run1`, `run2` (`ffa-N.log`); the D24 repro positions are in the ADR amendment's evidence paragraph (`grep -a "VeloxBotRoute" | grep -a "unreachable.*unreachable.*unreachable"`, then the preceding `VeloxBot name=<bot>` line: `ground=0 link=JumpPad`)
- Code: `VeloxItemValue.cpp` `Need`, `VeloxBotItemRouting.cpp` `GatherBotFacts`, `MakeScoringInput`, `WeaponStrength`, `BeginEvaluation` (airborne probe), `FinishEvaluation` (`S.Unreachable.Add`), `VeloxBotCombat.cpp` `Overlay` (`bBlendPath`, `In.PathWeight`), `VeloxBotController.cpp` `Describe` (debug line)

## Acceptance criteria
- [ ] `Velox.Bots.ItemValue`: Mega need at health 100 = 1.0, 125 = 0.75, 150 = 0.5, 200 = 0, 50 = 1.0; `Score(Mega, 100, 2 s)` = 100/3; the old "2/3 ratio" row replaced (D21). Unowned weapon need = 1 at `OwnedWeaponStrength 0`, 0.3 at 1.0, 0.845 at 0.22 (floor 0.3); owned-weapon need unchanged (0.3 × ammo need); a `MaxHealth`-capped health item unchanged (D22).
- [ ] `Velox.Bots.ItemRouting`: `MakeScoringInput` carries `OwnedWeaponStrength` = `WeaponStrength(BotFacts, MediumPrefs)` (RL owner 1.0, MG-only 20/90, nothing 0, empty table 1 — the existing row stays); a partial result with an airborne start does **not** add to `Unreachable`, a partial result with a grounded start does (two rows, or one FT check if the state machine is easier to drive there).
- [ ] `Velox.Bots.Decision`: the four D13 worked cases still pass; add the two D22 cases from the ADR — 125/0 RL vs {YA 17.5, LG 5.5} → Item YA; 125/100 RL under an Item goal vs {LG 5.5, YA-at-50-armor 8.75, RA-at-100-armor 11.25} → Fight; 125/100 RL vs Mega 18.75 at 3 s under an Item goal → Item.
- [ ] `VeloxBot` debug line has `wstr=`; `VeloxBotRoute` has `fight=` and `start=`; `Config/DefaultGame.ini` has `UnownedWeaponNeedFloor=0.3` and `[/Script/Velox.VeloxBotCombatSettings] FightPathWeight=1.5`.
- [ ] D24 root cause named in the report with the evidence (which poly the airborne probe projected to, which filter the query used, why the path was partial); fixed if a one-liner, else listed as a follow-up.
- [ ] `infra/ci/build.sh` → `[ci] CI OK`; `infra/ci/fts.sh` → `[fts] OK n/n` (n = the current count); `-DisableAdaptiveUnity` build succeeds.
- [ ] FFA, 3 Medium, dedicated, 120 s, seeds 1–3 (the corrected runner), **D20 bands:** `VeloxPickup … by=<bot>` ≥ 20 per run and ≥ 75 over the three; `DA_Item_Health_Mega` by a bot ≥ 1 per run; kills ≥ 5 per run; `VeloxBotStuck` ≤ 3; `refused` 0; `Error:`/`ensure` 0; all-candidates-unreachable evaluations with `start=air` = 0. **Reported:** `goal=` histogram (Fight share, expected 10–30 %), armed fight share (expected ≥ 20 %), Mega count and take times, weapon pickups vs total, deaths, `start=ground` all-unreachable count, `VeloxBotBudget mean_us` (debug on; report-only here).
- [ ] If a band is missed: report the three-run numbers and stop; do not move `FightBaseScore`, the floor or the path weight (D26 — those return to the architect).

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`; run the three `Velox.Bots.*` sections and paste the new rows' output.
2. `infra/ci/fts.sh --port <N>` → `[fts] OK n/n`.
3. `ffa.sh <port> 120 3 <outdir>` (corrected runner, `SEED0=0`) on the idle machine (not while 04c-b soaks) → the per-run summary lines; paste them and the three-run sums.
4. For D24: reproduce one all-unreachable evaluation from the 04c-a logs' positions (`velox.bots.DumpNav` from `(1500, 1120, 287)` and `(−1735, 1199, 480)` to the RA/Mega/Railgun pickups, or a log of the projected start in `BeginEvaluation` behind `velox.bots.Debug`) and state the cause.

## Report back
Standard dev-hard format: result, what changed (files, the two new settings, the D24 cause and whether it was fixed), verification (commands + output tails, the unit rows, the three summary lines), open issues. Flag for the lead: any band missed (with numbers), the D24 cause if not fixed, `VeloxBotBudget mean_us` > 300 with debug on, any `refused` or `giveup`.

## Notes / outcome
(Filled by the agent or the lead after completion.)

## Notes / outcome (lead, from the dev-hard report, 2026-10-30)
build.sh 95 green, fts 6/6, unity build OK. FFA 3 Medium 120 s seeds 1–3: pickups 51/47/47 (sum 145, weapons 89), Mega 4/4/3, kills 10/9/9, stuck 0/0/1, refused/giveup 0, Fight share 34–37 %, armed fight share 52–64 %, budget mean 57–66 µs. All D20 bands pass except `start=air` all-unreachable 5/6/2 (band 0).
D24 cause: Recast leaves isolated nav islands at floor level inside the solid Mega/RA (300 cm) and ramp-top (207 cm) platform boxes; the airborne probe (feet − 300) projects onto the island (z ≈ 10), whose paths are always partial. The ShouldMarkUnreachable rule stops the blacklist; the band only reaches 0 once the islands are gone. Proposed fix (a): `bFillCollisionUnderneathForNavmesh` on the platform boxes in `Tools/editor/create_item_test_map.py` + regenerate L_ItemTest. Check StartRoam projection on the same islands.
Implementation choice: D22 floor is an input field (`FVeloxItemScoringInput`), carried via `FillDerivedBotFacts`. Extra diagnostic `VeloxBotRouteAllPartial`.
Architect ruling (2026-10-30, ADR 0005 D33): the D24 "impossible" wording was wrong (the rule stops the blacklist, not the count; the count measures the 04e islands and becomes a 07 band once 04e is merged). Fight share 34–37 % is accepted; the report-only expectation is now 25–45 % (armed ≥ 40 %). No balance change.
