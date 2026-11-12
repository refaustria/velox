# 18 (B2) — Nemus blockout: build, bot iteration, evidence

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** S2, P5
**Wave:** 2 build (maps WT, serial)

## Goal
Generate `L_Nemus_Blockout` from the spec module, add the run script and FT row, and iterate the layout until the ADR 0008 §5.3 definition of done holds. Soak sets (quiet host): BotRun x 3; FFA x 10 at 3; FFA x 10 at 6; dwell on the Corona inside the DM1 camp band (report); nav build time recorded. Not added to `MapsToCook` or the public rotation.; the `JUMP_FAST` gap link is removed if its success is < 90 % (concept section 15); moves to dev-hard if the failure is link success

## Scope — files/modules the agent may touch
- `Content/Maps/Nemus/L_Nemus_Blockout.umap` (LFS)
- `Content/Data/MoveScripts/NemusRun.json`
- `infra/ci/fts.sh` (one row)
- `Tools/editor/maps/golden/nemus.json` (`--golden write`)
- `Tools/editor/maps/nemus_layout.py` (one layout iteration at most before escalation)
- `docs/adr/evidence/0008-nemus-blockout/` (summaries only, <= 5 MB; raw logs to `/mnt/oldroot/velox-scratch/m7-nemus/`)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §5.1, §5.2, §5.3 (map table), §3.5; concept `docs/maps/nemus.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] 5.3 (a) `create_map.py --map nemus --validate-only` green and golden written
- [ ] (b) `navlint.sh L_Nemus_Blockout` PASS (`clusters - edge = 0`, `lowclear = 0`)
- [ ] (c) `FtMapReachability` green in `fts.sh` (with the map's `WalkExempt`)
- [ ] (d) `NemusRun` PASS at 100 ms / 2 % loss
- [ ] (e) the soak sets above with every map-independent invariant PASS (stuck sum <= 5 per three bots, pro rata) and the map's extra gate
- [ ] (f) evidence under `docs/adr/evidence/0008-nemus-blockout/`
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
