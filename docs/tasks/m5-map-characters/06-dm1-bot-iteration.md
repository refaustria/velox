# 06 — DM1 bot iteration: report-only soak sets and layout fixes

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 04 (runner), 05 (blockout), ADR 0006 §7

## Goal
Bots play `L_DM1_Blockout` credibly: the map-independent invariants hold on three report-only sets (BotRun ×3, BotDuel ×3, FFA ×10), every item is taken, no stuck clusters; the numbers are recorded as the evidence base for the architect's band ruling (task 07). Layout defects found by the bots are fixed in the layout table, not in C++.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/dm1_layout.py`, `Content/Maps/DM1/L_DM1_Blockout.umap` (regenerated), `Content/Data/MoveScripts/DM1Run.json` (new), `infra/ci/nettest.sh` (register `DM1Run` in the default list on `L_DM1_Blockout`, judged like `PadRun`)
- `docs/adr/evidence/0006-dm1-blockout/` (logs and the summary tables), `docs/tasks/m5-map-characters/notes/06-report.md`
- No changes under `Source/Velox/Bots/` — a bot defect is reported to the lead with evidence (ADR 0005 process), not patched here.

## Relevant ADRs / references
- ADR 0006 §7 (invariants, item coverage, DM1Run), §3 (flow rules — a fix must keep them)
- ADR 0005 D36 (set method), D40/D46/D47 (idle host, contention classes), D49 (labels)
- `docs/adr/evidence/0005-D49-m4-final/` (what a complete evidence set looks like)

## Acceptance criteria
- [ ] Idle host (load < 2, no other UE process), soak lock held, one set at a time: `nettest.sh --map L_DM1_Blockout --scripts "BotRun" --repeat 3`, `… "BotDuel" --repeat 3`, `ffa.sh --map L_DM1_Blockout --repeat 10`.
- [ ] All invariants PASS on the final set: `left_arena 0`, `world_kill 0`, `no_nav_point 0`, `giveup 0`, Σ stuck ≤ 5, bursts 0, max dwell ≤ 5 s, nav build ≤ 2 s, link success ≥ 90 % per type, item coverage 100 %.
- [ ] Metrics recorded per set in `notes/06-report.md`: BotRun Σ kills and pickups per round, BotDuel Σ frags, camp episodes, Mega return lags (`respawn→take`), RA return lags (add the parse if the runner lacks it — in the runner, not in C++), FFA Σ kills, stuck positions with the `Layout:` tag of the nearest actor.
- [ ] Every layout change is a commit of its own with the before/after metric that motivated it; the §3 rules re-assert at generation and `navlint.sh` stays at zero clusters.
- [ ] `DM1Run.json` drives a scripted client along C (west → east), D lower (east → west) and over the three pads; PASS in `nettest.sh` (reaches each marker, Σ strays ≤ 2.5 cm at 100 ms / 2 %).
- [ ] Evidence directory ≤ 20 MB (`gzip` the logs).

## Verification steps
1. `uptime; pgrep -af "UnrealEditor|llama" | /usr/bin/grep -v llama` — only your processes.
2. The three set commands above under `flock -o "$SOAK_LOCK"`; keep the `Saved/CI/<stamp>` paths.
3. `infra/ci/navlint.sh /Game/Maps/DM1/L_DM1_Blockout` after each regeneration.
4. `nettest.sh --scripts "DM1Run"` → PASS.

## Report back
Standard format plus the metric tables of the final set, the list of layout changes with their motivating numbers, and any bot defect with evidence lines (for the lead to route to dev-hard/architect).

## Notes / outcome
