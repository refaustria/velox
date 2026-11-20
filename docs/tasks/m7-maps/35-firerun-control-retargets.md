# 35 — FireRunControl retarget regression check

**Milestone:** M7 Maps (ADR 0008) — regression guard
**Agent:** dev (escalate to dev-hard if it is a real regression)
**Status:** done (flake, no code change)
**Depends on:** H9 (merged)
**Wave:** follow-up (lead-added 2026-11-14 from H9 notes)

## Goal
In H9's default nettest list run, `FireRunControl` failed with 3 retargets where 4 are expected (M2 soak; ADR 0003/q20 weapon-switch retarget). Decide whether this is a regression from the M7 bot/combat changes (prime suspect: H10 unique bot PlayerIds — kill-feed ids, FindPlayerStateById and hitscan spread seed changed) or a load flake. Run FireRunControl ×5 on origin/m7-maps; if it fails reproducibly, A/B against origin/main (M6) and against the commit before H10's merge, find the cause and fix it (or report for dev-hard).

## Scope
- read anything; fix only within `Source/Velox/Weapons/*`, `Source/Velox/Combat/*`, `Source/Velox/Bots/*`, nettest FireRunControl judging, this file

## Acceptance criteria
- [x] FireRunControl ×5 result recorded with load; verdict regression vs flake with evidence
- [x] (n/a, not a regression) If regression: fixed, FireRunControl 5/5 PASS, FireRun + default nettest list green on behaviour; build + automation + fts.sh green

## Notes / outcome
**Verdict (2026-11-20, dev): flake, not a regression. No code or script change.**
Head: `0d24a24` (m7-maps incl. H10 unique bot PlayerIds, Fornax 38, Culmen 19/19b, Sacrarium 31); editor built in this worktree, `nettest.sh --scripts FireRunControl --repeat 5 --keep-going --port 7941`:
- Quiet host (load1 1.3-1.6, gate wait 0 s): **5/5 PASS**, every round `retarget client=4 server=4 modes=pending:4 switch_corrections=0 raise_switches=2`.
- Stress (20 busy loops, load1 about 18, `VELOX_LOAD_GATE=100`, port 7942; moves/round fell from about 4848 to about 3600, gaps 1200-1400): **5/5 PASS**, again `retarget client=4 server=4` in all rounds.
- 10/10, 0 rounds with 3 retargets, so no A/B against `main` or the pre-H10 commit was needed. The H9 failure (28-h9 note) was in the default 18-script list on a host where the load gate gave up at 1.82 / 5.71. H10 changes ids/seed only; the retarget count is the client's scripted key presses (FireRun.json) vs the weapon-switch state at press time, which does not read the PlayerId. Mechanism not proven (one failure, not reproduced); the rule `client retargets < 4` stays as is because it is a correct guard.
Raw logs: `/mnt/oldroot/velox-scratch/35-firerun-retargets/{head,loaded}/` and `head-run.log`, `loaded-run.log`. If it reappears in a default-list run, rerun FireRunControl alone before suspecting code.
