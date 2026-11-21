#!/bin/bash
# D36/D51 ten-run FFA soak (3 Medium bots, seeds 1..RUNS). W = the worktree, derived from this script's path (infra/ci -> ../..).
# UE_ROOT/VELOX_PROJECT come from the environment or infra/.env (defaults /opt/UnrealEngine-5.8, <W>/Velox.uproject).
#
# Usage: ffa.sh [--map NAME] [--port N] [--seconds S] [--repeat RUNS] [--out DIR] [--bots N]      (defaults: L_ItemTest, 7777, 120, 10, Saved/CI/ffa-<time>)
#        ffa.sh <port> <seconds> <runs> <outdir>                                            (the original positional form)
#        ffa.sh --reeval DIR [--map NAME]                                                   (summary + set evaluation on saved ffa-*.log; no engine)
#
# --bots N sets the bot count of the server URL (default 3, so stored sets re-evaluate unchanged; ADR 0008 1.6).
# --map NAME replaces the map of the server URL (ADR 0006 section 7); the ?game=FFA?Bots=3?... options stay.
# Link success and item coverage (ADR 0006 E0.3 b/c, ffa_set.py): link success = ok / (ok + failed) per type, failed = reason missed,
# timeout, noimpulse, tooslow, not an attempt = replaced, below_start, ineligible, air_start, relaunched (task m5-06b; printed as
# aborted=<n>), judged from 30 attempts;
# item coverage is per pickup instance, keyed (definition, at=) from the VeloxPickupReset / VeloxPickup lines, per definition on old
# logs without at=; BAND_FFA_COVERAGE_EXEMPT is honoured on L_ItemTest / L_CombatTest / L_MovementTest only (else ignored, warning).
# Bands are per map: infra/ci/bands/<Map>.sh (lib.sh ci_load_bands; the default map L_ItemTest has one: the D36/D51 sum of kills >= 74
# over 10 runs, kills >= 2 / pickups >= 20 / Mega >= 1 per run). A map without a band file runs report-only (`band=none`) and prints
# every metric. The map-independent invariants are judged on every map, see ffa_set.py: left_arena, world_kill, no_nav_point, giveup,
# sum of stuck strikes <= 5, bursts 0, max dwell <= 5 s, nav build <= 2 s, link traversal success >= 90 % per type, item coverage.
# Exit status 1 when the set FAILs. Run it on an idle host under the soak lock.
set -u
W=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# the real environment wins over infra/.env (same rule as lib.sh)
_UE=${UE_ROOT:-}; _PR=${VELOX_PROJECT:-}
if [ -f "$W/infra/.env" ]; then set -a; . "$W/infra/.env"; set +a; fi
UE_ROOT=${_UE:-${UE_ROOT:-/opt/UnrealEngine-5.8}}
PROJECT=${_PR:-${VELOX_PROJECT:-$W/Velox.uproject}}
# shellcheck source=infra/ci/lib.sh
source "$HERE/lib.sh"

PORT=7777; SECS=120; RUNS=10; BOTS=3; OUT=""; MAP=""; REEVAL=""
POS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --map)     [ $# -ge 2 ] && [[ "$2" =~ ^[A-Za-z0-9_]+$ ]] || die "--map needs a map name"; MAP=$2; shift 2 ;;
    --port)    [ $# -ge 2 ] || die "--port needs a value"; PORT=$2; shift 2 ;;
    --seconds) [ $# -ge 2 ] || die "--seconds needs a value"; SECS=$2; shift 2 ;;
    --repeat)  [ $# -ge 2 ] && [[ "$2" =~ ^[1-9][0-9]*$ ]] || die "--repeat needs a positive integer"; RUNS=$2; shift 2 ;;
    --bots)    [ $# -ge 2 ] && [[ "$2" =~ ^[1-9][0-9]*$ ]] && [ "$2" -le 15 ] || die "--bots needs an integer 1..15"; BOTS=$2; shift 2 ;;
    --out)     [ $# -ge 2 ] || die "--out needs a directory"; OUT=$2; shift 2 ;;
    --reeval)  [ $# -ge 2 ] || die "--reeval needs a directory"; REEVAL=$2; shift 2 ;;
    -h|--help) sed -n '2,15p' "${BASH_SOURCE[0]}"; exit 0 ;;
    --*)       die "unknown option: $1" ;;
    *)         POS+=("$1"); shift ;;
  esac
