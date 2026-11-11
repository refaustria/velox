#!/bin/bash
# gate.sh (M6-02 gate, 2026-11-06): each set under its own soak lock, sequential. Ports 7777-7789.
S=/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad
R=$S/gate02/runset.sh
run() { flock -o "$S/soak.lock" "$R" "$@"; echo "SET $1 rc=$? $(date -u +%T)"; }
run G1-dedicated-default --port 7777 --repeat 1 --keep-going
run G2-listen-default --server-mode listen --port 7778 --repeat 1 --keep-going
run G3-listen-botrun3 --server-mode listen --port 7779 --scripts BotRun --repeat 3 --keep-going
run G4-listen-botduel3 --server-mode listen --port 7780 --scripts BotDuel --repeat 3 --keep-going
run G5-dedicated-botrun3 --port 7781 --scripts BotRun --repeat 3 --keep-going
run G6-dedicated-botduel3 --port 7782 --scripts BotDuel --repeat 3 --keep-going
echo "GATE DONE"
