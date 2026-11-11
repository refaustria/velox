#!/bin/bash
# gate.sh : the full part-A gate, each set under its own soak lock, sequential. Prints one line per finished set.
S=/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad
R=$S/m6-02/runset.sh
run() { flock -o "$S/soak.lock" "$R" "$@"; echo "SET $1 rc=$? $(date -u +%T)"; }
run G1-dedicated-default --port 8413 --repeat 1 --keep-going
run G2-listen-default --server-mode listen --port 8414 --repeat 1 --keep-going
run G3-listen-botrun3 --server-mode listen --port 8415 --scripts BotRun --repeat 3 --keep-going
run G4-listen-botduel3 --server-mode listen --port 8416 --scripts BotDuel --repeat 3 --keep-going
echo "GATE DONE"
