# 29 (H10) — Bot fight stall against walls (dwell) + B1c Sacrarium re-soak

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H8 (both touch Bots/*; start after H8 is merged)
**Wave:** bot track (lead-added 2026-11-13 from B1b, docs/tasks/m7-maps/17-b1-sacrarium-build.md §B1b)

## Goal
1. Fix the fight stall B1b found: in FFA ×10 at 6 bots on L_Sacrarium_Blockout, max_dwell reached 7 and 9 s (limit 5). All dwell runs ≥ 6 s are class "other"; six of seven sit within ~38 cm of the aisle side of the atrium ring wall ((444..861,1338) N aisle, (1338,-852/-1147) E aisle, (-1338,1254) W aisle; one at (2662,-1438)). The bot hops against the wall (z 72–182) while firing selfsplash/notready at an enemy it can see but cannot reach. Root-cause it (combat wish vs path, LOS-but-no-path target, strafe into wall, selfsplash guard) and fix it generally (not map-specific). Raw logs: /mnt/oldroot/velox-scratch/m7-sacrarium-b1b/.
2. B1c: move `Ammo_Bullets@(-2500,-1700)` out of the Crypt corner in `Tools/editor/maps/sacrarium_layout.py` to a spot on a normal route (never taken in two of three 3-bot sets), regenerate L_Sacrarium_Blockout, re-run navlint, its fts row and SacrariumRun, then the three B1 soak sets.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/*`, `Source/Velox/Tests/VeloxBot*` (+ an FT reproducing the wall stall if feasible), `infra/ci/fts.sh` rows
- `Tools/editor/maps/sacrarium_layout.py`, `Content/Maps/Sacrarium/L_Sacrarium_Blockout.umap`, `Tools/editor/maps/golden/sacrarium.json` (re-write golden), evidence `docs/adr/evidence/0008-sacrarium-blockout/`
- this file and the B1 task file notes

## Acceptance criteria
- [x] Root cause stated with log evidence; fix covered by a unit test or FT
- [~] Sacrarium: FFA ×10 at 3 bots and at 6 bots — max_dwell ≤ 5, coverage 38/38, all map-independent invariants PASS; BotRun ×3 behaviour PASS (cost windows: A/B rule)
- [x] DM1: BotRun ×3 on L_DM1_Blockout behaviour PASS, no regression in dwell/stuck vs the H7 numbers
- [x] build + full automation + fts.sh green; DM1 golden OK; sacrarium golden updated
- [ ] B1 task (e) ticked if the Sacrarium sets pass

## Notes / outcome
dev-hard, 2026-11-13, branch `wip/m7-29-h10-wall-stall` (base origin/m7-maps), engine 5.8.4. Raw logs /mnt/oldroot/velox-scratch/m7-h10/.

- **Root cause: every bot had PlayerId 0.** The engine sets PlayerIds only in `AGameSession::RegisterPlayer` (human login);
  `AddBot` / the dummy spawner never did (`VeloxItemTimerEntitlement ... pid=0` for all six bots). The bot combat memory
  (`FVeloxBotCombatState::Enemies`, `TargetId`, `FindEnemy`, `IsGoalEnemyVisible`, the router's fight check) is keyed by
  PlayerId, so in a bot-only match every enemy collapsed into one record: `bVisible` = any enemy visible, `Pawn` = the last
  candidate of the PlayerArray loop (perception writes `Info->Pawn = Other` for every PS before the FOV/LOS test). The bot
  aimed at and closed in (FightLeash 2500, closing-in weight 1) on a pawn it could not see, through the atrium ring wall:
  aim ray into the wall 40 cm away = `fire=selfsplash`, closing-in wish into the wall + path blend = speed 0, dodge
  hops = z 72..182. Evidence: B1b ffa6b-9 Sagitta at (857,1338) "vis=1 enemy=VeloxCharacter_13" (Aquila on the loft at
  (-820,-787,710)): the engine's Weapon trace on that segment hits At_N at (828,1300) (`velox.bots` debug trace, removed);
  an instrumented 6-bot run logged every real sighting (404/404 geometrically clear) while the 1 Hz lines showed
  "vis=1" for pawns never sighted (Aquila's target VeloxCharacter_4 while it saw only _2 and _3). Over the B1b 6-bot logs
  60 % of vis=1 samples had a wall between bot and the named target. Not map-specific: DM1 had the same merge
  (H7 BotRun r1 8 s dwell; many through-wall vis=1 samples).
- **Fix (general):** `UVeloxBotSubsystem::AssignUniquePlayerId` gives an AI controller's PlayerState the next id of the
  game session's counter (the one human logins use, so bots, dummies and humans never collide); called from
  `AVeloxBotController::InitPlayerState` and `AVeloxDummyController::InitPlayerState`; `VeloxBot add ... player_id=N`.
  Side effects (all corrections): kill-feed `KillerPlayerId`, `FindPlayerStateById`, knockback-by-PlayerId cheats and the
  hitscan pattern seed (`PatternSeq ^ PlayerId`) now differ per bot. No change to the wall-stall rules (D30 / stand-off)
  was needed.
- **Test:** `velox.bots.FtBotTargets` (FT_BotTargetIdentity, `Tests/VeloxBotFunctionalTest.cpp`, fts row
  `FtBotTargets|L_CombatTest`): three bots, ids unique and non-zero; a Hard watcher with one frozen enemy in sight and one
  behind Cover_03; over 1.5 s its visible target is the seen enemy on every tick. On: PASS x4 (91/91 ticks seen, ids
  260/261/262). A/B with the assignment disabled: ids 0/0/0, target = the hidden pawn on 91/91 visible ticks, FAIL.
- **B1c:** `BulletsAmmo` (-2500,-1700) -> (-1600,-1100), W aisle between the Lightning ammo and the Crypt door (bullets have no
  weapon distance rule). `--validate-only` valid (127 solids, 38 items); L_Sacrarium_Blockout regenerated (no generator change
  since B1, so only the pickup moved); `--golden write` (sacrarium.json unchanged: items are not in the dump); dm1 golden OK.
  navlint PASS (clusters 0, lowclear 0); FtMapReachability-L_Sacrarium_Blockout PASS.
- **Sacrarium sets** (load1 9-33 from other projects throughout): FFA x10 at 6 bots: all invariants PASS, max_dwell **5**
  (B1b 7 / 9; the 5 s run is a strafing fight at speed 125-450, not a wall), dwell>=6 runs 0, sum_stuck 1, Drop 92.4 %,
  JumpPad 98.9 %, coverage 38/38, nav 1.49 s, sum_kills 230 (B1b 101). FFA x10 at 3 bots: dwell 4, stuck 5, JumpPad 100 %,
  coverage 38/38 (Bullets taken), but Drop 89.3 % (109/122) FAIL; repeat: **all invariants PASS** (Drop 92.9 %, stuck 5,
  dwell 4, coverage 38/38). The misses are the loft drops (Drop_Loft_E/W) landing on the gallery 240-330 cm past the link end
  (judged `missed`, EndAcceptance 150), the same links/rate as B1b (10 of 123) and not knockback-related (0 of 13 misses
  with a hit in the drop): marginal on this map, not this change. Per-run FAILs are budget>300 only (cost windows, host).
  BotRun x3 (L_Sacrarium_Blockout, --keep-going): r1 PASS (cost PASS, stuck 0, dwell 3, kills 8); r2/r3 INVALID (frame
  deficit > 10 %, load1 25-33; behaviour stuck 0/1, dwell 4/3, kills 8/6, nav 2.12 s in r2 under load). An earlier pair:
  r1 PASS, r2 cost FAIL (stuck 0, dwell 3/4). Behaviour clean in all 5 rounds; set not judgeable on this host (A/B rule).
- **DM1** BotRun x3 (L_DM1_Blockout): **set PASS** (behaviour PASS, cost PASS), stuck 1/1/0, max_dwell 3/3/3, kills 8/9/11
  (H7: stuck 0/0/0, dwell 8/4/3, kills 3/6/9), majors 4/4, Drop 97.3 %, left_arena none.
- **SacrariumRun FAIL** (not this change): with 0 corrections the script lands on the well floor at (211,607) and its fixed
  yaw -156.5 walks 116 cm past the Quad Flight pad (`loft (pad Quad)`, 8/9 checkpoints), twice identically. A run on the
  B1 umap (origin, same build) took a correction at PadMega and diverged (well floor (214,522), pad hit, round FAIL on the
  correction); two clean runs on the B1 umap: one PASS (well floor (216,588)), one FAIL exactly like mine (well floor
  (213,611), the same checkpoint). The open-loop row 27 ("to Quad Flight pad", yaw -156.5 from wherever the gallery drop
  lands) is on a knife edge of ~20 cm, map-independent of B1c; SacrariumRun.json is outside this task's scope (follow-up:
  steer row 27 at the pad, or a wider landing box upstream).
- Verification: `build.sh --targets VeloxEditor,Velox` green; automation 144/144; `fts.sh` OK 15/15 (incl. FtBotTargets);
  `create_map.py --map dm1 --golden check` OK.

