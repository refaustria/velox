# 37 (H15) — Fight stand-off dwell in open areas

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done (dev-hard, 2026-11-14)
**Depends on:** H14 (merged; both touch Bots/*)
**Wave:** bot track (lead-added 2026-11-14 from H13, docs/tasks/m7-maps/33-h13-void-edge-deaths.md notes)

## Goal
After H13, Orbita FFA ×10 at 4 bots fails only `max_dwell_s` 6 (limit 5) in 2 runs: two bots in a fight stand-off in the middle of the hub, not at an edge. The 4 Hz stand-off rule (D30 / task 06b) does not catch them. Make bots break stand-offs in open areas generally (strafe circle, reposition, or give up the fight goal) so dwell stays ≤ 5 s on every map, without making them passive. Evidence: /mnt/oldroot/velox-scratch/m7-h13/.

## Scope
- `Source/Velox/Bots/*` (combat / stand-off logic), `Source/Velox/Tests/VeloxBot*` (+ an FT reproducing the hub stand-off if feasible), `infra/ci/fts.sh` rows, B5 evidence/notes, this file

## Acceptance criteria
- [x] Root cause with log evidence (why the 4 Hz stand-off rule misses it)
- [x] Orbita FFA ×10 at 4 bots: max_dwell ≤ 5, all invariants PASS, hazard gate PASS, coverage ≥ 36/37 → tick B5 (e)
- [x] Fornax FFA ×10 at 3 bots: max_dwell ≤ 5 (H14 saw 6 s once, east gallery, item goal — check whether the same stall logic or a separate item-goal stall); hazard gate still PASS
- [x] Sacrarium FFA ×10 at 6 bots and DM1 BotRun ×3: no dwell/stuck regression vs H10/H13 numbers; kills per round not lower than before (bots must not become passive)
- [x] build + automation + fts.sh green

## Notes / outcome
dev-hard, branch `wip/m7-37-h15-standoff` on origin/m7-maps (c334cfe), engine 5.8.4. Raw logs /mnt/oldroot/velox-scratch/m7-h15/
(`fix/` = first build, `v2/` = final build; tri.sh / sim2.py there); evidence `docs/adr/evidence/0008-*/*-h15.txt`,
`0008-orbita-blockout/h15-dwell-classes.txt`.

**Dwell classes before (dwellwhy.py, class=other runs >= 5 samples).** H13 B5b Orbita final set: 4 runs (6, 6, 5, 5) and 26 more over H13 set1-3; H14 B4b Fornax: 1 (6). All but five are **Fight goal, vis=1, path=0 (in range, no path), speed 100-950 cm/s**: two bots
holding range and strafing (flip every 0.5-1.2 s) around a fixed point, often switching targets (Fight -> Fight). The rest are item
goals at 5 s (Quad / Railgun waits, within the limit) and one 14 s item stall in H13 set2 (intermediate build, gone in its final).
Fornax (2013,-682): the same fight class (speed ~800, vis=1) with the Fight goal 4.6 s, then the Railgun item.

**Root cause.** The 06b stand-off test fires when *every* 4 Hz sample of the last 3 s lies within 150 cm of the *current*
position. A strafing bot swings 200-400 cm around its centre, so some sample is always > 150 cm from "now" and the rule never holds,
while the soak's dwell rule (consecutive 1 Hz samples within 150 cm of the run's *first*) sees one circle: Ventus at the hub (36,32)
1 Hz positions within 110 cm for 6 s at speeds 113-800 cm/s, no `VeloxBotFightYield` in the run.

**Fix** (`Bots/VeloxBotController.*`, `Bots/VeloxBotItemRouting.*`; every map):
- `VeloxBotRules::UpdateFightDwell`: the soak's measurement on the bot - 10 Hz samples; a hit when for some anchor sample >= 3 s
  old the samples nearest anchor + 1, + 2, + 3 s all lie within `FightStandoffRadiusCm` 150 of it (every 1 Hz phase). It
  yields like the 06b stand-off (D30 suppression 4 s, router picks an item / roam point; aim and fire untouched), log
  `VeloxBotFightYield ... reason=dwell`. `FightDwellPeriodSeconds` 1 (0 = off). Moving fights (closing in, circle strafe at
  400 cm radius) never fire (unit cases).
- Found on Sacrarium with the first build (FFA x10 at 6: max_dwell 7, three runs >= 6): (a) a Fight -> Item (0.3 s) -> Fight
  flicker reset the window (bot pinned at a balcony wall, Y constant, 7 s); the dwell window now keeps sampling through
  `FightDwellGraceSeconds` 1 s without a Fight goal and fires only while watched (`bMayFire`); SetGoal no longer clears it.
  (b) two 6 s item stalls under Armor_Yellow (1000,0,320): the path ends on the floor 248 cm below (dist 12 cm, speed ~0) where
  the engine's reach test never passes, until the D18 progress strike (pre-existing: H10 ffa3b 4 s there). New:
  `VeloxBotRules::IsUnderGoal` (2-D within acceptance + 50, |dz| > agent height) for `UnderGoalSeconds` 1 s while following the
  path on the ground -> `VeloxBotPath ... result=under_goal`, goal dropped, `UVeloxBotItemRouter::MarkUnreachable`
  (UnreachableRetrySeconds 5, as D24's partial path). Final Sacrarium set: 9 under_goal drops, none on Orbita / Fornax.
- Tests: `Velox.Bots.ControllerRules` stand-off block gains the H15 cases (hub strafe +-300 at 1 s cycle: 06b never, dwell at
  3.0 s; still; closing in; circle strafe; not watched; sampled-not-judged keeps the window; period 0 off) and IsUnderGoal cases.
  No new FT: the hub stand-off needs two bots holding range, not reproducible deterministically on a test map; the rule test
  carries the kinematics. fts rows unchanged.

**Results, final build** (host load from other projects; every per-run FAIL below is budget>300 only):
- Orbita FFA x10 at 4: **all invariants PASS**, max_dwell **5** (B5b 6), coverage **37/37**, stuck 0, hazard gate PASS self 6
  (gate 10), kills **140** (B5b 152; H13 sets 129 / 141 / 148 / 152). First build: dwell 4, kills 152, coverage 36/37.
  -> B5 (e) FFA ticks.
- Fornax FFA x10 at 3: max_dwell **3** (B4b 6), hazard self 0 (gate 6), stuck 0, kills **157** (B4b 155); item_coverage 26/30 FAIL
  (Health_Small x4, the minor-item question pending from B4; first build 28/30). First build: dwell 4, kills 154.
- Sacrarium FFA x10 at 6: **SET PASS**, max_dwell 4 (H10 5), stuck 3, coverage 38/38, kills **235** (H10 230).
- DM1 BotRun x3: set behaviour PASS, kills **7/12/11** (H14 6/7/7, H10 8/9/11), stuck 1/2/0, dwell 4/3/2; cost not judged (host,
  slow throughout). First build: kills 7/9/14.
- build VeloxEditor + Velox, automation 144/144 (CI OK), fts.sh OK 20/20.

**Open.** Fornax minor-item coverage (lead's ruling from B4). The under-goal drop leaves the elevated item to other routes for 5 s;
why the Sacrarium paths end under Armor_Yellow instead of on its pedestal (nav projection) was not root-caused.
