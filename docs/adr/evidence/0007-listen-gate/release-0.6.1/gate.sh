#!/bin/bash
# gate.sh (M6-13, release 0.6.1): P1 default list, P2 BotRun x3, P3 BotDuel x3 against the packaged LinuxServer Development binary.
# usage: gate.sh [P1] [P2] [P3]    Ports 7910-7919 (assigned to this task).
S="${S:-/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad/t13}"
SC="$(dirname "$S")"
R="$(cd "$(dirname "$0")" && pwd)/runset.sh"
B="$S/srv/Velox/Binaries/Linux/Velox"
U="$S/userdir"
run() { flock -o "$SC/soak.lock" "$R" "$@"; echo "SET $1 rc=$? $(date -u +%T)"; }
for s in ${*:-P1 P2 P3}; do
  case $s in
    P1) run P1-default --server-binary "$B" --server-userdir "$U/p1" --port 7914 --repeat 1 --keep-going ;;
    P2) run P2-botrun3 --server-binary "$B" --server-userdir "$U/p2" --port 7916 --scripts BotRun --repeat 3 --keep-going ;;
    P3) run P3-botduel3 --server-binary "$B" --server-userdir "$U/p3" --port 7918 --scripts BotDuel --repeat 3 --keep-going ;;
  esac
done
echo "GATE DONE"
