#!/bin/bash
# D36/D51 ten-run FFA soak (3 Medium bots, seeds 1..RUNS). W = the worktree, derived from this script's path (infra/ci -> ../..).
# UE_ROOT/VELOX_PROJECT come from the environment or infra/.env (defaults /opt/UnrealEngine-5.8, <W>/Velox.uproject).
# Usage: ffa.sh <port> <seconds> <runs> <outdir>
set -u
PORT=$1; SECS=$2; RUNS=$3; OUT=$4
W=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
# the real environment wins over infra/.env (same rule as lib.sh)
_UE=${UE_ROOT:-}; _PR=${VELOX_PROJECT:-}
if [ -f "$W/infra/.env" ]; then set -a; . "$W/infra/.env"; set +a; fi
UE_ROOT=${_UE:-${UE_ROOT:-/opt/UnrealEngine-5.8}}
PROJECT=${_PR:-${VELOX_PROJECT:-$W/Velox.uproject}}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
mkdir -p "$OUT"
for r in $(seq 1 "$RUNS"); do
  LOG="$OUT/ffa-$r.log"
  timeout -k 10 $((SECS + 120)) "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PROJECT" \
    "L_ItemTest?game=FFA?Bots=3?BotsFill=1?BotDifficulty=Medium" -server -nullrhi -unattended -nosound -nopause -nosplash \
    -log -port="$PORT" -abslog="$LOG" -ExecCmds="velox.bots.Seed $r, velox.bots.Debug 1, velox.bots.After $SECS quit" >/dev/null 2>&1
  echo "run $r exit=$?"
  sleep 5
done
for r in $(seq 1 "$RUNS"); do
  python3 -I "$HERE/ffa_summary.py" "$OUT/ffa-$r.log" "run $r"
done
python3 -I "$HERE/ffa_set.py" "$OUT"
