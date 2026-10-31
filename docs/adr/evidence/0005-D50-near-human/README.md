# ADR 0005 D50 evidence - near-human bots (task `docs/tasks/m4-followups/03-near-human-bots.md`)

Run 2026-10-31 on branch `post-m4` + the task commits (ef06b13 step 1, 57f4b59 step 2, 47fea67 tuning), idle VM under the
soak lock, ports 7931-7933. Layout (D48 (4)(ii) recipe: server logs filtered to `LogVelox|Error|ensure|Warning`, client
logs to `Velox|Error|ensure`; `.result`, `-gate.txt`, `-host.log`, `.stats.json` unchanged; `console.out` = the runner's
console; `nettest.sh --reeval <set dir>` prints the same `.result` lines as the originals, checked on every acceptance set
and on the baseline sets):

| Path | What |
|---|---|
| `calibration/base/` | Baseline, unchanged bots (ef06b13): BotRun x3 Medium, BotDuel x3, BotRun x3 `--bot-difficulty Easy`, x3 `Expert` |
| `calibration/tune1-d50-values/` | The four sets with the D50 (2) values (57f4b59, `DuelCampOverScore` 10) |
| `calibration/tune2-iteration1/` | Tuning iteration 1 (Easy 0.5/1.3/0.25, Medium 2.0/1.2/0.15, Hard release 1.0, camp 11) = the final knobs |
| `calibration/tune3-iteration2/` | Tuning iteration 2 (Easy 0.25/1.3/0.25, Medium 2.0/1.1/0.10; BotRun Medium and Easy only), reverted |
| `botrun/set-1..4/`, `botduel/set-1..4/` | Acceptance: BotRun x12 Medium, BotDuel x12 (four `--repeat 3` sets each) on 47fea67 |
| `ffa/` | D36 ten-run FFA (120 s, 3 Medium, seeds 1-10): filtered server logs, `summary.py`/`set.py` json, `ffa.out`, `ffa.sh` |
| `firerun.out` | `nettest.sh --scripts FireRun,FireRunControl --repeat 1` on 47fea67 (PASS: the extended `VeloxShot` line) |
| `bands.py`, `bands.out` | D50 (6)(iv) re-derivation (bootstrap + Poisson k, Mega p, upper bands, report-only ranges) |
| `table.py`, `table.out` | Per-round table of the acceptance rounds and the pooled near-human quantities |

The calibration directories carry no client logs (the near-human quantities and every set band come from the server log;
`--reeval` on them reproduces the server-side lines, the BotRun client-correction line prints 0).

`python3 -I bands.py` and `python3 -I table.py` (no arguments) re-run the arithmetic on this directory.
