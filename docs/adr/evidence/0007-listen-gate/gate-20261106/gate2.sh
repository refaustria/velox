#!/bin/bash
# gate2.sh: one confirmation rerun of the listen bot sets after root-causing G3 (D33 air=1) and G4 (JumpPad 35/39) as
# mechanisms also present on dedicated (missed on VeloxNavLink_0/_4). Not repeated again whatever the outcome.
S=/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad
R=$S/gate02/runset.sh
run() { flock -o "$S/soak.lock" "$R" "$@"; echo "SET $1 rc=$? $(date -u +%T)"; }
run G7-listen-botrun3-confirm --server-mode listen --port 7786 --scripts BotRun --repeat 3 --keep-going
run G8-listen-botduel3-confirm --server-mode listen --port 7787 --scripts BotDuel --repeat 3 --keep-going
echo "GATE2 DONE"
