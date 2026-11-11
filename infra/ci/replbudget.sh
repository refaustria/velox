#!/usr/bin/env bash
# Replication budget measurement (M6 task 10, ADR 0007 section 5.6, ADR 0001 section 3).
# One headless listen server on L_DM1 (FFA, 12 Medium bots, t.MaxFPS 60) and N headless -game -nullrhi clients (t.MaxFPS 60,
# idle players, velox.bots.Watch <seconds> ends each), a host sidecar (the nettest.sh functions) and a read-only sidecar of the
# live production server's CPU (background load on this host). The server prints `VeloxNetStats` every 10 s
# (Source/Velox/Net/VeloxNetStatsLog.cpp, velox.net.StatsLog 1); Tools/ci/replbudget_summary.py judges.
#
# Usage: infra/ci/replbudget.sh --clients N [--bots 12] [--seconds 180] [--port 7890] [--out DIR] [--map L_DM1] [--fps 60]
#        run under the soak lock:  flock -o <scratchpad>/soak.lock infra/ci/replbudget.sh --clients 2
# Output DIR (default $VELOX_OUT/replbudget-<time>/n<N>): server.log, client<i>.log, host.log, prod.log, gate.txt, run.txt,
# server.stats.log (the filtered lines the summary reads).
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$HERE/../.." && pwd)"
CI_PLATFORM=Linux; CI_SCRIPT_NAME=infra/ci/replbudget.sh; CI_BUILD_SH=""; CI_DEFAULT_UE_ROOTS=(/opt/UnrealEngine-5.8)
# shellcheck source=infra/ci/lib.sh
source "$HERE/lib.sh"
GREP=/usr/bin/grep

BOTDEBUG=1; CLIENTS=1; BOTS=12; SECS=180; PORT=7890; OUT=""; MAP=L_DM1; FPS=60
HOST_SAMPLE_S=5; LOAD_GATE="${VELOX_LOAD_GATE:-2.0}"; LOAD_GATE_WAIT="${VELOX_LOAD_GATE_WAIT:-120}"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --clients) CLIENTS="$2"; shift 2 ;;
    --bots)    BOTS="$2"; shift 2 ;;
    --seconds) SECS="$2"; shift 2 ;;
    --port)    PORT="$2"; shift 2 ;;
    --out)     OUT="$2"; shift 2 ;;
    --map)     MAP="$2"; shift 2 ;;
    --fps)     FPS="$2"; shift 2 ;;
    --bot-debug) BOTDEBUG="$2"; shift 2 ;;   # 0 = control run without velox.bots.Debug logging
    -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) die "unknown option: $1" ;;
  esac
done
[[ "$CLIENTS" =~ ^[0-9]+$ && "$PORT" =~ ^[0-9]+$ && "$SECS" =~ ^[0-9]+$ ]] || die "numeric --clients/--port/--seconds expected"
(( PORT != 7777 && PORT != 8790 )) || die "ports 7777/8790 belong to production"
load_env_file
[[ -n "${UE_ROOT:-}" && -n "${VELOX_PROJECT:-}" ]] || die "UE_ROOT / VELOX_PROJECT not set (infra/.env)"
EDITOR_CMD="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
[[ -x "$EDITOR_CMD" ]] || die "no $EDITOR_CMD"
[[ -n "$OUT" ]] || OUT="${VELOX_OUT:-$REPO_ROOT/Saved/CI}/replbudget-$(date +%Y%m%d-%H%M%S)/n$CLIENTS"
mkdir -p "$OUT"

# Host sidecar: the nettest.sh functions, extracted verbatim (that file is a runner with a main, not a library).
SAMPLER_PID=""
# shellcheck disable=SC1090
source <(sed -n '/^host_sample() {$/,/^}$/p;/^start_host_sampler() {$/,/^}$/p;/^stop_host_sampler() {$/,/^}$/p' "$HERE/nettest.sh")
declare -F host_sample start_host_sampler stop_host_sampler >/dev/null || die "host sidecar functions not found in nettest.sh"

