# ADR 0005 D48 evidence — final M4 acceptance logs on 046dabb (a07d47)

Three nettest runs of 2026-10-31 07:51-08:17 UTC on head 046dabb (idle VM), copied from the lead's scratchpad
`a07d47/` by task 07d. `nettest.sh --reeval <dir>` on each copy prints the same summary lines as on the original
directory (diff ignoring the `Logs:` and path lines: empty) with the 046dabb runner; with the D48 runner it prints the numbers below.

| Dir | Original | Content |
|---|---|---|
| `botrun/` | `20261031-075112-2982110` | `--scripts BotRun --repeat 3` |
| `botduel/` | `20261031-075729-2992329` | `--scripts BotDuel --repeat 3` |
| `default/` | `20261031-080448-3004370` | default list, 13 scripts, one round (`nettest/` only) |

Server logs are filtered to lines matching `LogVelox|Error|ensure|Warning`, client logs to `Velox|Error|ensure`
(all scripts, including the non-bot ones); `.result`, `-gate.txt`, `-host.log`, `.stats.json`, `.railhits` are copied unchanged.
No filtered log changed a printed number, so no full log was kept. `all.out` is the lead's console output of the three runs.

Expected under D48 (runner with the transition window and the Mega set band):

- botrun: set behaviour PASS, cost PASS, cost windows [0, 0, 0], clean 5/10/10 (of 20 windows), P 15/10/10, `rr_round` 1.37/1.31/1.18.
- botduel: round 3 classes `[.........PPPPPPPPPPPPPPPPP]` (window 10 `transition=1`, R_other 1.88, rr 1.09), clean 9/26, P 17, cost 0,
  `p_norm_mean_us` 126.6, `rr_round` 1.43; rounds 1-2 P 17/17, `rr_round` 1.25/1.36; set cost windows [0, 0, 0],
  `within_6s=4 within_3s=2 within_4s=2 first_cycle=[8.2,2.3,26.0] later_cycle=[4.5,5.2,0.0]`, set behaviour PASS, cost PASS, set PASS.
- default: all 13 scripts PASS.

The D47 fixture (`../0005-D47-pre09a5aec/`) prints the D47 (f) numbers unchanged; the ten FFA logs of a08 show no `P`, `X` or transition window.
