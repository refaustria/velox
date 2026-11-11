#!/bin/bash
# gateB.sh (M6-02 part B, 2026-11-06): packaged LinuxServer (Development) Velox binary as the headless listen host.
S=/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad
R=$S/gate02/runset.sh
B=$S/gate02/srv/Velox/Binaries/Linux/Velox
U=$S/gate02/srv-userdir
run() { flock -o "$S/soak.lock" "$R" "$@"; echo "SET $1 rc=$? $(date -u +%T)"; }
run P1-packaged-default --server-binary "$B" --server-userdir "$U/p1" --port 7783 --repeat 1 --keep-going
run P2-packaged-botrun3 --server-binary "$B" --server-userdir "$U/p2" --port 7784 --scripts BotRun --repeat 3 --keep-going
run P3-packaged-botduel3 --server-binary "$B" --server-userdir "$U/p3" --port 7785 --scripts BotDuel --repeat 3 --keep-going
echo "GATE B DONE"
