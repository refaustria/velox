# 06 — DM1 bot iteration: report-only soak sets and layout fixes

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** partially done (evidence and layout fixes in; final FFA set still FAILs Σ stuck / dwell / Drop link success on a contended host, see notes/06-report.md)
**Depends on:** 04 (runner), **04b** (per-instance coverage, link definition — the final sets must run with it), 05 (blockout), ADR 0006 §7 and **E0.3**

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
- [ ] All invariants PASS on the final set: `left_arena 0`, `world_kill 0`, `no_nav_point 0`, `giveup 0`, Σ stuck ≤ 5, bursts 0, max dwell ≤ 5 s, nav build ≤ 2 s, link success ≥ 90 % per type (E0.3 b definition: failed = missed/timeout/noimpulse/tooslow; ≥ 30 attempts per type; E3 expects the *observed* rate ≥ 95 % — record it per type with `aborted=` and any `FLAG`), item coverage 100 % **per instance** (E0.3 c; no exemption exists for DM1 — an untaken item is a layout defect first, E0.3 a). A set whose only FAIL is link success or dwell is re-run once (E0.3 b/d); record both results.
- [ ] Dwell positions ≥ 4 s recorded with their goal and class (`infra/ci/dwellwhy.py`: camp / timed-wait / other) plus the per-round `timed_waits` / `wait_max_s` and any E2 FLAG, so E3 can confirm the E2 measure on DM1. The final sets run on a runner that includes task 04c.
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

**Outcome (dev, 2026-11-01, branch `m5-map-characters`).** Step 0 (BotDuel x3 on L_ItemTest, fresh build): behaviour PASS 3/3, the 04c failures did not recur. DM1: 18 report sets (6 layout states x BotRun/BotDuel/FFA), 7 layout commits (PadD x2, tall drop ends, drop links removed where the wall or PadD defeats the landing, RL/Quad/stub items moved), `DM1Run` registered and PASS (7/7, 3 pads, strays 0), RA return lags added to the runner (report-only). Final layout: coverage 35/35, left_arena / world_kill / no_nav_point / giveup 0, bursts 0, nav build 1.1 s, JumpPad 100 %; **not green**: ffa6 Σ stuck 7 (4 `fight_stall`, 3 airborne `progress`), max dwell 10 s (fight stand-offs at the ledge/balcony corners), Drop link success 89.2 % (97.9 % in the identical-layout ffa5). No set ran on a quiet host (three orphaned UE clients of worktree agent-aef158e55e4aa9a6f, ~2 cores, for the whole task), so cost is unjudged. Bot findings (fight stand-offs, airborne `progress` strikes, maneuver started in the air judged missed, 2-D arrival under stacked items) are in `notes/06-report.md` for the lead; no `Source/Velox/Bots/` change. Evidence: `docs/adr/evidence/0006-dm1-blockout/`.
