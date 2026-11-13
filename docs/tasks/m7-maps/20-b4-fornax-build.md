# 20 (B4) — Fornax blockout: build, bot iteration, evidence

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** S4, H5, H3
**Wave:** 2 build (maps WT, serial)

## Goal
Generate `L_Fornax_Blockout` from the spec module, add the run script and FT row, and iterate the layout until the ADR 0008 §5.3 definition of done holds. Soak sets (quiet host): BotDuel x 3; BotRun x 3; FFA x 10 at 3; hazard_death_self sum <= 6 over FFA x 10 and <= 1 per BotDuel round; RA and Mega taken every round. Not added to `MapsToCook` or the public rotation.

## Scope — files/modules the agent may touch
- `Content/Maps/Fornax/L_Fornax_Blockout.umap` (LFS)
- `Content/Data/MoveScripts/FornaxRun.json`
- `infra/ci/fts.sh` (one row)
- `Tools/editor/maps/golden/fornax.json` (`--golden write`)
- `Tools/editor/maps/fornax_layout.py` (one layout iteration at most before escalation)
- `docs/adr/evidence/0008-fornax-blockout/` (summaries only, <= 5 MB; raw logs to `/mnt/oldroot/velox-scratch/m7-fornax/`)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §5.1, §5.2, §5.3 (map table), §3.5; concept `docs/maps/fornax.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] 5.3 (a) `create_map.py --map fornax --validate-only` green and golden written
- [ ] (b) `navlint.sh L_Fornax_Blockout` PASS (`clusters - edge = 0`, `lowclear = 0`)
- [ ] (c) `FtMapReachability` green in `fts.sh` (with the map's `WalkExempt`)
- [ ] (d) `FornaxRun` PASS at 100 ms / 2 % loss
- [ ] (e) the soak sets above with every map-independent invariant PASS (stuck sum <= 5 per three bots, pro rata) and the map's extra gate
- [ ] (f) evidence under `docs/adr/evidence/0008-fornax-blockout/`
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

### B4 (dev-hard, 2026-11-13/14, branch `wip/m7-20-b4-fornax`, engine 5.8.4)
Evidence `docs/adr/evidence/0008-fornax-blockout/` (README has the table and per-link numbers), raw /mnt/oldroot/velox-scratch/m7-fornax/.
- (a) PASS, golden written (110 solids, 30 items, 5 spawns, 19 links). (b) navlint PASS clusters 0 / lowclear 0 / hazard_m2 96. (c) FtMapReachability PASS (`WalkExempt=DA_Item_Health_Mega MinMajors=4`). (d) FornaxRun PASS 8/8, both pads.
- (e) after the one layout iteration: BotDuel x3 PASS 3/3; BotRun x3 fails only on the idle client's world kills (BotWatch.json parks it at (-2700,-1700,100), outside the 48 x 36 m footprint); FFA x10 at 3 fails link_success[Jump] 88.7 % (Isle_In 81 %, bots overshoot the 300 cm island at ~950 cm/s) and item coverage 27/30 (minor items on side routes, different ones per set). Hazard gate PASS: self 1 over FFA x10, per BotDuel round [0,0,1]; credited FFA 3 / Duel 1. RA and Mega taken every round in every set.
- Chute (F5): the hop guard refuses hops on the 30 deg Chute (45 refusal ticks in 12 traversals, landing predicted at take-off height); bots walk it, no stuck or lava.
- Out-of-list files touched: `mapgen.py` (`MATERIAL_KEYS` + "Pit": the lava bed material was never loaded, generation crashed), `Tests/VeloxMapReachabilityFt.cpp` (`MinMajors=<n>`, default 5: the FT required 5 majors, Fornax has 4), `infra/ci/nettest.sh` (FornaxRun on L_Fornax_Blockout, as B1/B2 did).
- Escalation (rule): (e) still fails after one iteration; follow-ups: Isle_In (bot-side speed cap on short-landing gap hops, or drop the link), minor-item coverage ruling, BotWatch idle spot per map.
