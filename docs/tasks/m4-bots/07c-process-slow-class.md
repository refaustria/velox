# 07c — Process-slow window class in the cost classifier (ADR 0005 D47)

**Milestone:** M4 Bots
**Agent:** dev
**Status:** done
**Depends on:** 07b (classifier, `--reeval`), 08 (`VeloxBotCost` line with `frame_other_us`), ADR 0005 D46, D47

## Goal
The D46 1c classifier calls a window "cost" whenever the bots' wall clock rises and the host sidecar is quiet. M4-08
showed that on this VM the whole game thread steps 3–6× slower once per round, bots and engine alike, and that BotRun's
slow windows even pass as clean. After this task the runner uses `frame_other_us` (server frame minus bot ticks) as a
bot-independent yardstick: a window where the thread slowed is **process-slow** (`P`), excluded from the clean basis and
judged relative to the yardstick; a window where the bots slowed clearly more than the thread is still **cost** (FAIL).
The verdict of the M4 cost gate then comes from the D46 rerun on this runner. Rule text: ADR 0005 D47 (a)–(g).

## Scope — files/modules the agent may touch
- `infra/ci/nettest.sh` — `analyze_round` (reference, classes, round verdict, output, `stats.json` fields),
  `bot_set_eval` (slow-throughout rule, printed per-round reference figures), the `cost breakdown` line; `--reeval`
  needs no change beyond what the two functions print.
- `infra/ci/README.md` — the "Host evidence and classification" paragraph: add the `P` class, the reference, the
  thresholds, the new round/set rules and the fixture.
- `docs/tasks/m4-bots/07c-process-slow-class.md`, `docs/tasks/m4-bots/README.md`.
(No engine code. The counters this task reads are in `VeloxBotSubsystem.cpp` since M4-08; do not touch them.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` D47 (the rule), D46 items 1(c), 1(f), 5 (what stays), D14 (flag), §13 (budget).
- `docs/adr/evidence/0005-D47-pre09a5aec/README.md` — the fixture and `d47_ref.py`, a 60-line reference
  implementation of D47 (a)–(c) that produced the expected numbers below. Port its arithmetic; do not call it.
- `docs/tasks/m4-bots/08-bot-cost.md` — the breakdown and the `VeloxBotCost` field list.

## Acceptance criteria
- [ ] Per round: `frame_other_ref`, `mean_ref` from the reference set (windows 2..n with
      `frame_other_us <= 1.6 * min3`, `min3` = cheapest mean of three consecutive windows); per window `R_other`,
      `R_bots`, `rr`; `frame_other_us` derived as `frame_us - frame_bots_us` when the field is missing; rounds with
      < 5 windows or without `VeloxBotCost` lines keep the D46 classes (including `?`).
- [ ] Classes in D47 (b) order with constants `R_OTHER_SLOW = 2.0`, `RR_WINDOW_MAX = 2.5`, `RR_ROUND_MAX = 2.0`,
      `REF_BAND = 1.6`, `MIN_CLEAN_WINDOWS = 6` next to `BUDGET_MAX`; class char `P`; legend updated; with a reference
      present no window is `?`.
- [ ] Round cost judged iff clean >= 6 and clean + P >= 50 % of windows; clean bands unchanged; with >= 3 P windows
      `p_norm_mean_us <= 300` and `rr_round <= 2.0`; any cost window FAIL; P windows' `max_fire_us`/`max_path_us`/
      `stalls` printed, not judged (not in the D14 flag count).
- [ ] Set: a round with `frame_other_ref > 2.0 *` the set's smallest `frame_other_ref` is slow throughout: all windows
      P, cost not judged (not FAIL); rest per D46 5. `stats.json` carries `p_windows`, `frame_other_ref`, `mean_ref`,
      `rr_round`, `p_norm_mean_us`.
- [ ] Output: head line `clean=a/n P=b`; detail line per non-clean window with `R_other= R_bots= rr=`; `cost
      breakdown` prints clean | P | other and slowdown P/clean for bots and `frame_other`.
- [ ] `VELOX_OUT=<scratch> infra/ci/nettest.sh --reeval docs/adr/evidence/0005-D47-pre09a5aec --scripts BotRun,BotDuel`
      prints, ignoring the behaviour FAILs the extracts cause: cost windows 0 in all six rounds; BotDuel clean 10/8/8 of
      27, P 17/19/19, clean `mean_us` 71.4/76.9/85.7, `p_norm_mean_us` 89.7/92.3/103.8, `rr_round` 1.23/1.23/1.37;
      BotRun clean 9/8/8 of 21, P 12/13/13, clean `mean_us` 74.0/61.9/66.5, `p_norm_mean_us` 72.1/66.7/70.0,
      `rr_round` 1.07/1.07/1.06 (tolerance: ±1 window, ±2 µs, ±0.05 on ratios); `set ... cost PASS` for both.
- [ ] `--reeval` on the three D46 1f runs (`agent-a84aa42c7c159f905/Saved/CI/20261030-14*`, if still present) and on
      any nettest dir without `VeloxBotCost` lines prints exactly the D46 classes (no regression for old logs).
- [ ] `bash -n infra/ci/nettest.sh` clean; `infra/ci/build.sh` CI OK (nothing compiled changes; the run proves the
      runner still starts).

## Verification steps
1. `bash -n infra/ci/nettest.sh`.
2. `VELOX_OUT=/tmp/<scratch> infra/ci/nettest.sh --reeval docs/adr/evidence/0005-D47-pre09a5aec --scripts BotRun,BotDuel`
   → the numbers above; compare with `python3 -I docs/adr/evidence/0005-D47-pre09a5aec/d47_ref.py 1 <the six server logs>`.
3. `--reeval` on an older nettest directory without `VeloxBotCost` lines → unchanged classes and verdicts.
4. `infra/ci/build.sh` → `[ci] CI OK`.
5. Do not run the acceptance rounds in this task; the lead schedules the D47 rerun (idle host, `flock`, BotRun ×3 then
   BotDuel ×3) after the merge.

## Report back
Standard dev format: result, what changed (functions touched), verification (commands + output tail incl. the six
rounds' class strings and set lines), open issues. State explicitly if Unreal was not available for step 4.

## Notes / outcome
Implemented in `infra/ci/nettest.sh` (`analyze_round`: reference, P class, round verdict, output, stats.json; `bot_set_eval`: slow-throughout rule, per-round reference line) and `infra/ci/README.md`. `--reeval` on the D47 fixture reproduces the ruling exactly: cost windows 0; BotDuel clean 10/8/8 of 27, P 17/19/19, clean mean_us 71.4/76.9/85.7, p_norm 89.7/92.3/103.8, rr_round 1.23/1.23/1.37; BotRun clean 9/8/8 of 21, P 12/13/13, 74.0/61.9/66.5, 72.1/66.7/70.0, 1.07/1.07/1.06; set cost PASS both. Fixture with VeloxBotCost lines stripped: classes and verdicts identical to the pre-change runner (only the new P= and reference lines differ). Not run: infra/ci/build.sh and live rounds (no UE by instruction); the D46 1f logs no longer exist. Ambiguities: see report (slow-throughout round keeps its own FAIL on a per-round X window; set ignores it).
