#!/bin/bash
# D50 acceptance FFA (task m4-followups/03): copy of 0005-D49-m4-final/ffa/ffa.sh; W = this worktree (derived from the script path).
# Usage: ffa.sh <port> <seconds> <runs> <outdir>
set -u
PORT=$1; SECS=$2; RUNS=$3; OUT=$4
W=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../../.." && pwd)
UE_ROOT=/opt/UnrealEngine-5.8
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
mkdir -p "$OUT"
for r in $(seq 1 "$RUNS"); do
  LOG="$OUT/ffa-$r.log"
  timeout -k 10 $((SECS + 120)) "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$W/Velox.uproject" \
    "L_ItemTest?game=FFA?Bots=3?BotsFill=1?BotDifficulty=Medium" -server -nullrhi -unattended -nosound -nopause -nosplash \
    -log -port="$PORT" -abslog="$LOG" -ExecCmds="velox.bots.Seed $r, velox.bots.Debug 1, velox.bots.After $SECS quit" >/dev/null 2>&1
  echo "run $r exit=$?"
  sleep 5
done
for r in $(seq 1 "$RUNS"); do
  python3 -I "$HERE/summary.py" "$OUT/ffa-$r.log" "run $r"
done
python3 -I "$HERE/set.py" "$OUT"
