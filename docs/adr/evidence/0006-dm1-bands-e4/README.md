# ADR 0006 E4 evidence (task 07c): DM1 bands re-derived over the final-layout rounds

Read-only derivation on 2026-11-01 (architect, no UE run). Basis: task 06b on `L_DM1_Blockout` (`../0006-dm1-blockout-06b/`) plus task 15 on
`L_DM1` (`../0006-dm1-art/`, same layout by `--diff-collision`) — 6 BotRun, 6 BotDuel, 20 FFA rounds with the current bots on a quiet host
(basis A) — plus task 06 set 6 (`../0006-dm1-blockout/botrun6`, `botduel6`, `ffa6`; pre-06b bots, contended host, D40 caveat) as E3's second
basis: 9 / 9 / 30 (basis B, the ruling basis). Task 06 set 5 is a check only (not the final layout). D51's "≥ 12 fresh rounds per bot script"
is **not** met for BotRun/BotDuel; ADR 0006 E4 (a) says why the derivation is made now and when the 12-round basis arrives.

| File | Content |
|---|---|
| `e4_bands.py` | The derivation (D51 (1) method: largest k with P(Σ < k) ≤ 2.5 % under Poisson and bootstrap, the smaller). Reads the committed `.stats.json` / `.log.json` (06b rounds, both FFA sets), parses `../0006-dm1-art/<set>/run.out.gz` for the task-15 BotRun/BotDuel rounds (those directories carry only the last round's stats.json; the BotRun `timed` field is not parsed from the console — BotRun Mega timing is not judged), types in task 06 set 5/6 (cross-checked against the set-6 console and `../0006-dm1-blockout-06b/e3_bands.py`). Also: the E3 (c) link demand at every observed n and the dock N per-record analysis (Fisher exact, Wilson interval, threshold false-trigger rates). Run from this directory: `python3 -I e4_bands.py ..` |
| `e4_bands.out` | Its output |
| `reeval-botrun.out`, `reeval-botduel.out`, `reeval-ffa.out` | `nettest.sh --reeval <06b set> --map L_DM1_Blockout --scripts BotRun|BotDuel` and `ffa.sh --reeval <06b dm1-ffa/logs> --map L_DM1_Blockout` on gunzipped scratch copies of the 06b sets under the E4 `bands/L_DM1.sh`: `bands=L_DM1_Blockout (file L_DM1)`, BotRun `set PASS` (Σ kills 19 ≥ 11), BotDuel `set PASS` (Σ frags 14 ≥ 7, camp 30 ≥ 22, Mega within_3s 3 / within_10s 6), FFA `SET runs=10 PASS`, `sum_kills >= 36` (47), every invariant PASS |

## Result (every band, E3 → E4)

| `BAND_*` | E3 | E4 |
|---|---|---|
| `ROUND_KILLS_MIN` | 1 | 1 (confirmed) |
| `BOTRUN_PICKUPS_MIN` | 30 | 30 (confirmed) |
| `BOTRUN_MEGA_REQUIRED` | 1 | 1 (confirmed) |
| `BOTRUN_LINK_OK_MIN` | 5 | 5 (confirmed) |
| `BOTRUN_KILLS_MIN` | 11 | 11 (confirmed; A and B agree) |
| `BOTRUN_MAJOR_REQUIRED` | 1 | 1 (confirmed) |
| `BOTDUEL_FRAGS_MIN` | 7 | 7 (confirmed on B; A alone gives 6) |
| `BOTDUEL_CAMP_MIN` | 21 | **22** (A and B agree) |
| `MEGA_WITHIN3_MIN` / `MEGA_WITHIN10_MIN` | 1 / 2 | 1 / 2 (confirmed; the ≤ 3 s clause's false FAIL falls from 3.9 % to 1.6 %) |
| `FFA_KILLS_MIN` | 34 | **36** (A alone gives 37) |
| `FFA_RUN_PICKUPS_MIN` | 40 | 40 (confirmed) |
| `FFA_RUN_MEGA_MIN` | 2 | 2 (confirmed) |

Dock N (`Drop_DockW_N`, `Drop_DockE_N`): 06b 61/65 = 93.8 %, task 15 58/67 = 86.6 % (Fisher p = 0.24), pooled 119/132 = 90.2 % — watch item
with the thresholds of E4 (d); no layout task.

## Gap

The task-15 evidence (`../0006-dm1-art/`) has no server/client logs and no `ffa-N.log` (the worktree that produced them is gone), so it cannot be
`--reeval`ed; its sets are judged against the E4 file by arithmetic on the committed per-round figures (BotRun 19 ≥ 11, BotDuel 12 ≥ 7, camp
36 ≥ 22, Mega 4 / 6, FFA 54 ≥ 36). E4 (e) makes the D48 (4)(ii) recipe a condition for any future basis set.

## Update task m5-07e (2026-11-01, dev): live confirmation appended

`e4_bands.py` now also reads `../0006-dm1-art-e4/{botrun,botduel,ffa}` (D48 (4)(ii) recipe, replayable): basis A is 9 BotRun / 9 BotDuel / 30 FFA rounds,
basis B 12 / 12 / 40. `e4_bands.out` is the new output; `e4_bands.with-attempt1.out` (second argument `with-attempt1`) adds the two first attempts that
failed a dwell check and were rerun (D49). `reeval-07e-{botrun,botduel,ffa}.out` are `--reeval` of the new sets on gunzipped copies under the E4 file
(`bands=L_DM1`): BotRun set PASS (kills 19), BotDuel set PASS (frags 13, camp 32, Mega 5 / 5), FFA `SET runs=10 PASS` (kills 49, every invariant PASS), the
same verdicts as the live runs.
Bands, basis B: BOTRUN_KILLS 11, BOTDUEL_FRAGS 7, BOTDUEL_CAMP 22, FFA_KILLS 36 (basis A: 11 / 6 / 22 / 37): no k changed, no lower band rises; the band
file is unchanged. With the two failed first attempts added (sensitivity only) FFA_KILLS would read 37 on B (E4 already shows 37 on A). Dock N: 07e
46/50 = 92.0 %; pooled over task 15 + 07e 104/117 = 88.9 % (n < 120, the E4 (d)(ii) floor is not reached by the counted sets; with the failed first
attempts 151/168 = 89.9 %); no trigger either way.
