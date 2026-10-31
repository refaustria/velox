# ADR 0005 D47 fixture — pre-04g rounds with the M4-08 counters

Six dedicated-server rounds on head **09a5aec** (before 04g/04h/04i-B) with the M4-08 `VeloxBotCost` counters patched
in, run by task M4-08 on the idle VM on 2026-10-30 22:41–23:01 UTC as the regression comparison (`08-bot-cost.md`,
"Regression check"). Each `round-<k>/<script>-server.log` holds only the `VeloxBotBudget`, `VeloxBotCost` and
`VeloxMatch phase=` lines of the original server log (the full logs were in the deleted worktree
`agent-a0f7dec3e1eedc5ad`); the client logs are empty and there is no host sidecar. Behaviour bands therefore fail on
this fixture by construction; it exists for the cost classifier only (`nettest.sh --reeval`).

| Round | Script | Source log | Windows | Step at window |
|---|---|---|---|---|
| 1 | BotDuel | `Velox-backup-2026.10.30-22.43.42.log` | 27 | 11 |
| 2 | BotDuel | `Velox-backup-2026.10.30-22.46.20.log` | 27 | 9 |
| 3 | BotDuel | `Velox-backup-2026.10.30-22.48.57.log` | 27 | 9 (half) / 10 |
| 1 | BotRun | `Velox-backup-2026.10.30-22.57.16.log` | 21 | 9 (half) / 10 |
| 2 | BotRun | `Velox-backup-2026.10.30-22.59.23.log` | 21 | 9 (half) / 10 |
| 3 | BotRun | `Velox.log` | 21 | 9 (half) / 10 |

This counter format predates `frame_other_us`; the runner derives it as `frame_us - frame_bots_us` (D47 (a)).

`d47_ref.py` is the reference implementation of D47 (a)–(c) that produced the expected numbers quoted in D47 (f) and in
task 07c: `python3 -I d47_ref.py 1 round-1/BotDuel-server.log ...` (the first argument is the number of leading windows
excluded from the reference set; D47 uses 1).

Expected under D47: cost windows 0 everywhere; BotDuel clean 10/8/8, P 17/19/19, clean mean 71.4/76.9/85.7 µs,
`p_norm_mean_us` 89.7/92.3/103.8, `rr_round` 1.23/1.23/1.37; BotRun clean 9/8/8, P 12/13/13, clean mean
74.0/61.9/66.5 µs, `p_norm_mean_us` 72.1/66.7/70.0, `rr_round` 1.07/1.07/1.06. Under D46 (runner at df996cc) the same
files print BotRun "clean" 16/17/17 with a contaminated mean of 143–147 µs and BotDuel cost not judged with 17–19 `?`
windows per round.
