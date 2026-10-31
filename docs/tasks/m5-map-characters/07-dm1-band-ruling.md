# 07 — Architect ruling: DM1 soak bands (`infra/ci/bands/L_DM1.sh`)

**Milestone:** M5 Map + characters
**Agent:** architect
**Status:** todo
**Depends on:** 06 (three report-only sets), ADR 0006 §7, E2 (dwell rule; task 04c merged before 06's final sets)

## Goal
The DM1 bands exist and are justified: from the task-06 evidence, set bands for BotRun, BotDuel and the FFA ×10 set on `L_DM1` by the D36/D51 method (bands on the set, ≤ 10 % false-FAIL tail), written as ADR 0006 amendment **E3** (E1 is the cosmetics review, E2 the dwell rule) and as the band file. From then on the DM1 soaks are judged, not reported.

## Scope — files/modules the agent may touch
- `docs/adr/0006-first-map-and-characters.md` (amendment E3 with evidence and rules), `infra/ci/bands/L_DM1.sh` (values only; the runner is task 04's), `docs/adr/evidence/0006-dm1-blockout/README.md`

## Relevant ADRs / references
- ADR 0005 D36 (set bands, tail rule), D48 (Mega band re-specification on observed returns), D49 (camp episodes on the set), D51 (per-preset bands)
- Task 06's `notes/06-report.md` and evidence directory

## Acceptance criteria
- [ ] E3 states for each metric: observed distribution over the sets, the chosen band, the estimated false-FAIL probability (binomial on the set), and what a FAIL would mean on this map (layout vs. bot).
- [ ] `bands/L_DM1.sh` contains only the values E3 states; `nettest.sh --map L_DM1_Blockout --scripts "BotRun"` prints `bands=L_DM1`.
- [ ] E3 also rules whether the DM1 Mega/RA return bands count the Live reset (D42) and whether the camp band applies to a map with two YAs (`DuelCampOverScore` behaviour on DM1).
- [ ] Any bot defect reported by task 06 receives a ruling (fix now in a dev-hard task, or defer with reason).
- [ ] E3 closes the E0.3 follow-ups: the observed link-success rate per type against the ≥ 95 % expectation (E0.3 b); the dwell measure is **E2's** (timed-wait exclusion, 5 s cap) and is not redefined here — E3 records DM1's `timed_waits` / `wait_max_s` per round and confirms that no E2 FLAG fired (E0.3 d is closed by E2); whether any untaken ammo item was a layout or a scoring question (E0.3 a); whether `aborted ≥ ok` (`FLAG`) deserves a band. `bands/L_DM1.sh` contains no `BAND_FFA_COVERAGE_EXEMPT`.

## Verification steps
1. Read-only; one confirmation run by the lead after the file lands: `nettest.sh --map L_DM1_Blockout --scripts "BotRun BotDuel"` PASS on an idle host.

## Report back
Decision summary, the band table, open questions.

## Notes / outcome
