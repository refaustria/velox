# 03b — Test maps: edge-strip lint rule, seals and fills, one regeneration with `Layout:` tags, lint baseline

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** done (with one open item: FtOffMesh `ra_strip` / `mega_strip`, see outcome)
**Depends on:** 02 (library), 03 (lint), ADR 0006 E0.2 (e) and E0.4

## Goal
The three test maps are regenerated exactly once from the task-02 library (so they carry `Layout:` tags), with the fixes ADR 0006 E0.2 (e) orders — the two 100 cm slots north of the Mega and RA platforms sealed, `cover`/`platform` boxes filled underneath for nav — and the `.umap`s are committed. The lint learns the **edge strip** kind (one-cell strips along off-lattice ledges; report-only, not gated). The report-only lint output of the three maps after this task is the baseline the lead compares against whenever a test map is regenerated later. Soak results of `L_ItemTest` stay inside the D51 bands.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxNavCoverageMath.h` (edge-strip classification), `Source/Velox/Bots/VeloxNavCoverage.cpp` (`kind=edge`, summary field `edge=<n>`, gate count excludes edges), `Source/Velox/Tests/VeloxBotTests.cpp` (`Velox.Bots.NavCoverageClusters` rows), `infra/ci/navlint.sh` (print `edge=`; PASS/FAIL on `clusters − edge` and lowclear)
- `Tools/editor/create_item_test_map.py` (two fill boxes, counts), `Tools/editor/create_combat_test_map.py` (`cover` in `fill_nav_kinds`), `Tools/editor/create_movement_test_map.py` (`platform` in `fill_nav_kinds`), `Tools/editor/README.md` (one paragraph: fill kinds per map, the baseline rule)
- `Content/Maps/L_ItemTest.umap`, `L_CombatTest.umap`, `L_MovementTest.umap` (regenerated, LFS). **Not** `Content/Materials/M_Greybox_*` (re-saved bytes only; `git checkout` them).
- `docs/tasks/m5-map-characters/notes/03b-report.md` (the baseline), this file
- Not `Tools/editor/veloxmap.py` (task 05 may be editing it in parallel; `configure(fill_nav_kinds=…)` and `tag_actor` already exist).

## Relevant ADRs / references
- ADR 0006 §6 and **E0.2 (a), (d), (e)**, E0.4; ADR 0005 D17 (erosion), D41 (floor under thin surfaces), D46 (slot sealed flush = the remedy used here)
- `notes/03-report.md` (the cluster numbers referenced below), `Source/Velox/Bots/VeloxNavCoverage.cpp` l. 120–130 (tolerances), `VeloxNavCoverageMath.h` (`bWalkIn` flood fill)
- Bands: `infra/ci/bands/L_ItemTest.sh` (unchanged by this task)

## Acceptance criteria
- [ ] **Edge strips.** `FVeloxNavCoverageCluster` gains `bEdge`: true when the cluster is one cell wide along its whole length (`min(extent) == Grid`) **and** every member cell has at least one covered 4-neighbour. Cluster lines print `kind=edge` for them; the summary line gains `edge=<n>` and `clusters=<n>` keeps counting every cluster; `navlint.sh` prints `edge=<n>` and judges `clusters − edge > 0 || lowclear > 0` as FAIL. Unit rows: (i) a 1 × 8 strip along a covered row → `edge`; (ii) a 2 × 8 slot bordering covered cells → `walkin`, not edge; (iii) a 1 × 8 strip with no covered neighbour → `isolated`, not edge; (iv) the existing rows unchanged.
- [ ] **Item map seals.** Two boxes `MegaHealth_NorthFill` and `RedArmor_NorthFill`, kind `platform`, size 400 × 100 × 300, centred on the platform's X, Y 1450, Z 150, material `Platform`, so the slot between platform (Y 1400) and corridor wall (Y 1500) is flush with the platform top; `assert_counts` updated (`platform` 15 → 17); `Velox.Items.ItemTestMap` still sees 23 items; the pad links, drop links and `FtOffMesh` slot cases are untouched (the D17 room is not changed).
- [ ] **Fill kinds.** `create_combat_test_map.py`: `vm.configure(..., fill_nav_kinds=("cover",))`; `create_movement_test_map.py`: `fill_nav_kinds=("platform",)` (ramps are *not* filled — E0.2 (d)). `Tools/editor/README.md` names each map's fill kinds and why ramps are excluded.
- [ ] **Regeneration.** All three generators run headless, counts lines match the expected dictionaries (item: platform 17; combat and movement unchanged), every actor carries `Layout:<name>`. Only the three `.umap`s are committed; `git status --short Content/` shows nothing else after `git checkout -- Content/Materials`.
- [ ] **Lint baseline** in `notes/03b-report.md`: `navlint.sh --report-only` of each map with every cluster line and your classification (E/F/A as in `notes/03-report.md`). Expected: `L_ItemTest` clusters 5 and 6 gone, lowclear 0; `L_CombatTest` lowclear 0 (3 isolated cover tops remain); `L_MovementTest` lowclear 0, clusters 8/13/17/18 now `kind=edge`, the under-ramp floors and inter-lane slots still reported. Also one `--grid 25 --report-only` run per map, recorded (no judgement).
- [ ] **45° platform**: ≤ 30 min on the missing west 250 cm of `RampPlatform_45deg` (hypotheses: the 20 cm ramp slab's lip above the platform top at the junction; the 45° slope at 0.5° under `AgentMaxSlope`). Fix only if it is a one-line generator change; otherwise record the finding and stop.
- [ ] **Soaks unchanged** (idle host, `flock -o "$SOAK_LOCK"`): `infra/ci/fts.sh` → `OK 7/7`; `nettest.sh --scripts BotRun,BotDuel` → PASS with `bands=L_ItemTest`; `ffa.sh --repeat 3` → every judged invariant PASS (link success and item coverage are `n/j` on 3 runs — expected). Report the per-round kills/pickups next to the D51 figures; a drift beyond the band tails is reported, not re-banded.
- [ ] Build green (all automation tests incl. the new rows), `--reeval` of `docs/adr/evidence/0005-D51-single-shot` unaffected (no runner change in this task).

## Verification steps
1. `infra/ci/build.sh` → `Velox.Bots.NavCoverageClusters` lists the new rows and passes.
2. For each map: `"$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$VELOX_PROJECT" -run=pythonscript -script="$PWD/Tools/editor/create_<x>_map.py" -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput` → exit 0, counts line as expected.
3. `infra/ci/navlint.sh /Game/Maps/L_ItemTest --report-only` (and the other two; then with `--grid 25`) → paste into the report.
4. `git status --short Content/`; `git checkout -- Content/Materials`; `git lfs ls-files -s | /usr/bin/grep Maps/`.
5. `infra/ci/fts.sh`; `flock -o "$SOAK_LOCK" infra/ci/nettest.sh --scripts BotRun,BotDuel`; `flock -o "$SOAK_LOCK" infra/ci/ffa.sh --repeat 3`.
6. Kill your own leftover UE processes; `df -h /` ≥ 15 GB free.

## Report back
Standard format plus: the three lint summary lines before/after, the counts lines, the soak verdict lines with the D51 comparison, and the 45° finding.

## Notes / outcome

2026-10-31 (dev): Done. Build green, 107 tests, 0 failing (new `NavCoverageClusters` rows). Edge strips implemented (`bEdge`, `kind=edge`, `edge=<n>`, navlint gate on `clusters - edge`). Maps regenerated once and committed; baseline in `notes/03b-report.md`. Item map: lowclear 0, former clusters 5/6 gone; combat lowclear 0; movement lowclear 13 -> 2 (the 45 deg platform junction, not a one-line fix, finding in the report).
- **Open item (lead decides):** `fts.sh` is 6/7: `FtOffMesh` cases `ra_strip` / `mega_strip` (`Source/Velox/Tests/VeloxBotFunctionalTest.cpp` l. 1230-1231, outside this task's scope) start a bot in the 100 cm slots north of the RA / Mega platforms - exactly the slots E0.2 (e) orders sealed. After the seal the start (2100, 1450) lies inside `RedArmor_NorthFill`, the bot gets `no_clear_point` x6 and `giveup`. The task text assumed the slot cases were the D17 room ones. Options: drop the two cases (the D17-room slot A / B cases keep the D45 class covered) or point them at the YA slot. Not done here (out of scope).
- Soaks: `nettest.sh --scripts BotRun,BotDuel --repeat 3 --keep-going`: both set evaluations PASS (bands=L_ItemTest); BotDuel round 1 FAILed one per-round rule, max_dwell 6 s at Rapidus (2539,-1715) goal=item (SE quadrant, far from the changed slots). `ffa.sh --repeat 3`: all judged invariants PASS.

