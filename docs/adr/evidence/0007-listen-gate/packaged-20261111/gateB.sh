#!/bin/bash
# gateB.sh (M6-02b, 2026-11-11): packaged LinuxServer (Development) Velox binary as the headless listen host. Paths parameterised from 02.
# usage: gateB.sh [P1] [P2] [P3]
SC=/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad
S=$SC/t02b
R=/home/bojan/velox/.claude/worktrees/agent-a7a746c921cbb02bd/docs/adr/evidence/0007-listen-gate/packaged-20261111/runset.sh
B=$S/srv/Velox/Binaries/Linux/Velox
U=$S/userdir
run() { flock -o "$SC/soak.lock" "$R" "$@"; echo "SET $1 rc=$? $(date -u +%T)"; }
SETS="${*:-P1 P2 P3}"
for s in $SETS; do
  case $s in
    P1) run P1-packaged-default --server-binary "$B" --server-userdir "$U/p1" --port 7845 --repeat 1 --keep-going ;;
    P2) run P2-packaged-botrun3 --server-binary "$B" --server-userdir "$U/p2" --port 7848 --scripts BotRun --repeat 3 --keep-going ;;
    P3) run P3-packaged-botduel3 --server-binary "$B" --server-userdir "$U/p3" --port 7851 --scripts BotDuel --repeat 3 --keep-going ;;
  esac
done
echo "GATE B DONE"
