# ADR 0005 D49 evidence - final M4 acceptance on 901015a (task 07g-2 runner)

Run 2026-10-31 08:24-09:15 UTC, idle VM, one pass, no fallback rerun. `botrun/`, `botduel/`, `default/` are nettest run
directories (server logs filtered to `LogVelox|Error|ensure|Warning`, client logs to `Velox|Error|ensure`; `.result`,
`-gate.txt`, `-host.log`, `.stats.json` unchanged); `nettest.sh --reeval` on `botrun/` and `botduel/` prints the same
summary lines as the originals. `ffa/` holds the ten D36 FFA server logs (filtered, `summary.py` on them gives the same
verdicts), their `.json`, and the runner (`ffa.sh`, `summary.py`, `set.py`). `all.out` is the console output of the six steps.
