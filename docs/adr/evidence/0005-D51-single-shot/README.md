# ADR 0005 D51 evidence - single-shot weapons keep the gate (task `docs/tasks/m4-followups/03b-single-shot-gate-and-d51-bands.md`, step 3)

Run 2026-10-31 on branch `post-m4` at 388ff70 (step 1 b872fe8 + step 2 a78adc1), idle VM under the soak lock, ports
7981-7985. `infra/ci/build.sh` before the runs: 105 tests, 0 not successful, `[ci] CI OK`. Layout (D48 (4)(ii) recipe:
server logs filtered to `LogVelox|Error|ensure|Warning`, client logs to `Velox|Error|ensure`; `.result`, `-gate.txt`,
`-host.log`, `.stats.json` unchanged; `console.out` = the runner's console):

| Path | What |
|---|---|
| `botrun-medium/` | `nettest.sh --scripts BotRun --repeat 3 --keep-going` (Medium) |
| `botrun-easy/` | `nettest.sh --scripts BotRun --repeat 3 --keep-going --bot-difficulty Easy` (calibration: no set band judged) |
| `botduel/` | `nettest.sh --scripts BotDuel --repeat 3 --keep-going` (Hard) |
| `ffa/` | `infra/ci/ffa.sh 7984 120 10 <out>`: ten filtered server logs, `ffa_summary.py` json, `ffa.out` |
| `firerun.out` | `nettest.sh --scripts FireRun,FireRunControl --repeat 1` |

`VELOX_OUT=<scratch> infra/ci/nettest.sh --reeval <set>/nettest` prints the same round and set lines as `console.out`
(checked on all three sets, diff empty). `ffa_summary.py` + `ffa_set.py` on the filtered FFA logs reproduce `ffa.out` and
the ten `.json` byte for byte.

## Set sums against D51 (7)

| Set | Result | Sums |
|---|---|---|
| BotRun x3 Medium | set behaviour PASS, cost PASS, `NET SOAK OK` | kills [9, 11, 14] **Σ 34 ≥ 20**; stuck [0, 0, 1] Σ 1 ≤ 10; majors 7/7; pocket 0; `max_dwell_s` ≤ 4; cost windows [0, 0, 0], clean mean_us 79.4 |
| BotRun x3 Easy (calibration) | 3/3 rounds, `NET SOAK OK` (no band judged) | kills [9, 5, 6] Σ 20; stuck Σ 3; majors 7/7; cost windows [0, 0, 0], clean mean_us 60.3 |
| BotDuel x3 | set behaviour PASS, cost PASS, `NET SOAK OK` | frags [10, 12, 8] **Σ 30 ≥ 20**; camp episodes [7, 4, 9] **Σ 20 ≥ 7**; Mega within_3s **5 ≥ 1**, within_10s **7 ≥ 2** (returns 5.2, 0.0, 0.0, 0.2, 0.0, 9.0, 0.4); stuck Σ 4; cost windows [0, 0, 0], clean mean_us 89.2 |
| FFA x10 | `SET runs=10 PASS` | **sum_kills 120 ≥ 74** (min 9); sum_stuck 3 ≤ 30, max run 2 ≤ 7; bursts 0; pocket 0; dwell ≥ 6 s runs 0; max link dt 1.83; Mega 4 per run; refused/giveup/errors 0 |
| FireRun / FireRunControl | PASS / PASS, `NET SOAK OK` | FireRun C=0, rails=11 hits=11, rail_confirms 10/10 |

Every round: `left_arena=none`, `world_kills=0`, no `INVALID`; no slow-throughout round, no unjudged-round FLAG.

## D51 (11) near-human lines (pooled over the set, report-only)

| Quantity | Easy (BotRun) | Medium (BotRun) | Hard (BotDuel) |
|---|---|---|---|
| `acc_mg_pct` | 20.9 [10-24 IN] (73/350) | 15.6 [10-24 IN] (175/1120) | 58.7 [47-61 IN] (350/596) |
| `acc_rg_pct` | 71.4 report (10/14) | **41.1 [30-60 IN] (23/56); was 7.6** | 37.0 [35-60 IN] (27/73) |
| `acc_rl_pct` (self-only excluded) | 26.1 [15-45 IN], rl_self_only=1 | 37.1 [15-45 IN], rl_self_only=1 | 23.2 [15-45 IN], rl_self_only=0 |
| `held_share_pct` | 25.0 [25-60 IN] | 29.6 [20-50 IN] | 0.0 [<=10 IN] |
| `trigger_share_pct` | 25.0 report | 32.1 [>=30 IN] | 28.3 report |
| pooled `acc_hitscan_pct` | 30.0 report | 19.8 report | 58.1 [51-61 IN] |
| camp trio | - | - | episodes/round **6.67 [3-6 OUT]**, share 9.8 [4-12 IN], time 4.0 [2-8 IN], max lead 15.9 [<=23 IN] |

The one `OUT` is report-only and on the generous side (more camping than the band): episodes [7, 4, 9]. Expert was not
run (not in step 3). Per-round lines (in `console.out`) scatter more, e.g. Medium RG 31.8 / 14.3 / 55.6 on 22 / 7 / 27
rails; the bands are defined on the pooled set.
