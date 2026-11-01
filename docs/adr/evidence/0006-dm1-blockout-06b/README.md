# Task m5-06b evidence: DM1 bot defects fixed (L_DM1_Blockout acceptance, L_ItemTest regression)

Head 8e95bfd (branch `m5-06b-dm1-bot-defects`, on `m5-map-characters` f9629a0), layout unchanged from task 06 (ec4700f).
All sets ran on 2026-11-01 between 04:48 and 05:52 local time on a quiet host: no foreign UE process at the start or end of any set, load 0.07 to 1.63
(each `<set>/host.txt`). Every UE step ran under the soak lock. Recipe D48 (4)(ii): server logs filtered to `LogVelox|Error|ensure|Warning`, client
logs to `Velox|Error|ensure`. The `.result`, `-gate.txt`, `-host.log`, `.stats.json` and FFA `.log.json` files are copied unchanged. `run.out.gz` is the console output.
`ffa.sh --reeval` on the gunzipped `dm1-ffa/logs` prints the same SET line and invariants. Logs are gzipped: gunzip them before `--reeval`.

| Dir | Command | Result |
|---|---|---|
| `dm1-ffa/` | `ffa.sh --map L_DM1_Blockout --repeat 10` | SET PASS, every invariant PASS: Σ stuck 3 (3 `fight_stall`, 0 `progress`), bursts 0, max dwell 5, Drop 94.2 % (131/139, aborted 15), JumpPad 100 % (92/92), coverage 35/35, nav 0.91 s, errors 0 |
| `dm1-botrun/` | `nettest.sh --map L_DM1_Blockout --scripts BotRun --repeat 3 --keep-going` | 3/3, set PASS: stuck [0,0,0], max dwell [3,3,3], Drop 43/43, JumpPad 19/19 (n<30) |
| `dm1-botduel/` | same with `BotDuel` | 3/3, set PASS: stuck [0,0,0], max dwell [2,2,3], Drop 30/31 (96.8 %), JumpPad 14/14, RJ 1/1 |
| `it-botrun/` | `nettest.sh --scripts BotRun --repeat 3 --keep-going` (L_ItemTest) | 2/3: round 2 max dwell 9 s (Item goal under fire on the ramp, 16 cm from a segment target; D27-bounded progress strike at ~6 s). Set bands PASS |
| `it-botrun2/` | re-run once (E0.3 b) | 3/3, set PASS: kills 39, stuck 0, max dwell [3,3,2], majors 7/7 |
| `it-botduel/` | `nettest.sh --scripts BotDuel --repeat 3 --keep-going` | 3/3, set PASS: frags 29, camp 18, Mega within_3s 4 / within_10s 6, stuck 0, max dwell 2 |
| `it-ffa/` | `ffa.sh --repeat 3` | SET PASS: kills 38, stuck 0, max dwell 3 |

`dm1-ffa/links-by-record.txt` lists link outcomes per record. `finding5-dock350-probe-links.txt` is the 3-run FFA probe with the dock N drops moved to
x = ±350: 8 of 10 dock N drops were missed (the bots drift inward through the door jamb). The move was reverted and is not in the commit.
