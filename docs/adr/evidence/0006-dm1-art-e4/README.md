# Task m5-07e evidence: live confirmation on `L_DM1` (ADR 0006 E4 (a)/(e))

Branch base `m5-map-characters` (0da6300, final M5 code incl. 13b and 07d), built with `infra/ci/build.sh` in the agent worktree (rc 0). 2026-11-01 09:25 to 10:31
local, one agent on the host, every UE step under the soak lock, ports 8331-8334. `<set>/host.txt` has loadavg, disk and the foreign-UE-process list at the
start and end of each set: no foreign UE process anywhere, load 0.29 to 1.43 (the VM idles at about 1.1 with an unrelated llama-server). Recipe D48 (4)(ii) as
in `../0006-dm1-blockout-06b/README.md`: server logs filtered to `LogVelox|Error|ensure|Warning`, client logs to `Velox|Error|ensure` (the runner's own filter),
gzipped; `.result`, `-gate.txt`, `-host.log`, `.stats.json`, FFA `.log.json` unchanged; `run.out.gz` is the console; `links-by-record.txt` from the server / FFA logs.
Gunzip `logs/` before `--reeval`.

## Counted sets (the basis added to `../0006-dm1-bands-e4/e4_bands.py`)

| Dir | Command | Result |
|---|---|---|
| `botrun/` | `nettest.sh --map L_DM1 --scripts BotRun --repeat 3 --keep-going` | `bands=L_DM1`, 3/3, set PASS: kills [6,5,8] = 19 (>= 11), stuck 1, max dwell [3,3,4], Drop 35/37, JumpPad 22/22, majors 4/4 |
| `botduel/` | same with `BotDuel` | 3/3, set PASS: frags [4,5,4] = 13 (>= 7), camp 32 (>= 22), Mega within_3s 5 / within_10s 5, stuck 0, max dwell [3,2,3], Drop 31/33, JumpPad 18/18, majors 3/3 |
| `ffa/` | `ffa.sh --map L_DM1 --repeat 10` | SET PASS: sum_kills 49 (>= 36), stuck 4 (<= 5), max dwell 5, Drop 128/129 (99.2 %), JumpPad 85/85, coverage 35/35, nav 0.92 s, errors 0 |

## Not counted (D49 one rerun; kept for the record)

| Dir | What |
|---|---|
| `attempt1-botrun-FAIL/` | first BotRun x3 with host.txt: 2/3, round 1 FAIL `max_dwell_s 7 > 5` (Celer @ (2199,838), goal=item; behaviour counts kills 6, links 15/18); the other rounds PASS; set line PASS (kills 22); the rerun above is the counted set |
| `attempt1-ffa-FAIL/` | first FFA x10: SET FAIL, run 7 `max_dwell_s 6 > 5` (Rapidus @ (-2136,216), goal=item); everything else PASS (kills 54, Drop 133/137, JumpPad 96/99); the rerun above is the counted set |
| `attempt0-botrun-nohost/` | a BotRun x3 run before `host.txt` was bracketed (helper script not executable): 3/3, set PASS (kills 25); console and per-round `.result` / `.stats.json` only, no logs; replaced by a fully recorded set. Not in any basis |

`../0006-dm1-bands-e4/e4_bands.py ..` reads `botrun/`, `botduel/`, `ffa/`; `... with-attempt1` additionally appends the two `attempt1-*` sets (sensitivity).