SERVER_PID=""; PROD_SAMPLER=""; CLIENT_PIDS=()
kill_pid() { local p="$1"; [[ -n "$p" ]] || return 0; kill -0 "$p" 2>/dev/null || return 0; kill -TERM "$p" 2>/dev/null
  for _ in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$p" 2>/dev/null || return 0; sleep 0.5; done; kill -KILL "$p" 2>/dev/null || true; }
cleanup() {
  stop_host_sampler
  [[ -n "$PROD_SAMPLER" ]] && { kill "$PROD_SAMPLER" 2>/dev/null; wait "$PROD_SAMPLER" 2>/dev/null; PROD_SAMPLER=""; }
  local p; for p in "${CLIENT_PIDS[@]:-}"; do kill_pid "$p"; done; CLIENT_PIDS=()
  kill_pid "$SERVER_PID"; SERVER_PID=""
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# Production background load (read-only): CPU jiffies of the live server's main PID, every HOST_SAMPLE_S s.
PROD_PID="$(systemctl show -p MainPID --value velox-server@ffa 2>/dev/null || true)"
[[ "$PROD_PID" =~ ^[1-9][0-9]*$ ]] || PROD_PID=""
: > "$OUT/prod.log"
if [[ -n "$PROD_PID" ]]; then
  ( while true; do
      if [[ -r /proc/$PROD_PID/stat ]]; then
        st="$(sed 's/^.*) //' /proc/$PROD_PID/stat)"; set -- $st
        echo "t=$(date +%s.%N) prod_pid=$PROD_PID prod_cpu=$(( ${12:-0} + ${13:-0} ))" >> "$OUT/prod.log"
      fi
      sleep "$HOST_SAMPLE_S"
    done ) &
  PROD_SAMPLER=$!
fi

# Load gate (nettest.sh load_gate, simplified: no previous PIDs of ours to wait for)
waited=0
while true; do
  read -r load _ < /proc/loadavg
  if awk -v l="$load" -v g="$LOAD_GATE" 'BEGIN { exit !(l < g) }'; then break; fi
  if (( waited >= LOAD_GATE_WAIT )); then info "load gate: gave up after ${waited}s (load1=$load); starting anyway"; break; fi
  sleep 1; waited=$((waited + 1))
done
echo "load_start=$load gate=$LOAD_GATE gate_waited_s=$waited prod_pid=${PROD_PID:-none}" > "$OUT/gate.txt"
if ss -H -ulnp 2>/dev/null | $GREP -q ":${PORT}[[:space:]]"; then die "UDP port $PORT is in use"; fi

URL="${MAP}?game=FFA?Bots=${BOTS}?BotDifficulty=Medium?MaxPlayers=16?listen?SpectatorOnly=1?Name=Server"
info "server: $URL (${FPS} fps), $CLIENTS client(s), $SECS s, out $OUT"
t0="$(date +%s)"
"$EDITOR_CMD" "$VELOX_PROJECT" "$URL" -game -nullrhi -nosound -unattended -nopause -nosplash -log -stdout -FullStdOutLogOutput \
  -port="$PORT" -ExecCmds="t.MaxFPS $FPS, velox.bots.Debug $BOTDEBUG, velox.net.StatsLog 1" > "$OUT/server.log" 2>&1 &
SERVER_PID=$!
waited=0
while ! $GREP -aq "IpNetDriver listening on port $PORT" "$OUT/server.log"; do
  kill -0 "$SERVER_PID" 2>/dev/null || die "server exited during startup ($OUT/server.log)"
  (( waited >= 180 )) && die "server not ready after 180 s"
  sleep 1; waited=$((waited + 1))
done
info "server ready after ${waited}s"
start_host_sampler "$SERVER_PID" "$OUT/host.log"
sleep 2
for i in $(seq 1 "$CLIENTS"); do
  "$EDITOR_CMD" "$VELOX_PROJECT" "127.0.0.1:${PORT}?Name=C${i}" -game -log -stdout -FullStdOutLogOutput -nullrhi -nosound -unattended -nopause -nosplash \
    -ExecCmds="t.MaxFPS $FPS, velox.net.StatsLog 1, velox.bots.Watch $SECS" -WINDOWED -ResX=320 -ResY=240 > "$OUT/client$i.log" 2>&1 &
  CLIENT_PIDS+=($!)
  sleep 2
done
# wait for all clients (velox.bots.Watch quits each SECS after it connected), at most SECS + 120 s
waited=0
while :; do
  alive=0; for p in "${CLIENT_PIDS[@]}"; do kill -0 "$p" 2>/dev/null && alive=1; done
  (( alive )) || break
  kill -0 "$SERVER_PID" 2>/dev/null || { echo "server died" >&2; break; }
  (( waited >= SECS + 120 )) && { echo "client timeout" >&2; break; }
  sleep 1; waited=$((waited + 1))
done
host_sample "$SERVER_PID" "$OUT/host.log"
t1="$(date +%s)"
read -r load_end _ < /proc/loadavg; echo "load_end=$load_end" >> "$OUT/gate.txt"
sleep 1
cleanup
echo "clients=$CLIENTS bots=$BOTS seconds=$SECS map=$MAP fps=$FPS port=$PORT wall_s=$((t1 - t0)) server_mode=listen" > "$OUT/run.txt"
# the lines the summary reads (the full logs stay next to them but are not evidence)
$GREP -a -E 'VeloxNetStats|VeloxBotBudget|VeloxBotCost|VeloxMatch|LogNet: Warning|Error:' "$OUT/server.log" > "$OUT/server.stats.log" || true
for i in $(seq 1 "$CLIENTS"); do $GREP -a -E 'VeloxNetStats|VeloxBotWatch|Error:' "$OUT/client$i.log" > "$OUT/client$i.stats.log" || true; done
info "done: $OUT"
python3 -I "$REPO_ROOT/Tools/ci/replbudget_summary.py" --one "$OUT" || true
