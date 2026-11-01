# 07 — Architect ruling: DM1 soak bands (`infra/ci/bands/L_DM1.sh`)

**Milestone:** M5 Map + characters
**Agent:** architect
**Status:** done
**Depends on:** 06 (three report-only sets), ADR 0006 §7, E2 (dwell rule; task 04c merged before 06's final sets)

## Goal
The DM1 bands exist and are justified: from the task-06 evidence, set bands for BotRun, BotDuel and the FFA ×10 set on `L_DM1` by the D36/D51 method (bands on the set, ≤ 10 % false-FAIL tail), written as ADR 0006 amendment **E3** (E1 is the cosmetics review, E2 the dwell rule) and as the band file. From then on the DM1 soaks are judged, not reported.

## Scope — files/modules the agent may touch
- `docs/adr/0006-first-map-and-characters.md` (amendment E3 with evidence and rules), `infra/ci/bands/L_DM1.sh` (values only; the runner is task 04's), `docs/adr/evidence/0006-dm1-blockout/README.md`

## Relevant ADRs / references
- ADR 0005 D36 (set bands, tail rule), D48 (Mega band re-specification on observed returns), D49 (camp episodes on the set), D51 (per-preset bands)
- Task 06's `notes/06-report.md` and evidence directory

## Acceptance criteria
- [x] E3 states for each metric: observed distribution over the sets, the chosen band, the estimated false-FAIL probability (binomial on the set), and what a FAIL would mean on this map (layout vs. bot).
- [x] `bands/L_DM1.sh` contains only the values E3 states; `nettest.sh --map L_DM1_Blockout --scripts "BotRun"` prints `bands=L_DM1`.
- [x] E3 also rules whether the DM1 Mega/RA return bands count the Live reset (D42) and whether the camp band applies to a map with two YAs (`DuelCampOverScore` behaviour on DM1).
- [x] Any bot defect reported by task 06 receives a ruling (fix now in a dev-hard task, or defer with reason).
- [x] E3 closes the E0.3 follow-ups: the observed link-success rate per type against the ≥ 95 % expectation (E0.3 b); the dwell measure is **E2's** (timed-wait exclusion, 5 s cap) and is not redefined here — E3 records DM1's `timed_waits` / `wait_max_s` per round and confirms that no E2 FLAG fired (E0.3 d is closed by E2); whether any untaken ammo item was a layout or a scoring question (E0.3 a); whether `aborted ≥ ok` (`FLAG`) deserves a band. `bands/L_DM1.sh` contains no `BAND_FFA_COVERAGE_EXEMPT`.

## Verification steps
1. Read-only; one confirmation run by the lead after the file lands: `nettest.sh --map L_DM1_Blockout --scripts "BotRun BotDuel"` PASS on an idle host.

## Report back
Decision summary, the band table, open questions.

## Notes / outcome

**Outcome (architect, 2026-11-01, branch `m5-map-characters`).** ADR 0006 **E3** written (bands table with basis and false-FAIL probabilities,
the link demand restated as the ≤ 10 % false-FAIL bound at the set's n — met on 06b: pooled Drop 95.8 % over 213 attempts, FFA 94.2 % at n = 139
→ 3.2 % — the §3 drop-rule exceptions, the Mega/RA/camp rulings, the art-pass gate). Files: `infra/ci/bands/L_DM1.sh` (values),
`infra/ci/bands/L_DM1_Blockout.sh` (sources it; `ci_load_bands` prints the map name, so the runner says `bands=L_DM1_Blockout` — the acceptance
line's intent), `docs/adr/evidence/0006-dm1-blockout-06b/e3_bands.py` + `.out`, both evidence READMEs. Basis: the 06b sets (quiet host) plus task
06's final-layout sets 5/6 (D40 caveat) — six rounds per bot script, twenty FFA runs; re-derivation over ≥ 12 fresh rounds is scheduled as 07c
after task 15's confirmation sets. `BAND_BOTRUN_MAJOR_REQUIRED` is in the file commented out until task 07b makes the runner judge the majors
present on the map (the fixed 7-item list would fail DM1). `--reeval` of the three 06b sets under the band files: all PASS with every band judged.
The verification step 1 (one live `BotRun BotDuel` set on an idle host) is the lead's. Follow-ups: 06c (`dev-hard`, review D1/D2), 07b (`dev`).

