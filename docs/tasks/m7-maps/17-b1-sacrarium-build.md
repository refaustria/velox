# 17 (B1) — Sacrarium blockout: build, bot iteration, evidence

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** S1, P5
**Wave:** 2 build (maps WT, serial)

## Goal
Generate `L_Sacrarium_Blockout` from the spec module, add the run script and FT row, and iterate the layout until the ADR 0008 §5.3 definition of done holds. Soak sets (quiet host): BotRun x 3; FFA x 10 at 3 bots; FFA x 10 at 6 bots (band basis); coverage 38/38. Not added to `MapsToCook` or the public rotation.

## Scope — files/modules the agent may touch
- `Content/Maps/Sacrarium/L_Sacrarium_Blockout.umap` (LFS)
- `Content/Data/MoveScripts/SacrariumRun.json`
- `infra/ci/fts.sh` (one row)
- `Tools/editor/maps/golden/sacrarium.json` (`--golden write`)
- `Tools/editor/maps/sacrarium_layout.py` (one layout iteration at most before escalation)
- `docs/adr/evidence/0008-sacrarium-blockout/` (summaries only, <= 5 MB; raw logs to `/mnt/oldroot/velox-scratch/m7-sacrarium/`)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §5.1, §5.2, §5.3 (map table), §3.5; concept `docs/maps/sacrarium.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] 5.3 (a) `create_map.py --map sacrarium --validate-only` green and golden written
- [ ] (b) `navlint.sh L_Sacrarium_Blockout` PASS (`clusters - edge = 0`, `lowclear = 0`)
- [ ] (c) `FtMapReachability` green in `fts.sh` (with the map's `WalkExempt`)
- [ ] (d) `SacrariumRun` PASS at 100 ms / 2 % loss
- [ ] (e) the soak sets above with every map-independent invariant PASS (stuck sum <= 5 per three bots, pro rata) and the map's extra gate
- [ ] (f) evidence under `docs/adr/evidence/0008-sacrarium-blockout/`
- [ ] A task that still fails an invariant after one layout iteration is reported back to be split/escalated, not retried blindly
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
(Filled by the agent or the lead after completion.)

### B1b - soak gate after H7 (dev, 2026-11-13, branch `wip/m7-17b-sacrarium-soak`, base origin/m7-maps incl. H7, engine 5.8.4)
Fresh build of origin/m7-maps, no layout change. Raw output `/mnt/oldroot/velox-scratch/m7-sacrarium-b1b/`, summaries `docs/adr/evidence/0008-sacrarium-blockout/b1b/`.
Host load1 was 10-20 during all sets (other projects, B2 soaks); map has no band file (report-only). Pad links: VeloxNavLink_22/23/24/25 = ok/(ok+missed).
- FFA x10 at 3 bots: invariants all PASS except item_coverage 37/38 (untaken `Ammo_Bullets@(-2500,-1700)`, Crypt corner; same item as in B1, but H7's own 3-bot set and both 6-bot sets took it). JumpPad 100.0 % (163/163; _22 40/40, _23 23/23, _24 52/52, _25 48/48), Drop 91.9 %, sum_stuck 5, max_dwell 3, nav 1.58 s. Per-run FAILs are budget>300 only (cost windows, host load).
- FFA x10 at 6 bots: FAIL on max_dwell_s 7 (3 runs >= 6) and nav_build_s 2.381 (one run under load; 1.93 s on the repeat). JumpPad 98.4 % (242/246; _23 53/55), Drop 90.0 %, sum_stuck 3, coverage 38/38. Repeat (same build): max_dwell_s 9 (3 runs >= 6), nav 1.93 PASS, JumpPad 98.8 % (242/245; _23 36/37), Drop 91.9 %, sum_stuck 2, coverage 38/38. So the pad success (B1: 82-86 %) and the stuck sum are fixed by H7; max_dwell > 5 recurs at 6 bots only.
- BotRun x3 (map L_Sacrarium_Blockout, port 7791 because 7777 is taken): set behaviour PASS (stuck sum 1, max_dwell 3/3/4, majors 4/4, JumpPad 37/37, left_arena 0); round 1 behaviour miss (kills 2) on a round with 4 contended windows, round 3 one cost window: set FAIL on cost only (host load, band=none).
- Dwell triage (`dwellwhy.py 6`, 7 runs of >= 6 s over the two 6-bot sets, all class=other): not the Rocket Launcher wait. All sit on the aisle side of the atrium ring wall within 38 cm of it ((444..861,1338) N aisle, (1338,-852/-1147) E aisle, (-1338,1254) W aisle; one at (2662,-1438), Mega chapel side): the bot hugs the wall with goal fight/item (Armor_Yellow, RL, LG, SG), enemy vis=1, fire=selfsplash/notready, z hopping 72..182 cm against the wall (a rocket user firing at an enemy it cannot reach, or jump-pressing the wall toward the YA_E / gallery goal). A cluster at the one structure, but the cause is bot behaviour at a wall (fight stall against an enemy on the other side / gallery), not a layout defect I can fix in `sacrarium_layout.py` with a justified, single change; no layout iteration was made (the one iteration stays unspent).
- (e) is NOT ticked: 6-bot max_dwell_s 7 / 9 (limit 5) and 3-bot item coverage 37/38. Suggested follow-up: a bot task (fight against an enemy behind the atrium ring wall / wall-hugging hop; `dwellwhy` rows in the evidence) or an accepted-deviation ruling for max_dwell on 6-bot sets; optionally move `BulletsAmmo` out of the Crypt corner.
