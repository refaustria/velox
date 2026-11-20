# 19 (B3) — Culmen blockout: build, bot iteration, evidence

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** S3, P5
**Wave:** 2 build (maps WT, serial)

## Goal
Generate `L_Culmen_Blockout` from the spec module, add the run script and FT row, and iterate the layout until the ADR 0008 §5.3 definition of done holds. Soak sets (quiet host): BotRun x 3; FFA x 10 at 3; FFA x 10 at 6; one FFA x 10 at 8 report-only (cost not judged); Haste and both far 50 HP taken. Not added to `MapsToCook` or the public rotation.; roof pads agree with `DumpPadLandings` within 60 cm

## Scope — files/modules the agent may touch
- `Content/Maps/Culmen/L_Culmen_Blockout.umap` (LFS)
- `Content/Data/MoveScripts/CulmenRun.json`
- `infra/ci/fts.sh` (one row)
- `Tools/editor/maps/golden/culmen.json` (`--golden write`)
- `Tools/editor/maps/culmen_layout.py` (one layout iteration at most before escalation)
- `docs/adr/evidence/0008-culmen-blockout/` (summaries only, <= 5 MB; raw logs to `/mnt/oldroot/velox-scratch/m7-culmen/`)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §5.1, §5.2, §5.3 (map table), §3.5; concept `docs/maps/culmen.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] 5.3 (a) `create_map.py --map culmen --validate-only` green and golden written
- [ ] (b) `navlint.sh L_Culmen_Blockout` PASS (`clusters - edge = 0`, `lowclear = 0`)
- [ ] (c) `FtMapReachability` green in `fts.sh` (with the map's `WalkExempt`)
- [ ] (d) `CulmenRun` PASS at 100 ms / 2 % loss
- [ ] (e) the soak sets above with every map-independent invariant PASS (stuck sum <= 5 per three bots, pro rata) and the map's extra gate
- [ ] (f) evidence under `docs/adr/evidence/0008-culmen-blockout/`
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

### B3 (dev-hard, 2026-11-14, branch `wip/m7-19-b3-culmen`, engine 5.8.4)
Evidence `docs/adr/evidence/0008-culmen-blockout/README.md` (table, per-link numbers), raw /mnt/oldroot/velox-scratch/m7-culmen/.
- (a) PASS, golden written. (b) navlint PASS clusters 0 / lowclear 0, nav 1.09-1.20 s (`NAV_TILE_SIZE_UU` 4000). (c) FtMapReachability
  PASS (`WalkExempt=DA_Item_Weapon_Railgun`); all 11 pad landings within 29.1 cm of `DumpPadLandings` (roof pads <= 1.8 cm).
  (d) CulmenRun PASS 8/8, 7 pads, corrections 0.
- (e) NOT ticked: BotRun x3 set PASS; FFA x10 at 3 fails max_dwell_s 34 + coverage 40/46, at 6 fails max_dwell_s 11 (coverage 46/46);
  every other invariant PASS. All >= 6 s dwells are bots stranded on the 250 cm kiosk tops after a jump/fall from the Viaduct (S3 D6).
  Extra gate: Haste every round; both far 50 HP taken in the 6-bot set (H50_E not in the 3-bot s3 set). JUMP_FAST and RJ: 0 attempts.
- Layout: S10 off the terrace edge, BrSE X-mirrored (bring-up defect), one item iteration (NW shards, T_H5_4). Escalation (rule):
  kiosk fix (taller kiosks or drop links off their tops) + re-run of the FFA sets.
- Haste DA regenerated (`DisabledInModes` Duel). Out-of-list: `infra/ci/nettest.sh` (CulmenRun on L_Culmen_Blockout, 7 pads / 8
  checks, as B1/B2/B4/B5 did), `Content/Items/DA_Item_Powerup_Haste.uasset` (lead request).

### B3 resume (dev-hard, 2026-11-20, branch `wip/m7-19-b3-culmen`, engine 5.8.4) — reported back for split/escalation
Evidence `docs/adr/evidence/0008-culmen-blockout/README.md` (first-run files moved to `pre-kiosk/`), raw /mnt/oldroot/velox-scratch/m7-culmen/run2/.
- WIP verified: validate-only, golden, fts row, nettest row, Haste DA `DisabledInModes` Duel; Editor + Game build green (merge with H14/H15).
- The one layout iteration: kiosks 250 -> 700 (`KIOSK_TOP_Z`; jump apex 114 cm, out of reach from the 320 decks and the 480 Porticus top).
- (a) PASS, golden rewritten. (b) navlint PASS clusters 0 / lowclear 0, nav 1.14-1.29 s. (c) FtMapReachability PASS 7/7; all 11 pad landings
  within 29.1 cm of `DumpPadLandings` (roof pads <= 1.8 cm). (d) CulmenRun PASS 3/3 rounds, 8/8 checks, corrections 0.
- (e) NOT ticked: BotRun x3 set PASS (round 3 per-round `allunreach_air` 1, B6); FFA x10 at 6 and at 8 every invariant PASS (dwell 4, coverage 46/46);
  FFA x10 at 3 FAIL item_coverage 43/46 (H50_E, T_H5_1, T_H5_3, all east Trench health), every other invariant PASS. Dwell fixed (34 s -> 4 s).
  Extra gate: Haste 10/10 in every set; H50_E 0/10 at 3 bots, 5/10 at 6, 3/10 at 8; H50_W 2/6/8 of 10.
- Escalation (rule, iteration budget spent): item iteration T_H5 row onto the Drop_TE_Trench landing -> P11 line and H50_E nearer P11
  (as task 38 for Fornax), then re-run FFA x10 at 3 (+ at 6 to keep coverage); or a B6 ruling on minor health coverage at 3 bots.
