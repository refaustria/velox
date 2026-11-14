# 35 — FireRunControl retarget regression check

**Milestone:** M7 Maps (ADR 0008) — regression guard
**Agent:** dev (escalate to dev-hard if it is a real regression)
**Status:** open
**Depends on:** H9 (merged)
**Wave:** follow-up (lead-added 2026-11-14 from H9 notes)

## Goal
In H9's default nettest list run, `FireRunControl` failed with 3 retargets where 4 are expected (M2 soak; ADR 0003/q20 weapon-switch retarget). Decide whether this is a regression from the M7 bot/combat changes (prime suspect: H10 unique bot PlayerIds — kill-feed ids, FindPlayerStateById and hitscan spread seed changed) or a load flake. Run FireRunControl ×5 on origin/m7-maps; if it fails reproducibly, A/B against origin/main (M6) and against the commit before H10's merge, find the cause and fix it (or report for dev-hard).

## Scope
- read anything; fix only within `Source/Velox/Weapons/*`, `Source/Velox/Combat/*`, `Source/Velox/Bots/*`, nettest FireRunControl judging, this file

## Acceptance criteria
- [ ] FireRunControl ×5 result recorded with load; verdict regression vs flake with evidence
- [ ] If regression: fixed, FireRunControl 5/5 PASS, FireRun + default nettest list green on behaviour; build + automation + fts.sh green

## Notes / outcome
(Filled by the agent.)