done
if [ ${#POS[@]} -eq 4 ]; then PORT=${POS[0]}; SECS=${POS[1]}; RUNS=${POS[2]}; OUT=${POS[3]}
elif [ ${#POS[@]} -ne 0 ]; then die "positional form needs exactly <port> <seconds> <runs> <outdir>"; fi

ci_load_bands "${MAP:-L_ItemTest}"
if [ "$VELOX_BAND_NAME" = none ]; then echo "[ci] band=none (report-only) for map ${MAP:-L_ItemTest}"; else echo "[ci] bands=${VELOX_BAND_LABEL:-$VELOX_BAND_NAME}"; fi

if [ -n "$REEVAL" ]; then
  # The saved ffa-*.log are re-summarised into a scratch copy (the saved .json and logs stay untouched).
  [ -d "$REEVAL" ] || die "--reeval: not a directory: $REEVAL"
  mkdir -p "${VELOX_OUT:-$W/Saved/CI}"
  OUT=$(mktemp -d "${VELOX_OUT:-$W/Saved/CI}/ffa-reeval-XXXXXX")
  cp "$REEVAL"/ffa-*.log "$OUT"/ || die "--reeval: no ffa-*.log in $REEVAL"
  RUNS=$(ls "$OUT"/ffa-*.log | wc -l)
else
  [ -n "$OUT" ] || OUT="${VELOX_OUT:-$W/Saved/CI}/ffa-$(date +%Y%m%d-%H%M%S)-$$"
  mkdir -p "$OUT"
  URL="${MAP:-L_ItemTest}?game=FFA?Bots=${BOTS}?BotsFill=1?BotDifficulty=Medium"
  for r in $(seq 1 "$RUNS"); do
    LOG="$OUT/ffa-$r.log"
    timeout -k 10 $((SECS + 120)) "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PROJECT" \
      "$URL" -server -nullrhi -unattended -nosound -nopause -nosplash \
      -log -port="$PORT" -abslog="$LOG" -ExecCmds="velox.bots.Seed $r, velox.bots.Debug 1, velox.bots.After $SECS quit" >"$LOG.stdout" 2>&1
    rc=$?
    echo "run $r exit=$rc"
    # M7 H9: a start-up crash (exit 139 / 134 with no or a short log) was seen once (H11, Nemus); keep what the engine printed and the
    # crash report path, then retry that run once (the retry is noted; the first attempt is kept as crash1-ffa-$r.*).
    if [ "$rc" -ge 128 ] && [ "$rc" -ne 143 ]; then
      echo "[ci] run $r crashed (exit $rc, $(wc -c < "$LOG" 2>/dev/null || echo 0) log bytes); kept as crash1-ffa-$r.*; retrying once"
      mv -f "$LOG" "$OUT/crash1-ffa-$r.txt" 2>/dev/null; mv -f "$LOG.stdout" "$OUT/crash1-ffa-$r.stdout" 2>/dev/null
      ls -dt "$W"/Saved/Crashes/* 2>/dev/null | head -1 | sed 's/^/[ci] latest crash dir: /'
      sleep 5
      timeout -k 10 $((SECS + 120)) "$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd" "$PROJECT" \
        "$URL" -server -nullrhi -unattended -nosound -nopause -nosplash \
        -log -port="$PORT" -abslog="$LOG" -ExecCmds="velox.bots.Seed $r, velox.bots.Debug 1, velox.bots.After $SECS quit" >"$LOG.stdout" 2>&1
      echo "run $r retry exit=$?"
    fi
    sleep 5
  done
fi
for r in $(seq 1 "$RUNS"); do
  python3 -I "$HERE/ffa_summary.py" "$OUT/ffa-$r.log" "run $r"
done
python3 -I "$HERE/ffa_set.py" "$OUT"
rc=$?
echo "Logs: $OUT"
exit $rc
