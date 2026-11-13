# 21 (B5) — Orbita blockout: build, bot iteration, evidence

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** S5, H5, H3
**Wave:** 2 build (maps WT, serial)

## Goal
Generate `L_Orbita_Blockout` from the spec module, add the run script and FT row, and iterate the layout until the ADR 0008 §5.3 definition of done holds. Soak sets (quiet host): BotDuel x 3; FFA x 10 at 4; hazard_death_self sum <= 10 over FFA x 10 and <= 2 per BotDuel round. Not added to `MapsToCook` or the public rotation.; `OrbitaRun` flies every pad with and without input; two-client pad check at 100 ms / 2 % loss logged with correction counts

## Scope — files/modules the agent may touch
- `Content/Maps/Orbita/L_Orbita_Blockout.umap` (LFS)
- `Content/Data/MoveScripts/OrbitaRun.json`
- `infra/ci/fts.sh` (one row)
- `Tools/editor/maps/golden/orbita.json` (`--golden write`)
- `Tools/editor/maps/orbita_layout.py` (one layout iteration at most before escalation)
- `docs/adr/evidence/0008-orbita-blockout/` (summaries only, <= 5 MB; raw logs to `/mnt/oldroot/velox-scratch/m7-orbita/`)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §5.1, §5.2, §5.3 (map table), §3.5; concept `docs/maps/orbita.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] 5.3 (a) `create_map.py --map orbita --validate-only` green and golden written
- [ ] (b) `navlint.sh L_Orbita_Blockout` PASS (`clusters - edge = 0`, `lowclear = 0`)
- [ ] (c) `FtMapReachability` green in `fts.sh` (with the map's `WalkExempt`)
- [ ] (d) `OrbitaRun` PASS at 100 ms / 2 % loss
- [ ] (e) the soak sets above with every map-independent invariant PASS (stuck sum <= 5 per three bots, pro rata) and the map's extra gate
- [ ] (f) evidence under `docs/adr/evidence/0008-orbita-blockout/`
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

### B5 (dev-hard, 2026-11-13, branch `wip/m7-21-b5-orbita`)
Summary in `docs/adr/evidence/0008-orbita-blockout/README.md`. (a) PASS, (b) PASS (bring-up: FILL_NAV_KINDS platform/wall, K1/K2/HO_Rack2
moved, five clip caps), (d) OrbitaRun PASS 12/12 (every pad idle and forward held), two-client check 12/12 + 12/12 with 2 / 0
corrections (max 1.75 cm). (c) FAIL and (e) FAIL: 5 of 6 pad links span 2-4 nav tiles (`VeloxNav link_span`, TileSizeUU 1000) and never
connect, so Scopulus is unreachable for bots and only P3 is ever used; hazard_death_self 182 over FFA x10 at 4 (gate 10) and
10/19/22 per BotDuel round (gate 2), credited 96 / 14; G1 JUMP gap 5 ok vs 137 aborted (H11). Layout iteration not spent (no
layout cause). Out-of-scope edits: `infra/ci/nettest.sh` (OrbitaRun map + per-launch judging), `Tools/editor/mapgen.py`
(`expected_counts` without `clip: 0`). Follow-ups: per-map nav tile size or split pad links (bot/nav), bot edge falls on 400 cm decks.
