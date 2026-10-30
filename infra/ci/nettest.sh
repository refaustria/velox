#!/usr/bin/env bash
# Headless networked movement soak (M1 task 05, ADR 0002 section 5 and amendments A17-A34).
# Starts a dedicated server and one scripted -nullrhi client on this machine with 100 ms / 2 % packet simulation in both
# directions, runs the soak scripts and applies the per-script pass criteria from
# docs/tasks/m1-movement/05-debug-tooling-and-net-soak.md. See infra/ci/README.md.
set -uo pipefail

CI_SCRIPT_NAME="infra/ci/nettest.sh"
CI_PLATFORM="Linux"
CI_BUILD_SH="Engine/Build/BatchFiles/Linux/Build.sh"
CI_DEFAULT_UE_ROOTS=(/opt/UnrealEngine-5.8)
# shellcheck source=infra/ci/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

GREP=/usr/bin/grep   # the interactive shell's grep is a ugrep wrapper; use the real one
PORT=7777
SCRIPTS_ARG="StrafeRun,PadRun,KnockbackRun,KnockbackGroundRun,LatchRun,CrouchRun,FireRun,FireRunControl,RocketJumpRun,ItemRun,HasteRun,BotRun,BotDuel"
REPEAT=1
KEEP_GOING=0
CLIENT_TIMEOUT="${VELOX_NETTEST_TIMEOUT:-120}"
SERVER_START_TIMEOUT="${VELOX_NETTEST_SERVER_START_TIMEOUT:-180}"
SERVER_FPS=60
CLIENT_FPS=120
# Client-side packet simulation: outgoing (PktLag/PktLoss) and incoming (PktIncoming*), one-way lag per script (default
# 100 ms, RTT about 200 ms; FireRun/FireRunControl 50 ms = the ADR 0003 latency of 100 ms RTT, task 08b D2), 2 % loss
# each way. --lag N sets the one-way lag of every script (incoming N +- 20 ms).
LAG_MS=100
FIRE_LAG_MS=50
LAG_OVERRIDE=""
PKT_ARGS=()
# Scripts on L_CombatTest (task 08); everything else runs on L_MovementTest.
COMBAT_SCRIPTS=" FireRun FireRunControl FireRunHold RocketJumpRun RocketJumpRunHold PlasmaRun "
# Scripts on L_ItemTest (M3 task 04); the client runs velox.items.Watch.
ITEM_SCRIPTS=" ItemRun "
# Bot soaks (M4 task 07, ADR 0005 section 14 / D36), both on L_ItemTest with the server options in the map URL.
BOT_SCRIPTS=" BotRun BotDuel "
BOT_RUN_SECONDS=90      # BotWatch.json
BOT_DUEL_SECONDS=120    # velox.bots.Watch 120 (wall time after the spectator connected)

nettest_usage() {
  cat <<USAGE
Usage: ${CI_SCRIPT_NAME} [options]

Runs dedicated server + scripted -nullrhi client (100 ms / 2 % loss both directions) per script and checks the
net-soak pass criteria. Exit code is non-zero if any script fails. FireRun, FireRunControl (FireRun.json with
velox.combat.LagComp 0) and RocketJumpRun (velox.combat.RocketJumpTest -90) run on L_CombatTest. ItemRun runs on
L_ItemTest. HasteRun (M3 task 03) runs on L_MovementTest. FireRunHold (task 08g, not in the default list) is FireRun
with the first three rail RPCs held 300 ms on the server (velox.combat.DebugHoldShotRpc 300 7 3). RocketJumpRunHold
(task 08f, not in the default list) is RocketJumpRun with the server holding the rocket's fire RPC 300 ms
(velox.combat.DebugHoldShotRpc 300 5 1, ADR 0003 B35). PlasmaRun (task 08i, not in the default list) holds plasma fire
for 5 s at 50 ms one way (ADR 0003 B38: one move-carried schedule per shot sequence).
BotRun and BotDuel (M4 task 07, ADR 0005 D36) run on L_ItemTest with the server's game options in the map URL. BotRun:
FFA, 3 Medium bots (velox.bots.Debug 1), the client plays BotWatch.json (90 s, no input) with velox.bots.Watch.
BotDuel: Duel, 2 Hard bots, the client joins with ?SpectatorOnly=1 and velox.bots.Watch 120 quits it. Both are judged
from the server log (per-round rules); after the last round the runner evaluates the repeat set (D36: sums over
--repeat 3) from <script>.stats.json and prints "set evaluation" lines. Set bands are only judged for --repeat 3.

Options:
  --scripts a,b    Scripts to run (default: ${SCRIPTS_ARG})
  --port N         Server UDP port (default ${PORT}; must be free)
  --timeout S      Seconds to wait for a client's VeloxNetSoak line (default ${CLIENT_TIMEOUT})
  --lag MS         One-way packet lag in ms for every script (default ${LAG_MS}, FireRun/FireRunControl ${FIRE_LAG_MS};
                   incoming MS +- 20); loss stays 2 % each way
  --repeat N       Run the script list N times (default 1); stops after the first failing round unless --keep-going
  --keep-going     With --repeat: run all N rounds and report the pass count
  -h, --help       Show this help

Environment: UE_ROOT, VELOX_PROJECT, VELOX_OUT (as for build.sh), VELOX_NETTEST_TIMEOUT.
Logs: \$VELOX_OUT/<timestamp>-<pid>/nettest/[round-<k>/]<script>-{server,client}.log
USAGE
}

SERVER_PID=""
CLIENT_PID=""

kill_proc() {
  local pid="$1"
  [[ -n "$pid" ]] || return 0
  kill -0 "$pid" 2>/dev/null || return 0
  kill -TERM "$pid" 2>/dev/null || true
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    kill -0 "$pid" 2>/dev/null || return 0
    sleep 0.5
  done
  kill -KILL "$pid" 2>/dev/null || true
}

cleanup() {
  kill_proc "$CLIENT_PID"; CLIENT_PID=""
  kill_proc "$SERVER_PID"; SERVER_PID=""
}
trap cleanup EXIT
trap 'exit 130' INT TERM

# One-way lag of a script: --lag if given, else FIRE_LAG_MS for the hitscan soak (ADR 0003 B17 is written for 100 ms
# RTT; MaxRewindSeconds 0.25 cannot cover a 200 ms RTT plus the interpolation delay), LAG_MS for everything else.
script_lag() {
  if [[ -n "$LAG_OVERRIDE" ]]; then echo "$LAG_OVERRIDE"; return; fi
  case "$1" in
    FireRun|FireRunControl|FireRunHold|PlasmaRun) echo "$FIRE_LAG_MS" ;;
    *) echo "$LAG_MS" ;;
  esac
}

port_busy() { ss -H -ulnp 2>/dev/null | "$GREP" -q ":${PORT}[[:space:]]"; }

# run_one <script> : leaves results in $OUT_DIR/<script>.result; returns 0 pass / 1 fail
run_one() {
  local script="$1" slog clog extra=() crc=0 waited=0
  slog="$OUT_DIR/$script-server.log"; clog="$OUT_DIR/$script-client.log"
  # Real-time pacing, not -UseFixedTimeStep: that mode runs frames as fast as possible, so the (wall-clock) packet
  # simulation would add seconds of game time per 100 ms. The server is paced by NetServerMaxTickRate=60, the client by t.MaxFPS.
  # LogVeloxMovement Verbose on the client: counts the A25 "ClientAckGoodMove ... ignored" lines.
  # LogVeloxNet Verbose on the client: one VeloxResend/VeloxSend line per ServerMovePacked RPC (A32/A34 resend counts).
  local cexec="t.MaxFPS ${CLIENT_FPS}, log LogVeloxMovement Verbose, log LogVeloxNet Verbose" sexec="" map="L_MovementTest"
  local script_args=(-VeloxScript="$script") lag
  lag="$(script_lag "$script")"
  PKT_ARGS=(-PktLag="$lag" -PktLagVariance=20 -PktLoss=2 -PktIncomingLagMin=$((lag - 20)) -PktIncomingLagMax=$((lag + 20)) -PktIncomingLoss=2)
  if [[ "$script" == LatchRun ]]; then cexec="velox.move.Preset Q3, $cexec"; sexec="velox.move.Preset Q3"; fi
  if [[ "$COMBAT_SCRIPTS" == *" $script "* ]]; then map="L_CombatTest"; fi
  local connect_url="127.0.0.1:$PORT" client_timeout="$CLIENT_TIMEOUT"
  case "$script" in
    BotRun)
      # FFA with 3 Medium bots; the playing client idles through BotWatch.json and velox.bots.Watch 90 ends it. Client
      # corrections are report-only (LogCorrections), rocket hits on the human are legitimate corrections.
      map="L_ItemTest?game=FFA?Bots=3?BotDifficulty=Medium"; sexec="velox.bots.Debug 1"
      cexec="$cexec, velox.move.LogCorrections 1, velox.bots.Watch $BOT_RUN_SECONDS"; script_args=(-VeloxScript=BotWatch)
      client_timeout=$(( CLIENT_TIMEOUT > BOT_RUN_SECONDS + 90 ? CLIENT_TIMEOUT : BOT_RUN_SECONDS + 90 )) ;;
    BotDuel)
      # Two Hard bots duel; the client is a spectator and quits itself 120 s after it connected.
      map="L_ItemTest?game=Duel?Bots=2?BotsFill=1?BotDifficulty=Hard"; sexec="velox.bots.Debug 1"
      cexec="$cexec, velox.bots.Watch $BOT_DUEL_SECONDS"; connect_url="127.0.0.1:$PORT?SpectatorOnly=1"; script_args=()
      client_timeout=$(( CLIENT_TIMEOUT > BOT_DUEL_SECONDS + 90 ? CLIENT_TIMEOUT : BOT_DUEL_SECONDS + 90 )) ;;
  esac
  if [[ "$ITEM_SCRIPTS" == *" $script "* ]]; then
    # M3 task 04 / ADR 0004 section 10: velox.items.Watch logs the client's placeholder and Health/Armor/Bullets changes.
    map="L_ItemTest"; cexec="$cexec, velox.items.Watch"
  fi
  case "$script" in
    FireRun|FireRunControl|FireRunHold)
      # Task 08: invulnerable strafing target at FireRun.json's dummyLocation, every weapon, per-pawn rewind lines.
      # FireRunHold (task 08g, ADR 0003 B36): FireRun with the server holding the first three rail target-data RPCs
      # (slot 7) for 300 ms each, a deterministic stand-in for a reliable resend.
      local dummy lagcomp=1
      dummy="$(python3 -I -c 'import json,sys; print(" ".join("%g" % v for v in json.load(open(sys.argv[1]))["dummyLocation"]))' \
        "$REPO_ROOT/Content/Data/MoveScripts/FireRun.json")" || { echo "FAILED: FireRun.json has no dummyLocation" >&2; return 1; }
      [[ "$script" == FireRunControl ]] && lagcomp=0
      sexec="velox.combat.GiveAllWeapons, velox.combat.SpawnDummy $dummy invulnerable, velox.lagcomp.Debug 1, velox.combat.LagComp $lagcomp"
      [[ "$script" == FireRunHold ]] && sexec="$sexec, velox.combat.DebugHoldShotRpc 300 7 3"
      script_args=(-VeloxScript=FireRun) ;;
    RocketJumpRun|RocketJumpRunHold)
      # Task 08 / ADR 0003 B25: wraps the task-05 test command; VeloxCorrection lines come from velox.move.LogCorrections.
      sexec="velox.combat.GiveAllWeapons"
      # Task 08f / ADR 0003 B35: forced late fire RPC (the server holds the RL target data 300 ms, as a lost packet's
      # reliable resend would); the fire move itself is on time, so the self splash must come from the move stream.
      [[ "$script" == RocketJumpRunHold ]] && sexec="$sexec, velox.combat.DebugHoldShotRpc 300 5 1"
      cexec="$cexec, velox.move.LogCorrections 1, velox.combat.RocketJumpTest -90 quit"
      script_args=() ;;
    PlasmaRun)
      # Task 08i / ADR 0003 B38: PlasmaRun.json holds plasma fire for 5 s (the 0.1 s interval keeps several fire RPCs
      # inside one pending timeout); every weapon from the server.
      sexec="velox.combat.GiveAllWeapons" ;;
  esac
  # Debug knob: VELOX_NETTEST_EXEC="p.NetShowCorrections 1" runs extra console commands on both processes.
  if [[ -n "${VELOX_NETTEST_EXEC:-}" ]]; then cexec="$cexec, $VELOX_NETTEST_EXEC"; sexec="${sexec:+$sexec, }$VELOX_NETTEST_EXEC"; fi
  if [[ -n "$sexec" ]]; then extra=(-ExecCmds="$sexec"); fi
  if port_busy; then echo "FAILED: UDP port $PORT is in use (ss -ulnp)" >&2; return 1; fi

  info "[$script] starting dedicated server (${SERVER_FPS} Hz), log $slog"
  "$EDITOR_CMD" "$VELOX_PROJECT" "$map" -server -log -stdout -FullStdOutLogOutput -nullrhi -unattended -nopause -nosplash \
    -port="$PORT" "${extra[@]}" >"$slog" 2>&1 &
  SERVER_PID=$!
  while ! "$GREP" -aq "Game Engine Initialized" "$slog"; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then echo "FAILED: server exited during startup (log: $slog)" >&2; SERVER_PID=""; return 1; fi
    if (( waited >= SERVER_START_TIMEOUT )); then echo "FAILED: server not ready after ${SERVER_START_TIMEOUT}s (log: $slog)" >&2; cleanup; return 1; fi
    sleep 1; waited=$((waited + 1))
  done
  sleep 2

  info "[$script] starting client (${CLIENT_FPS} Hz, packet simulation ${lag} ms one way), log $clog"
  "$EDITOR_CMD" "$VELOX_PROJECT" "$connect_url" -game -log -stdout -FullStdOutLogOutput -nullrhi -nosound -unattended -nopause -nosplash \
    -ExecCmds="$cexec" "${script_args[@]}" -WINDOWED -ResX=320 -ResY=240 \
    "${PKT_ARGS[@]}" >"$clog" 2>&1 &
  CLIENT_PID=$!
  waited=0
  while kill -0 "$CLIENT_PID" 2>/dev/null; do
    if (( waited >= client_timeout )); then echo "[$script] client timeout after ${client_timeout}s" >&2; break; fi
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then echo "[$script] server died" >&2; break; fi
    sleep 1; waited=$((waited + 1))
  done
  if kill -0 "$CLIENT_PID" 2>/dev/null; then
    crc=124
  else
    wait "$CLIENT_PID" 2>/dev/null; crc=$?
  fi
  CLIENT_PID=""
  local server_alive=1
  kill -0 "$SERVER_PID" 2>/dev/null || server_alive=0
  sleep 1
  cleanup

  python3 -I - "$script" "$slog" "$clog" "$crc" "$server_alive" "$OUT_DIR/$script.result" "$lag" "$OUT_DIR" <<'PY'
import math, os, re, statistics, sys

script, slog, clog, crc, server_alive, result_path, lag_ms, out_dir = sys.argv[1:9]
crc = int(crc); server_alive = server_alive == "1"
# Round trip of the packet simulation (one-way lag each direction plus the incoming jitter bound).
rtt = 2.0 * float(lag_ms) / 1000.0 + 0.04

def read(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()

def kv(line):
    return dict(re.findall(r"(\w+)=(\S+)", line))

def num(d, k, default=0.0):
    try:
        return float(d[k])
    except (KeyError, ValueError):
        return default

S, C = read(slog), read(clog)
fails, notes = [], []

# ---- generic failures
for name, lines in (("server", S), ("client", C)):
    for l in lines:
        if "Ensure condition failed" in l:
            fails.append("%s: ensure failed: %s" % (name, l.strip()[:160])); break
    for l in lines:
        if re.search(r"LogNetPlayerMovement: Warning.*(TimeStamp expired|discrepancy)", l, re.I):
            fails.append("%s: %s" % (name, l.strip()[:160])); break
    for l in lines:
        if re.search(r"Fatal error|Assertion failed|Signal 11|SIGSEGV|CRASH", l):
            fails.append("%s: crash marker: %s" % (name, l.strip()[:160])); break
if not server_alive:
    fails.append("server process died during the run")
if crc != 0:
    fails.append("client exit code %d" % crc)

if script in ("RocketJumpRun", "RocketJumpRunHold"):
    # velox.combat.RocketJumpTest's result line instead of a script's VeloxNetSoak line.
    final = next((l for l in reversed(C) if "VeloxRocketJump: apex_cm=" in l), None)
    if final is None:
        fails.append("no VeloxRocketJump result line (client timeout or crash)")
        f = {}
    else:
        f = kv(final)
        if f.get("result") != "PASS":
            fails.append("RocketJumpTest result=%s: %s" % (f.get("result"), final.split("VeloxRocketJump:", 1)[-1].strip()[:200]))
elif script in ("BotRun", "BotDuel"):
    # velox.bots.Watch ends the client after the wall-time window (exit code 0); BotWatch.json is only the idle human. Its
    # script clock stops while the human pawn is dead (bots kill it), so there is no VeloxNetSoak result line to demand.
    f = {}
else:
    final = next((l for l in reversed(C) if "VeloxNetSoak: script=" in l), None)
    if final is None:
        fails.append("no VeloxNetSoak line (client timeout or crash)")
        f = {}
    else:
        f = kv(final)
        if f.get("result") != "PASS":
            fails.append("script result=%s (%s)" % (f.get("result"), final.split("reason=", 1)[-1]))
moves = int(num(f, "moves"))

# ---- client events
corr = []     # (t, cm, fields), in log (= time) order
for l in C:
    if "VeloxCorrection t=" in l:
        d = kv(l); corr.append((num(d, "t"), num(d, "cm"), d))
info_line = next((kv(l) for l in C if "event=info" in l), {})
ground_speed = num(info_line, "ground_speed", 800.0)
jump_velocity = num(info_line, "jump_velocity", 0.0)
gravity = abs(num(info_line, "gravity_z", 0.0))
grace_max = num(info_line, "knockback_grace_max", 0.2)
# A30(ii): GroundFriction / StopSpeed. The info line does not carry them yet (VeloxScriptedInputComponent); the defaults
# of UVeloxMovementSettings (6.0 1/s, 250 cm/s) apply to every script except LatchRun (Preset Q3, which keeps both).
ground_friction = num(info_line, "ground_friction", 6.0)
stop_speed = num(info_line, "stop_speed", 250.0)
# A28(iv)/A34(i): server moves longer than 20 ms (lost RPCs merged into the next move; also slow client frames).
gaps = [kv(l) for l in S if "VeloxMoveGap ts=" in l]
# A32/A34(ii): client RPCs (VeloxResend = carried an OldMove, VeloxSend = none) and server-side repaired losses.
rpc_lines = [kv(l) for l in C if "VeloxResend old=" in l or "VeloxSend new=" in l]
resends = [d for d in rpc_lines if "old" in d]
repaired = [kv(l) for l in S if "VeloxMoveRepaired ts=" in l]
# Task M1-08 (A35): repairs by the second old-move slot (slot=extra); `repaired` counts both slots.
repaired_extra = sum(1 for l in S if "VeloxMoveRepaired ts=" in l and "slot=extra" in l)
# Optional position logs (VELOX_NETTEST_EXEC="log LogNetPlayerMovement VeryVerbose"): move stamp -> position.
POS_RE = re.compile(r"(ClientMove|ServerMove) Time (\S+) .*?Position X=(\S+) Y=(\S+) Z=(\S+)")
def positions(lines, kind):
    out = {}
    for l in lines:
        m = POS_RE.search(l)
        if m and m.group(1) == kind:
            try:
                out[m.group(2)] = tuple(float(v) for v in m.group(3, 4, 5))
            except ValueError:
                pass
    return out
responses = [kv(l) for l in C if "VeloxCorrectionResponse t=" in l]
samples = [kv(l) for l in C if "event=post_correction_sample" in l]
lands = [kv(l) for l in C if "event=land" in l and "VeloxNetSoak" in l]
kb = [kv(l) for l in C if "VeloxNetSoak event=knockback " in l]
replays = [l for l in C if "VeloxReplay t=" in l]
acks_ignored = sum(1 for l in C if "ClientAckGoodMove" in l and "ignored: correction pending replay" in l)

def response_for(c):
    """The VeloxCorrectionResponse logged for correction c (same world time t, same format)."""
    m = [r for r in responses if r.get("t") == c[2].get("t")]
    return m[-1] if m else None

# ---- echo accounting (ADR 0002 A24): an echo is cm < 1.0 within [t_c, t_c + 0.35] after a non-echo correction at t_c.
ECHO_CM, ECHO_WINDOW, MAX_ECHOES = 1.0, 0.35, 5
nonecho, echoes, echoes_of = [], [], {}
last_ne = None
for c in corr:
    if c[1] < ECHO_CM and last_ne is not None and last_ne[0] <= c[0] <= last_ne[0] + ECHO_WINDOW:
        echoes.append(c); echoes_of[id(last_ne)].append(c)
    else:
        nonecho.append(c); echoes_of[id(c)] = []; last_ne = c
for c in nonecho:
    if len(echoes_of[id(c)]) > MAX_ECHOES:
        fails.append("replay does not converge: %d echoes after the %.1f cm correction at t=%.2f (allowed %d)" % (len(echoes_of[id(c)]), c[1], c[0], MAX_ECHOES))

# ---- replay start (ADR 0002 A25 regression check)
replay_late = 0
for l in replays:
    d = kv(l)
    if num(d, "start") - num(d, "expected") > 0.5 / 120.0:
        replay_late += 1
        if replay_late == 1:
            fails.append("replay starts after the corrected move (A25 regression): %s" % l.split("VeloxReplay", 1)[-1].strip()[:160])

def in_window(t0, t1, pool=None):
    return [c for c in (nonecho if pool is None else pool) if t0 <= c[0] <= t1]

def fmt(cs):
    return [(round(c[0], 2), round(c[1], 1)) for c in cs]

def stray_only(t0, t1, what, exclude=()):
    w = [c for c in in_window(t0, t1) if c not in exclude]
    if len(w) > 1 or any(c[1] > 5.0 for c in w):
        fails.append("%s: non-echo corrections in window [%.2f,%.2f]: %s (allowed: one <= 5 cm)" % (what, t0, t1, fmt(w)))
    return w

Lexp = 0.02 * moves
detail = ["Lexp=%.1f" % Lexp]
excluded = []

def knockback_checks(airborne):
    """KnockbackRun (lift-off) / KnockbackGroundRun (stays Walking): task 05, A24-A26 block."""
    if not kb:
        fails.append("no knockback event on the client"); return
    tk = num(kb[0], "t")
    w = in_window(tk, tk + 0.5)
    if not w:
        fails.append("no non-echo correction in [t_k, t_k+0.5] after the knockback (t_k=%.2f)" % tk); return
    intended = max(w, key=lambda c: c[1])
    others = [c for c in w if c is not intended]
    if len(others) > 1 or any(c[1] > 5.0 for c in others):
        fails.append("more than one stray correction beyond the intended one: %s" % fmt(others))
    excluded.append(intended)
    tc, cm, d = intended
    resp = response_for(intended)
    rgrace = num(resp, "grace", -1.0) if resp else -1.0
    detail.append("intended=%.1f cm at +%.2fs echoes=%d response_grace=%.3f air=%s" % (cm, tc - tk, len(echoes_of[id(intended)]), rgrace, d.get("air")))
    if airborne and d.get("air") != "1":
        fails.append("client not airborne right after the knockback correction")
    if not airborne and d.get("air") != "0":
        fails.append("client airborne after the ground knockback correction (want Walking)")
    # Review 05/06 defect 9: the exact relation instead of a fixed grace_max - 2/60 (a merged or lost launch move shifts
    # the first response by whole moves). The server logs the move that consumed the knockback (VeloxKnockbackConsumed);
    # the response for ts_resp must carry grace - (ts_resp - ts_start) within 1e-3 (A18 snapshot, A27 simulated time).
    consumed = [kv(l) for l in S if "VeloxKnockbackConsumed ts_start=" in l]
    rts = num(resp, "ts", -1.0) if resp else -1.0
    cons = [c for c in consumed if num(c, "ts_end") <= rts + 1e-6]
    if not consumed:
        fails.append("no VeloxKnockbackConsumed line on the server (binary older than task 08?)")
    elif not cons or resp is None:
        fails.append("no VeloxKnockbackConsumed before the intended correction's response ts=%s" % (resp.get("ts") if resp else "?"))
    else:
        c0 = cons[-1]
        want = max(0.0, num(c0, "grace") - (rts - num(c0, "ts_start")))
        detail.append("consumed=[%s,%s] want_grace=%.4f" % (c0.get("ts_start"), c0.get("ts_end"), want))
        if abs(rgrace - want) > 1e-3:
            fails.append("knockback grace in the server's response %.4f at ts=%s, want %.4f = grace %s - (ts_resp - ts_start %s): response container lost it" % (rgrace, resp.get("ts"), want, c0.get("grace"), c0.get("ts_start")))
    # A18: each echo's response grace continues the countdown of the previous response by the server-time difference.
    prev = resp
    for e in echoes_of[id(intended)]:
        er = response_for(e)
        if er is None or prev is None:
            fails.append("echo at t=%.2f has no matching VeloxCorrectionResponse" % e[0]); break
        want = max(0.0, num(prev, "grace") - (num(er, "ts") - num(prev, "ts")))
        if abs(num(er, "grace") - want) > 1e-3:
            fails.append("echo grace chain broken at ts=%s: grace %.4f, want %.4f (previous ts=%s grace=%s)" % (er.get("ts"), num(er, "grace"), want, prev.get("ts"), prev.get("grace")))
            break
        prev = er
    if airborne:
        smp = [s for s in samples if abs(num(s, "t") - (tc + 0.15)) < 0.1]
        if not smp:
            fails.append("no speed sample 0.15 s after the correction")
        else:
            sp = num(smp[0], "speed2d")
            detail.append("speed+0.15s=%.0f" % sp)
            if sp < 0.9 * 1250:
                fails.append("speed 0.15 s after the correction %.0f < %.0f (friction not suppressed)" % (sp, 0.9 * 1250))
    else:
        # Friction suppression online. The task file compares GetSpeed2D() right after the replay with the response's svel
        # (1 %). That is impossible by construction: the replay spans one RTT (~0.24 s) > the response grace (~0.18 s),
        # so friction rightly acts on the last replayed moves (1250 -> ~876 after ~7 moves of 1/120 s). Exact form used
        # instead: while the response grace is > 0 the server's svel stays within 1 % of the intended correction's svel
        # (server ran friction-free), and every response of the burst is an echo (cm < 1: the client's replay reproduced
        # the server's trajectory; a client replaying with friction would be off by ~125 cm/s x 0.1 s = 12 cm, a non-echo
        # correction caught by the window rule below).
        svel = num(resp, "svel", -1.0) if resp else -1.0
        sp = num(d, "speed2d")
        detail.append("speed2d_after_replay=%.1f svel=%.1f" % (sp, svel))
        if svel <= 0:
            fails.append("intended correction's response has no svel (> 0)")
        for e in echoes_of[id(intended)]:
            er = response_for(e)
            if er is not None and num(er, "grace") > 0 and abs(num(er, "svel") - svel) > 0.01 * svel:
                fails.append("server velocity %.1f at ts=%s (grace %.3f > 0) not within 1 %% of %.1f: friction during the grace" % (num(er, "svel"), er.get("ts"), num(er, "grace"), svel))
                break
        # A30(ii): the server's friction starts at the grace expiry t_exp = ts_c + grace_c, whatever its move partition
        # (A27). Every burst response after the expiry (grace 0) must follow svel_c * exp(-GroundFriction * (ts - t_exp))
        # within 3 % while that value exceeds 1.2 x StopSpeed (below it Q3 friction is linear, not exponential).
        if resp is not None and svel > 0 and rgrace >= 0:
            t_exp = num(resp, "ts") + rgrace
            checked = 0
            for c in corr:
                if not (tc <= c[0] <= tc + 0.6):
                    continue
                r = response_for(c)
                if r is None or num(r, "grace") > 0 or num(r, "ts") <= t_exp:
                    continue
                want = svel * math.exp(-ground_friction * (num(r, "ts") - t_exp))
                if want <= 1.2 * stop_speed:
                    continue
                checked += 1
                if abs(num(r, "svel") - want) > 0.03 * want:
                    fails.append("friction start (A30): svel %.1f at ts=%s, want %.1f +- 3 %% (t_exp=%.6f, GroundFriction %.2f): server friction did not start at the grace expiry" % (num(r, "svel"), r.get("ts"), want, t_exp, ground_friction))
                    break
            detail.append("t_exp=%.6f friction_checks=%d" % (t_exp, checked))
    late = [c for c in in_window(tc + 0.05, tc + 0.6) if c is not intended]
    if late:
        why = ""
        if not airborne:
            # A34(iv): a merged server move during the friction decay (A31 class) shortly before the stray's acked move.
            for c in late:
                r = response_for(c)
                rts = num(r, "ts", -1.0) if r else -1.0
                g = next((g for g in gaps if rts - 0.2 <= num(g, "ts") <= rts + 1e-6), None)
                if g is not None:
                    why = " - merged move during speed change (A31): VeloxMoveGap ts=%s dt=%s before the stray's ts=%s" % (g.get("ts"), g.get("dt"), r.get("ts"))
                    break
        fails.append("further non-echo correction in [t_corr+0.05, t_corr+0.6]: %s%s" % (fmt(late), why))

if script == "StrafeRun":
    pk = num(f, "peak2d"); pt = num(f, "peak_t", -1.0)
    detail.append("peak=%.0f (%.2fx)" % (pk, pk / ground_speed if ground_speed else 0))
    if ground_speed and pk < 1.4 * ground_speed:
        fails.append("peak speed %.0f < 1.4 x %.0f" % (pk, ground_speed))
    bad = [c for c in in_window(pt, pt + 0.5) if c[1] > 5.0]
    if bad:
        fails.append("correction > 5 cm within 0.5 s after the speed peak: %s" % fmt(bad))

elif script == "PadRun":
    # ADR 0002 A28(iii)/A30(iii): per pad exactly one client line src=live applied=1 and exactly one server line, with equal
    # move time stamps ts (the launch is queued at the end of the same move on both ends). src=replay applied=1 lines (a
    # replay re-entering the trigger) are allowed once per pad and replay and must carry the server's ts; applied=0 lines
    # (A19 teleport overlaps) are ignored.
    def pad_lines(lines):
        return [kv(l) for l in lines if "VeloxJumpPad: pad=" in l]
    cl, sl = pad_lines(C), pad_lines(S)
    if any("src" not in d or "ts" not in d for d in cl + sl):
        fails.append("pad launch line without ts=/src= (binary older than ADR 0002 A28(iii)?)")
    live, srv, rep_lines = {}, {}, {}
    for d in cl:
        if d.get("applied") != "1":
            continue
        if d.get("src") == "live":
            live.setdefault(d["pad"], []).append(d)
        elif d.get("src") == "replay":
            rep_lines.setdefault(d["pad"], []).append(d)
    for d in sl:
        srv.setdefault(d["pad"], []).append(d)
    detail.append("pads=%s" % sorted(live))
    if len(live) != 3:
        fails.append("expected 3 distinct pads launched live on the client, got %d: %s" % (len(live), sorted(live)))
    def gap_at(ts):
        return next((g for g in gaps if num(g, "ts") - num(g, "dt") < ts + 1e-6 and ts <= num(g, "ts") + 1e-6), None)
    cpos, spos = positions(C, "ClientMove"), positions(S, "ServerMove")
    def resend_proof(entry_ts):
        """A34(iii): the first RPC after the entry move's own RPC (the launch move's) carries the entry move, as OldMove
        (VeloxResend event=1, A32) or as its pending half. Returns (ok, the RPC line or None)."""
        after = [d for d in rpc_lines if num(d, "new") > entry_ts + 1e-6]
        if not after:
            return False, None
        first = after[0]
        carried = abs(num(first, "old", -1.0) - entry_ts) < 2e-6 or abs(num(first, "pending", -1.0) - entry_ts) < 2e-6
        return carried, first
    for pad in sorted(set(live) | set(srv)):
        lv, sv = live.get(pad, []), srv.get(pad, [])
        if len(lv) != 1:
            fails.append("pad %s: %d client src=live applied=1 lines (want 1)" % (pad, len(lv)))
        if len(sv) != 1:
            fails.append("pad %s: %d server launch lines (want 1)" % (pad, len(sv)))
        if len(lv) == 1:
            ok, line = resend_proof(num(lv[0], "ts"))
            if not ok:
                fails.append("pad %s: entry move ts=%s not carried by the launch move's RPC (A32 resend): %s" % (
                    pad, lv[0].get("ts"), "no RPC line after the entry" if line is None else " ".join("%s=%s" % kv_ for kv_ in sorted(line.items()))))
        if len(lv) == 1 and len(sv) == 1:
            cts, sts = num(lv[0], "ts"), num(sv[0], "ts")
            if abs(cts - sts) > 1e-6:
                g = gap_at(cts)
                ekey = lv[0].get("ts")
                if g is not None:
                    fails.append("pad %s: launch one move apart: lost entry move (burst loss, resend did not arrive): client ts=%s server ts=%s, VeloxMoveGap ts=%s dt=%s" % (pad, ekey, sv[0].get("ts"), g.get("ts"), g.get("dt")))
                elif ekey in cpos and ekey in spos:
                    off = math.dist(cpos[ekey], spos[ekey])
                    if off > 0.1:
                        fails.append("pad %s: launch one move apart: sub-threshold offset flipped the trigger (A31/B2): client ts=%s server ts=%s, offset %.2f cm at the entry" % (pad, ekey, sv[0].get("ts"), off))
                    else:
                        fails.append("pad %s: launch one move apart: overlap ordering regression: client ts=%s server ts=%s, offset %.3f cm, no VeloxMoveGap at the entry" % (pad, ekey, sv[0].get("ts"), off))
                else:
                    fails.append("pad %s: launch one move apart, no VeloxMoveGap at the entry: client ts=%s server ts=%s; sub-threshold offset (A31/B2) or overlap ordering regression (position logs absent: VELOX_NETTEST_EXEC='log LogNetPlayerMovement VeryVerbose' separates them)" % (pad, ekey, sv[0].get("ts")))
        if len(sv) == 1:
            per_replay = {}
            for d in rep_lines.get(pad, []):
                per_replay[d.get("t")] = per_replay.get(d.get("t"), 0) + 1
                if abs(num(d, "ts") - num(sv[0], "ts")) > 1e-6:
                    fails.append("pad %s: replayed launch at t=%s has ts=%s, server ts=%s" % (pad, d.get("t"), d.get("ts"), sv[0].get("ts")))
                    break
            if any(n > 1 for n in per_replay.values()):
                fails.append("pad %s: more than one applied launch within one replay: %s" % (pad, per_replay))
            if len(per_replay) > len(replays):
                fails.append("pad %s: %d replayed launches but only %d replays" % (pad, len(per_replay), len(replays)))
    for pad, ds in sorted(live.items()):
        for d in ds:
            t = num(d, "t")
            stray_only(t, t + 0.6, "pad %s launch" % pad)
    if final and "checks=3/3" not in final:
        fails.append("landing checkpoints: %s" % kv(final).get("checks"))

elif script == "KnockbackRun":
    knockback_checks(airborne=True)

elif script == "KnockbackGroundRun":
    knockback_checks(airborne=False)

elif script == "LatchRun":
    if len(lands) < 2:
        fails.append("expected >= 2 landing observations, got %d" % len(lands))
    if not kb:
        fails.append("no knockback event on the client")
    if len(lands) >= 2 and kb:
        tk = num(kb[0], "t")
        l1, l2 = lands[0], lands[1]
        t1, t2 = num(l1, "t"), num(l2, "t")
        mid = [c for c in in_window(tk, t1) if c[2].get("air") == "1"]
        detail.append("midair_corr=%d" % len(mid))
        if not mid:
            fails.append("no mid-air correction between the knockback and the first landing (latch-through-replay not exercised)")
        else:
            intended = max(mid, key=lambda c: c[1])
            excluded.append(intended)
            detail.append("intended=%.1f cm at +%.2fs echoes=%d" % (intended[1], intended[0] - tk, len(echoes_of[id(intended)])))
            extra = [c for c in in_window(tk, t2) if c is not intended]
            if extra:
                fails.append("non-echo corrections in [t_k, t_land2] besides the intended mid-air one: %s" % fmt(extra))
        for name, l, t, want_jump in (("land1", l1, t1, True), ("land2", l2, t2, False)):
            jumped = l.get("jumped") == "1"
            before, after = num(l, "speed_before"), num(l, "speed_after")
            detail.append("%s jumped=%d vz=%.0f spd %.0f->%.0f" % (name, jumped, num(l, "vz"), before, after))
            if jumped != want_jump:
                fails.append("%s: jumped=%s, want %s" % (name, jumped, want_jump))
            if want_jump and jumped:
                vz = num(l, "vz")
                tol = 2.0 * gravity / 60.0 if gravity else 0.0
                if not (jump_velocity - tol <= vz <= jump_velocity + 1.0):
                    fails.append("%s: Vz %.1f not in [JumpVelocity %.1f - %.1f, JumpVelocity]" % (name, vz, jump_velocity, tol))
                if before > 0 and abs(after - before) > 0.01 * before:
                    fails.append("%s: XY speed changed %.1f -> %.1f (> 1 %%)" % (name, before, after))
            if not want_jump and before > 0 and after >= before:
                notes.append("%s: no friction drop (%.1f -> %.1f)" % (name, before, after))
            stray_only(t, t + 0.3, name)

elif script in ("FireRun", "FireRunControl", "FireRunHold"):
    # Task 08 / ADR 0003 section 9, B17, B24, B25: FireRun.json against the invulnerable strafing dummy. Client lines:
    # VeloxShotFired (seq, weapon, aim_err_cm, view_delay_ms, server_est), VeloxHitConfirm (new=), ClientActivateAbilityFailed
    # (GAS, Display). Server lines: VeloxShot (seq, hit, rewind_ms, view_ms, hint, hits, snap), VeloxShotRejected,
    # VeloxProjectileExplode, SpawnDummy, VeloxLagCompRegister/VeloxLagCompRewind (velox.lagcomp.Debug 1).
    control = script == "FireRunControl"
    hold_run = script == "FireRunHold"
    # Task 08g / B36: shots the server held (velox.combat.DebugHoldShotRpc); judged separately from the FireRun rules.
    held_lines = [kv(l) for l in S if "VeloxShotRpcHeld seq=" in l]
    held = [d.get("seq") for d in held_lines]
    WALL_RE = re.compile(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]")
    def wall(line):
        m = WALL_RE.match(line)
        if not m:
            return None
        y, mo, d, h, mi, se, ms = (int(v) for v in m.groups())
        return ((d * 24 + h) * 60 + mi) * 60 + se + ms / 1000.0
    fired = {}
    for l in C:
        if "VeloxShotFired t=" in l:
            d = kv(l); d["_wall"] = wall(l); fired[d.get("seq")] = d
    possess = [wall(l) for l in C if "VeloxShotSeqReset pawn=" in l]
    t_possess = possess[-1] if possess and possess[-1] is not None else None
    srv = {}
    for l in S:
        if "VeloxShot seq=" in l and "rewind_ms=" in l:
            d = kv(l); srv[d.get("seq")] = d
    rejected = [l for l in S if "VeloxShotRejected" in l]
    act_failed = [l for l in C if "ClientActivateAbilityFailed" in l]
    confirms = sum(int(num(kv(l), "new")) for l in C if "VeloxHitConfirm t=" in l)
    dummy_line = next((l for l in S if "SpawnDummy: " in l and "invulnerable=1" in l), None)
    dummy = dummy_line.split("SpawnDummy: ", 1)[1].split()[0] if dummy_line else None
    if dummy is None:
        fails.append("no 'SpawnDummy: <pawn> ... invulnerable=1' line on the server")
    # B24 warm-up: shots within 5 s of possession (client VeloxShotSeqReset, wall clock) are reported, not judged.
    def judged(d):
        return t_possess is None or d.get("_wall") is None or d["_wall"] - t_possess >= 5.0
    if t_possess is None:
        notes.append("no VeloxShotSeqReset line: warm-up not applied")
    warm = [d for d in fired.values() if not judged(d)]
    rails = [d for d in fired.values() if "Railgun" in d.get("weapon", "") and judged(d) and d.get("seq") not in held]
    mgs = [d for d in fired.values() if "MachineGun" in d.get("weapon", "") and judged(d)]
    rails.sort(key=lambda d: num(d, "t")); mgs.sort(key=lambda d: num(d, "t"))
    rail_srv = [(d, srv.get(d.get("seq"))) for d in rails]
    hit_n = sum(1 for _, s_ in rail_srv if s_ and num(s_, "hits") >= 1 and (dummy is None or s_.get("hit") == dummy))
    stamp_n = sum(1 for _, s_ in rail_srv if s_ and s_.get("hint") == "stamp")
    rew = [num(s_, "rewind_ms") for _, s_ in rail_srv if s_]
    dif = [abs(num(s_, "rewind_ms") - num(s_, "view_ms")) for _, s_ in rail_srv if s_]
    aim = [num(d, "aim_err_cm", -1.0) for d in rails]
    delays = [num(d, "view_delay_ms") for d in rails]
    explodes = [kv(l) for l in S if "VeloxProjectileExplode seq=" in l and "RocketLauncher" in l]
    rl_ok = [e for e in explodes if num(e, "confirm_victims") >= 1 and e.get("confirm_sent") == "1"]
    hits_srv = sum(1 for s_ in srv.values() if num(s_, "hits") >= 1) + sum(1 for e in explodes if e.get("confirm_sent") == "1")
    detail.append("rails=%d hits=%d stamp=%d rewind_med=%s dmean=%s aim_max=%s delay_med=%s mg=%d rejected=%d act_failed=%d confirms=%d/%d rl=%s warmup_shots=%d" % (
        len(rails), hit_n, stamp_n, ("%.1f" % statistics.median(rew)) if rew else "-", ("%.1f" % (sum(dif) / len(dif))) if dif else "-",
        ("%.1f" % max(aim)) if aim else "-", ("%.1f" % statistics.median(delays)) if delays else "-", len(mgs), len(rejected), len(act_failed),
        confirms, hits_srv, ",".join("direct=%s/victims=%s/sent=%s" % (e.get("direct"), e.get("confirm_victims"), e.get("confirm_sent")) for e in explodes) or "none", len(warm)))
    # B36: late_ms = (last processed move stamp - FireMoveTimeStamp) on every server shot line; VeloxShotOriginSnap (any
    # weapon, err_cm against the eye at the fire stamp) must not occur for an honest client.
    lates = [(seq, num(s_, "late_ms")) for seq, s_ in srv.items() if seq not in held and s_.get("late_ms") not in (None, "na")]
    over40 = [(seq, round(v, 1)) for seq, v in lates if v > 40.0]
    detail.append("late_max=%s late_gt40=%d" % (("%.1f" % max(v for _, v in lates)) if lates else "-", len(over40)))
    if over40:
        notes.append("non-held shots with late_ms > 40 (a lost RPC resent, RTT + ~80 ms): %s" % over40[:6])
    snap_lines = [kv(l) for l in S if "VeloxShotOriginSnap seq=" in l]
    if snap_lines:
        fails.append("%d VeloxShotOriginSnap line(s), max err_cm %.1f: %s" % (len(snap_lines), max(num(d, "err_cm") for d in snap_lines),
            ["seq=%s err_cm=%s reason=%s late_ms=%s" % (d.get("seq"), d.get("err_cm"), d.get("reason"), d.get("late_ms")) for d in snap_lines[:4]]))
    if hold_run:
        # Task 08j / B39: late_ms at release = lateness at hold time (VeloxShotRpcHeld late_ms=, 0 unless the RPC was a resend,
        # RTT + ~80 ms after a lost packet) + the hold + timer/move-stream quantisation. Assert the delta = hold +- 50 ms (hold
        # = the ms= of the held line = the DebugHoldShotRpc argument) and late_pre <= RTT + 150; hit / rewind_ms are reported.
        rows = []
        for hd in held_lines:
            seq = hd.get("seq")
            s_ = srv.get(seq)
            if s_ is None:
                rows.append("seq=%s:no_server_line" % seq)
                fails.append("held rail seq=%s has no server VeloxShot line" % seq)
                continue
            late = s_.get("late_ms")
            pre = hd.get("late_ms")
            hold = num(hd, "ms", 300.0)
            if late in (None, "na") or pre in (None, "na"):
                fails.append("held rail seq=%s late_pre=%s late_final=%s (want numbers)" % (seq, pre, late))
                rows.append("seq=%s:late_pre=%s/late_final=%s" % (seq, pre, late))
                continue
            delta = num(s_, "late_ms") - num(hd, "late_ms")
            rows.append("seq=%s:late_pre=%s/late_final=%s/delta=%.1f/rewind=%s/hint=%s/hit=%s" % (seq, pre, late, delta, s_.get("rewind_ms"), s_.get("hint"), s_.get("hit")))
            if not (hold - 50.0 <= delta <= hold + 50.0):
                fails.append("held rail seq=%s late_final - late_pre = %.1f not in [%.0f, %.0f] (late_pre %s, late_final %s)" % (seq, delta, hold - 50.0, hold + 50.0, pre, late))
            if num(hd, "late_ms") > rtt * 1000.0 + 150.0:
                fails.append("held rail seq=%s late_pre %s > RTT %.0f + 150" % (seq, pre, rtt * 1000.0))
        detail.append("held=[%s]" % " ".join(rows))
        if len(held) != 3:
            fails.append("expected 3 held rail RPCs (VeloxShotRpcHeld seq=), got %d" % len(held))
    railfile = os.path.join(out_dir, "FireRun.railhits")
    if not control:
        if not hold_run:
            with open(railfile, "w") as rf:
                rf.write("%d %d\n" % (hit_n, len(rails)))
        if len(rails) < (10 - len(held)):
            fails.append("expected >= %d judged rail shots, got %d" % (10 - len(held), len(rails)))
        missing = [d.get("seq") for d, s_ in rail_srv if s_ is None]
        if missing:
            fails.append("rail shots without a server VeloxShot line (rejected or lost): seq %s" % missing)
        bad_aim = [(d.get("seq"), num(d, "aim_err_cm", -1.0)) for d in rails if not (0.0 <= num(d, "aim_err_cm", -1.0) <= 30.0)]
        if bad_aim:
            fails.append("script aim: rail shots with aim_err_cm outside [0, 30]: %s" % bad_aim)
        if rails and hit_n < len(rails) - 1:
            fails.append("rail hits on the dummy %d of %d (want >= n - 1)" % (hit_n, len(rails)))
        if rails and stamp_n < len(rails) - 1:
            fails.append("hint=stamp on %d of %d rail shots (want >= n - 1): %s" % (stamp_n, len(rails), [s_.get("hint") for _, s_ in rail_srv if s_]))
        # Task 08g / B36: a rail whose RPC was resent is processed late_ms later, so its needed rewind grows by late_ms (and
        # is clamped at MaxRewindSeconds, accepted by B36): the upper bound is 250 + max(0, late_ms).
        out_rew = [(s_.get("seq"), num(s_, "rewind_ms"), s_.get("late_ms")) for _, s_ in rail_srv
                   if s_ and not (60.0 <= num(s_, "rewind_ms") <= 250.0 + max(0.0, num(s_, "late_ms")))]
        if out_rew:
            fails.append("rail rewind_ms outside [60, 250 + late_ms] (seq, rewind, late): %s" % out_rew)
        big = [(s_.get("seq"), round(num(s_, "rewind_ms") - num(s_, "view_ms"), 1)) for _, s_ in rail_srv if s_ and abs(num(s_, "rewind_ms") - num(s_, "view_ms")) > 60.0]
        if big:
            fails.append("|rewind_ms - view_ms| > 60 ms: %s" % big)
        if dif and sum(dif) / len(dif) > 30.0:
            fails.append("mean |rewind_ms - view_ms| %.1f > 30 ms" % (sum(dif) / len(dif)))
        snaps = [s_.get("seq") for _, s_ in rail_srv if s_ and s_.get("snap") != "0"]
        if snaps:
            fails.append("origin snapped (snap=1) on rail shots %s" % snaps)
        if len(mgs) < 40:
            fails.append("expected >= 40 judged machine-gun shots, got %d" % len(mgs))
        if rejected:
            fails.append("%d VeloxShotRejected line(s) on the server, first: %s" % (len(rejected), rejected[0].split("VeloxShotRejected", 1)[1].strip()[:140]))
        if act_failed:
            fails.append("%d ClientActivateAbilityFailed line(s) on the client (B18 switch tolerance), first: %s" % (len(act_failed), act_failed[0].strip()[-140:]))
        if not rl_ok:
            fails.append("no rocket explosion with confirm_victims >= 1 and confirm_sent=1: %s" % (explodes or "no VeloxProjectileExplode line"))
        # B24 hit-confirm accounting (ClientConfirmHit is unreliable). Strict "server hits - 1" for the rail phase and the
        # rocket, where the confirms are seconds apart and attributable by time; over the whole run (40 MG confirms at
        # 10 Hz included) one loss plus 5 % (P(>= 2 losses of 50 at 2 %) is ~26 %, so "- 1" would fail every fourth run).
        conf_t = [(num(kv(l), "t"), int(num(kv(l), "new"))) for l in C if "VeloxHitConfirm t=" in l]
        hitscan_t = sorted(num(d, "t") for d in fired.values())
        if rails and mgs:
            r0, m0 = num(rails[0], "t"), num(mgs[0], "t")
            rail_conf = sum(n for t, n in conf_t if r0 <= t < m0)
            rail_hits = sum(1 for d, s_ in rail_srv if s_ and num(s_, "hits") >= 1 and num(d, "t") < m0)
            detail.append("rail_confirms=%d/%d" % (rail_conf, rail_hits))
            if rail_conf < rail_hits - 1:
                fails.append("rail phase: client VeloxHitConfirm %d < server rail hits %d - 1" % (rail_conf, rail_hits))
        if rl_ok and hitscan_t:
            rl_conf = sum(n for t, n in conf_t if t >= hitscan_t[-1] + 1.0)
            detail.append("rl_confirms=%d" % rl_conf)
            if rl_conf < 1:
                notes.append("rocket confirm sent by the server but not received (one unreliable loss)")
        if confirms < hits_srv - (1 + int(0.05 * hits_srv)):
            fails.append("client VeloxHitConfirm %d < server confirmed hits %d - (1 + 5 %%)" % (confirms, hits_srv))
        if dummy:
            regs = [l for l in S if "VeloxLagCompRegister pawn=%s " % dummy in l]
            if len(regs) != 1:
                fails.append("VeloxLagCompRegister pawn=%s logged %d times (want 1)" % (dummy, len(regs)))
            cur = [l for l in S if "VeloxLagCompRewind" in l and "pawn=%s " % dummy in l and "mode=current" in l]
            if cur:
                fails.append("%d VeloxLagCompRewind mode=current for the dummy (no history at fire time), first: %s" % (len(cur), cur[0].split("VeloxLagCompRewind", 1)[1].strip()[:140]))
    else:
        # Control round (velox.combat.LagComp 0): the one hard rule is strictly fewer hits than the compensated FireRun of
        # this round (a +-800 cm/s dummy moves ~1.6 m in the ~200 ms rewind, beyond the 37.5 cm radius).
        if os.path.exists(railfile):
            with open(railfile) as rf:
                comp_hits, comp_n = (int(v) for v in rf.read().split())
            detail.append("compensated=%d/%d" % (comp_hits, comp_n))
            if not hit_n < comp_hits:
                fails.append("control round hits %d not below the compensated round's %d: lag compensation not acting" % (hit_n, comp_hits))
        else:
            notes.append("no FireRun result in this round: control hits reported only")

elif script in ("RocketJumpRun", "RocketJumpRunHold"):
    # Task 08 / ADR 0003 B25: velox.combat.RocketJumpTest -90 (fire RL at the floor and jump in the same frame).
    fire_line = next((kv(l) for l in C if "VeloxRocketJump: fire+jump t=" in l), None)
    if fire_line is None:
        fails.append("no 'VeloxRocketJump: fire+jump' line on the client")
    else:
        tf = num(fire_line, "t")
        w = stray_only(tf, tf + 0.6, "rocket jump")
        detail.append("apex=%s ratio=%s self_damage=%s window_corr=%s" % (f.get("apex_cm"), f.get("ratio"), f.get("self_damage"), fmt(w)))
    def splash(lines, role):
        return [kv(l) for l in lines if "VeloxSelfSplash role=%s " % role in l]
    ss, cs = splash(S, "server"), splash(C, "client")
    if len(ss) != 1 or len(cs) != 1:
        fails.append("expected one server and one client VeloxSelfSplash line, got %d / %d" % (len(ss), len(cs)))
    else:
        a, b = ss[0], cs[0]
        detail.append("splash seq=%s ts=%s/%s points=%s/%s dist=%s/%s" % (a.get("seq"), a.get("ts"), b.get("ts"), a.get("points"), b.get("points"), a.get("dist"), b.get("dist")))
        if a.get("seq") != b.get("seq"):
            fails.append("VeloxSelfSplash seq server %s != client %s" % (a.get("seq"), b.get("seq")))
        if abs(num(a, "ts") - num(b, "ts")) > 1e-6:
            fails.append("VeloxSelfSplash ts server %s != client %s (the splash ran in different moves)" % (a.get("ts"), b.get("ts")))
        if a.get("hit") != "1" or b.get("hit") != "1":
            fails.append("VeloxSelfSplash hit server %s client %s (want 1)" % (a.get("hit"), b.get("hit")))
        if a.get("points") != b.get("points"):
            fails.append("VeloxSelfSplash points server %s != client %s" % (a.get("points"), b.get("points")))
        if abs(num(a, "dist") - num(b, "dist")) > 1.0:
            fails.append("VeloxSelfSplash dist server %s client %s differ by > 1 cm" % (a.get("dist"), b.get("dist")))
    if any("VeloxSelfSplash" in l and "forced by the deadline" in l for l in S):
        fails.append("VeloxSelfSplash forced by the deadline (no owner move reached the event)")
    S_held = [kv(l) for l in S if "VeloxShotRpcHeld seq=" in l]
    shots = [kv(l) for l in S if "VeloxProjectileShot seq=" in l and "client_ts=" in l]
    if not shots:
        fails.append("no VeloxProjectileShot line on the server")
    for d in shots:
        # Task 08f / ADR 0003 B35: the rocket's self splash comes from the move stream (source=move, late_ms reported); the
        # |client_ts - last_ts| rule only applies to a fire RPC that scheduled it itself (source=rpc, review defect 7).
        src = d.get("source", "?")
        detail.append("source=%s late_ms=%s" % (src, d.get("late_ms", "-")))
        if src != "move":
            fails.append("VeloxProjectileShot seq=%s source=%s (want move: the fire move did not carry the shot)" % (d.get("seq"), src))
        if "late_ms" not in d:
            fails.append("VeloxProjectileShot seq=%s has no late_ms" % d.get("seq"))
        if src == "rpc":
            gap = abs(num(d, "client_ts") - num(d, "last_ts"))
            if gap > 1.0 / 60.0 + rtt:
                fails.append("VeloxProjectileShot seq=%s |client_ts - last_ts| %.4f > 1/60 + RTT %.3f" % (d.get("seq"), gap, rtt))
        if script == "RocketJumpRunHold":
            hd = next((h for h in S_held if h.get("seq") == d.get("seq")), None)
            if hd is None or hd.get("late_ms") in (None, "na") or "late_ms" not in d:
                fails.append("RocketJumpRunHold: seq=%s has no held-line late_ms / shot late_ms (hold not applied?)" % d.get("seq"))
            else:
                pre, fin, hold = num(hd, "late_ms"), num(d, "late_ms"), num(hd, "ms", 300.0)
                detail.append("late_pre=%.1f late_final=%.1f delta=%.1f" % (pre, fin, fin - pre))
                if not (hold - 50.0 <= fin - pre <= hold + 50.0):
                    fails.append("RocketJumpRunHold: late_final - late_pre = %.1f not in [%.0f, %.0f]" % (fin - pre, hold - 50.0, hold + 50.0))
                if pre > rtt * 1000.0 + 150.0:
                    fails.append("RocketJumpRunHold: late_pre %.1f > RTT %.0f + 150" % (pre, rtt * 1000.0))
    if script == "RocketJumpRunHold" and not any("VeloxShotRpcHeld" in l for l in S):
        fails.append("RocketJumpRunHold: no VeloxShotRpcHeld line on the server (velox.combat.DebugHoldShotRpc did not act)")
    for tag in ("VeloxShotLate", "VeloxMoveShotOrphan", "VeloxShotOriginSnap", "VeloxMoveShotMismatch"):
        hits = [l for l in S if tag + " " in l]
        if hits:
            fails.append("%d %s line(s): %s" % (len(hits), tag, hits[0].split(tag, 1)[1].strip()[:140]))
    ignored = [l for l in S if "VeloxMoveShotIgnored" in l]
    if ignored:
        notes.append("%d VeloxMoveShotIgnored: %s" % (len(ignored), ignored[0].split("VeloxMoveShotIgnored", 1)[1].strip()[:100]))
    moveshots = [kv(l) for l in S if "VeloxMoveShot seq=" in l]
    if not moveshots:
        fails.append("no VeloxMoveShot line on the server (the fire move did not carry the shot)")
    clamps = [l for l in S if "VeloxShotTimeClamp" in l]
    if clamps:
        fails.append("%d VeloxShotTimeClamp line(s): %s" % (len(clamps), clamps[0].split("VeloxShotTimeClamp", 1)[1].strip()[:140]))
    resets = [l for l in S if "VeloxShotTimeReset" in l]
    if resets:
        notes.append("%d VeloxShotTimeReset (informational)" % len(resets))
    seqs = {d.get("seq") for d in shots}
    ex = [kv(l) for l in S if "VeloxProjectileExplode seq=" in l and kv(l).get("seq") in seqs]
    if not ex:
        fails.append("no VeloxProjectileExplode line for the rocket")
    elif not any(e.get("self") == "move_timed" for e in ex):
        fails.append("VeloxProjectileExplode self=%s (want move_timed)" % [e.get("self") for e in ex])

elif script == "PlasmaRun":
    # Task 08i / ADR 0003 B38: a projectile sequence is scheduled on the server exactly once. Per server
    # VeloxProjectileShot sequence (plasma gun): exactly one shot line with source=move and exactly one VeloxMoveShot; at
    # most one server VeloxSelfSplash. No late, orphan, pending-timeout, rpc_done or rejected shot.
    def is_pg(d):
        return "Plasma" in d.get("weapon", "")
    def by_seq(lines, marker, pg_only=True):
        out = {}
        for l in lines:
            if marker in l:
                d = kv(l)
                if pg_only and not is_pg(d):
                    continue
                out.setdefault(d.get("seq"), []).append(d)
        return out
    pshots = by_seq([l for l in S if "client_ts=" in l], "VeloxProjectileShot seq=")
    mshots = by_seq(S, "VeloxMoveShot seq=")
    ssplash = by_seq(S, "VeloxSelfSplash role=server ", pg_only=False)
    csplash = by_seq(C, "VeloxSelfSplash role=client ", pg_only=False)
    fired = [kv(l) for l in C if "VeloxProjectileFired seq=" in l and is_pg(kv(l))]
    nshots = len(pshots)
    if nshots < 30:
        fails.append("only %d plasma VeloxProjectileShot sequences on the server (want >= 30 from a 5 s hold)" % nshots)
    if len(fired) != nshots:
        fails.append("client fired %d plasma shots, server launched %d sequences" % (len(fired), nshots))
    bad_shot, bad_src, bad_move, multi_splash = [], [], [], []
    for seq, ds in sorted(pshots.items(), key=lambda kvp: int(kvp[0] or 0)):
        if len(ds) != 1:
            bad_shot.append((seq, len(ds)))
        if any(d.get("source") != "move" for d in ds):
            bad_src.append((seq, [d.get("source") for d in ds]))
        if len(mshots.get(seq, [])) != 1:
            bad_move.append((seq, len(mshots.get(seq, []))))
        if len(ssplash.get(seq, [])) > 1:
            multi_splash.append((seq, len(ssplash[seq])))
    if bad_shot:
        fails.append("sequences with more than one VeloxProjectileShot: %s" % bad_shot[:8])
    if bad_src:
        fails.append("sequences not source=move: %s" % bad_src[:8])
    if bad_move:
        fails.append("sequences without exactly one VeloxMoveShot: %s" % bad_move[:8])
    if multi_splash:
        fails.append("sequences with more than one server VeloxSelfSplash (double schedule): %s" % multi_splash[:8])
    one_splash = sum(1 for seq in pshots if len(ssplash.get(seq, [])) == 1)
    splash_hit = sum(1 for seq in pshots if any(d.get("hit") == "1" for d in ssplash.get(seq, [])))
    # Report-only: the predicted and the server self splash of a sequence that hit the owner ran at the same move time
    # stamp (a hit=0 evaluation queues no knockback, so its move is no event move and may merge: its stamp may differ).
    ts_mismatch = sum(1 for seq in pshots if len(ssplash.get(seq, [])) == 1 and len(csplash.get(seq, [])) == 1
                      and "1" in (ssplash[seq][0].get("hit"), csplash[seq][0].get("hit"))
                      and abs(num(ssplash[seq][0], "ts") - num(csplash[seq][0], "ts")) > 1e-6)
    counts = {}
    for tag, marker in (("late", "VeloxShotLate "), ("orphan", "VeloxMoveShotOrphan "), ("pending_timeout", "VeloxProjectileShotPendingTimeout "),
                        ("rpc_done", "reason=rpc_done"), ("rejected", "VeloxShotRejected "), ("forced", "forced by the deadline")):
        hits = [l for l in S if marker in l]
        counts[tag] = len(hits)
        if hits:
            fails.append("%d %s line(s): %s" % (len(hits), marker.strip(), hits[0].strip()[-160:]))
    ignored = [l for l in S if "VeloxMoveShotIgnored" in l and "reason=rpc_done" not in l]
    if ignored:
        notes.append("%d VeloxMoveShotIgnored: %s" % (len(ignored), ignored[0].split("VeloxMoveShotIgnored", 1)[1].strip()[:100]))
    # The dual-move split (120 Hz client at 60 Hz send): the fire move is a single client frame (move_dt ~ 8.3 ms) and the
    # RPC normally precedes it (rpc_first=1, the pending path).
    ms_all = [d[0] for d in mshots.values() if d]
    rpc_first = sum(1 for d in ms_all if d.get("rpc_first") == "1")
    short_moves = sum(1 for d in ms_all if 0.0 < num(d, "move_dt") < 0.0125)
    moves_per_rpc = (moves / len(rpc_lines)) if rpc_lines else 0.0
    detail.append("pg_seqs=%d fired=%d moveshots=%d source_move=%d splash_1=%d splash_hit=%d splash_ts_mismatch=%d rpc_first=%d move_dt_8ms=%d moves_per_rpc=%.2f "
                  "late=%d orphan=%d pending_timeout=%d rpc_done=%d rejected=%d" % (
                      nshots, len(fired), len(ms_all), nshots - len(bad_src), one_splash, splash_hit, ts_mismatch, rpc_first, short_moves, moves_per_rpc,
                      counts["late"], counts["orphan"], counts["pending_timeout"], counts["rpc_done"], counts["rejected"]))

elif script == "HasteRun":
    # M3 task 03 / ADR 0004 sections 3 and 10: HasteRun.json on L_MovementTest. Client: VeloxNetSoak event=haste t= (the
    # row that asks the server for 2.5 s of Haste), VeloxScript row lines (vel= at row start), VeloxHasteState role=client
    # (live moves: active flips with ts, speed2d at the move start, peak2d), VeloxPowerupMoveTimes recv= (end times taken
    # from a response). Server: VeloxNetSoak event=haste_server haste_end=, VeloxHasteState role=server.
    # vel is FVector::ToCompactString: V(0) or V(X=.., Y=.., Z=..) with zero components left out.
    ROW_RE = re.compile(r"VeloxScript: st=(\S+) row=(\d+) '([^']*)' pos=.* vel=(V\([^)]*\))")
    def row_speed(label):
        for l in C:
            m = ROW_RE.search(l)
            if m and m.group(3) == label:
                comp = dict((a, float(b)) for a, b in re.findall(r"([XYZ])=(-?[0-9.]+)", m.group(4)))
                return math.hypot(comp.get("X", 0.0), comp.get("Y", 0.0))
        return None
    def states(lines, role):
        return [kv(l) for l in lines if "VeloxHasteState role=%s " % role in l]
    hc = next((kv(l) for l in C if "VeloxNetSoak event=haste " in l), None)
    hs = next((kv(l) for l in S if "VeloxNetSoak event=haste_server " in l), None)
    cs, ss = states(C, "client"), states(S, "server")
    c_on = [d for d in cs if d.get("active") == "1"]; c_off = [d for d in cs if d.get("active") == "0"]
    s_on = [d for d in ss if d.get("active") == "1"]; s_off = [d for d in ss if d.get("active") == "0"]
    if hc is None:
        fails.append("no client 'VeloxNetSoak event=haste' line")
    if hs is None:
        fails.append("no server 'VeloxNetSoak event=haste_server' line (RPC rejected?)")
    for name, on, off in (("client", c_on, c_off), ("server", s_on, s_off)):
        if len(on) != 1 or len(off) != 1:
            fails.append("%s: want exactly one VeloxHasteState active=1 and one active=0 line, got %d / %d" % (name, len(on), len(off)))
    base, hasted, after = row_speed("haste"), row_speed("strafe hop"), row_speed("after expiry")
    detail.append("client speed base=%s hasted=%s after=%s" % tuple(("%.1f" % v) if v is not None else "-" for v in (base, hasted, after)))
    TOL = 0.995   # steady-state convergence of the ground speed (Q3 accel/friction substeps)
    scale = 1.3
    if None in (base, hasted, after):
        fails.append("missing VeloxScript row lines (haste / strafe hop / after expiry)")
    else:
        if hasted < scale * base * TOL:
            fails.append("client hasted ground speed %.1f < 1.3 x baseline %.1f" % (hasted, base))
        if after > 1.01 * base or after < 0.99 * base:
            fails.append("client speed 1 s after the expiry %.1f not back at the baseline %.1f (+- 1 %%)" % (after, base))
    if len(s_on) == 1 and len(s_off) == 1:
        sb, sh = num(s_on[0], "speed2d"), num(s_off[0], "speed2d")
        detail.append("server speed base=%.1f at_expiry=%.1f peak=%s" % (sb, sh, s_off[0].get("peak2d")))
        if sh < scale * sb * TOL:
            fails.append("server hasted ground speed at the expiry %.1f < 1.3 x its speed at the pickup move %.1f" % (sh, sb))
    if len(c_off) == 1 and base is not None:
        ch = num(c_off[0], "speed2d")
        if ch < scale * base * TOL:
            fails.append("client hasted ground speed at the expiry %.1f < 1.3 x baseline %.1f" % (ch, base))
    # The end time the server set is the one the client received (full float precision through acks and corrections).
    recv = [kv(l) for l in C if "VeloxPowerupMoveTimes recv=" in l]
    got = [d for d in recv if num(d, "haste_end") != 0.0]
    if hs is not None:
        if not got:
            fails.append("client never received a non-zero haste_end (VeloxPowerupMoveTimes recv=)")
        elif got[0].get("haste_end") != hs.get("haste_end"):
            fails.append("client received haste_end=%s, server set %s" % (got[0].get("haste_end"), hs.get("haste_end")))
        else:
            detail.append("haste_end=%s via %s after %.0f ms" % (hs.get("haste_end"), got[0].get("recv"), (num(got[0], "t") - num(hc, "t")) * 1000.0 if hc else -1.0))
    # Expiry: exact in the move timeline - the first non-hasted move starts at the same boundary on both ends. The client
    # logs its live move, which it may combine with the next one before sending (the server then simulates the combined
    # move), so the start stamps ts - dt are compared, not the end stamps.
    if len(c_off) == 1 and len(s_off) == 1:
        cts = num(c_off[0], "ts") - num(c_off[0], "dt")
        sts = num(s_off[0], "ts") - num(s_off[0], "dt")
        detail.append("expiry_boundary client=%.6f server=%.6f" % (cts, sts))
        if abs(cts - sts) > 1e-5:
            g = next((g for g in gaps if num(g, "ts") - num(g, "dt") < cts + 1e-6 and cts <= num(g, "ts") + 1e-6), None)
            if g is not None:
                notes.append("expiry move merged on the server (VeloxMoveGap ts=%s dt=%s): client ts=%s server ts=%s" % (g.get("ts"), g.get("dt"), c_off[0].get("ts"), s_off[0].get("ts")))
            else:
                fails.append("expiry at different move boundaries: client ts=%s dt=%s server ts=%s dt=%s" % (c_off[0].get("ts"), c_off[0].get("dt"), s_off[0].get("ts"), s_off[0].get("dt")))
    # Windows (client world time). t_pickup = the server's grant in client time: the request row + one-way lag.
    if hc is not None:
        th = num(hc, "t")
        t_pick_end = th + rtt / 2.0 + 0.3
        w = in_window(th, t_pick_end)
        detail.append("pickup_window=[%.2f,%.2f] corr=%s" % (th, t_pick_end, fmt(w)))
        if len(w) > 1 or any(c[1] > 30.0 for c in w):
            fails.append("pickup: non-echo corrections in [t_h, t_h + RTT/2 + 0.3]: %s (allowed: at most one <= 30 cm)" % fmt(w))
        excluded.extend(w)
    if len(c_off) == 1:
        te = num(c_off[0], "t")
        around = [c for c in corr if te - 1.0 <= c[0] <= te + 1.0]
        detail.append("expiry t=%.2f corr_2s=%d" % (te, len(around)))
        if around:
            fails.append("corrections in the 2 s around the expiry (want 0): %s" % fmt(around))

elif script == "ItemRun":
    # M3 task 04 / ADR 0004 section 10: the client runs over Health.Small, Armor.Shard and Ammo.Bullets (ItemRun.json, L_ItemTest).
    # Client lines: VeloxPickupPredict item= t= (the predicted take), VeloxPickupPredict{Confirmed,Lost,Refused} item= ms=,
    # VeloxItemWatch placeholder=hidden|shown item= predicted= (velox.items.Watch, once per frame) and VeloxItemWatch attr=
    # Health|Armor|Bullets value= t=. Server: VeloxPickup item= by= gain=. The confirmation and the attribute step are
    # replicated by the server, so they cannot arrive before the round trip; the ADR's 150 ms / 200 ms are measured on top of
    # it: threshold = RTT (2 x one-way lag + 40 ms jitter) + 150 ms / + 200 ms from the predicted take.
    FRAME_RE = re.compile(r"^\[[^\]]*\]\[\s*(\d+)\]")
    def frame_of(line):
        m = FRAME_RE.match(line)
        return int(m.group(1)) if m else None
    want = {"DA_Item_Health_Small": "Health", "DA_Item_Armor_Shard": "Armor", "DA_Item_Ammo_Bullets": "Bullets"}
    predicts, outcomes, hidden, attrs = {}, {}, {}, []
    for l in C:
        if "VeloxPickupPredict item=" in l:
            d = kv(l); predicts.setdefault(d.get("item"), (num(d, "t"), frame_of(l)))
        m = re.search(r"VeloxPickupPredict(Confirmed|Lost|Refused) item=(\S+) ms=(\S+)", l)
        if m:
            outcomes.setdefault(m.group(2), []).append((m.group(1), float(m.group(3))))
        if "VeloxItemWatch placeholder=hidden" in l:
            d = kv(l); hidden.setdefault(d.get("item"), []).append((num(d, "t"), frame_of(l), d.get("predicted")))
        if "VeloxItemWatch attr=" in l:
            d = kv(l); attrs.append((num(d, "t"), d.get("attr"), num(d, "value")))
    granted = {}
    for l in S:
        if "VeloxPickup item=" in l and " gain=" in l:
            d = kv(l)
            if num(d, "gain") > 0:
                granted.setdefault(d.get("item"), []).append(d)
    if not any("VeloxItemWatch started" in l for l in C):
        fails.append("velox.items.Watch did not start on the client")
    conf_limit_ms = rtt * 1000.0 + 150.0
    attr_limit_s = rtt + 0.200
    # With 2 % loss each way a server -> client bunch is lost now and then and its properties are retransmitted one RTT
    # later; that is the network, not the item code. One such slow event per run (at most RTT + 60 ms over its limit) is
    # reported as a note, a second one or a larger overshoot fails.
    retx_ms = rtt * 1000.0 + 60.0
    slow = []
    for item, attr in sorted(want.items()):
        short = item.split("_")[-1]
        if item not in predicts:
            fails.append("%s: no VeloxPickupPredict line on the client (not predicted)" % short)
            continue
        pt, pf = predicts[item]
        # predicted hide within one frame: the placeholder is hidden in the same call as the predict log line; the observer
        # (engine ticker, start of the next frame) sees it at most one frame later.
        hs = [h for h in hidden.get(item, []) if h[0] >= pt - 0.001]
        if not hs:
            fails.append("%s: placeholder never observed hidden after the predicted take" % short)
            hide_frames = None
        else:
            hide_frames = ((hs[0][1] - pf) % 1000) if (hs[0][1] is not None and pf is not None) else None
            if hide_frames is None or hide_frames > 2 or hs[0][2] != "1":
                fails.append("%s: hidden %s frame(s) after the predict (want <= 1 observer frame, predicted=1; got predicted=%s)" % (short, hide_frames, hs[0][2]))
        res = outcomes.get(item, [])
        confirmed = [ms for kind, ms in res if kind == "Confirmed"]
        bad = [kind for kind, ms in res if kind != "Confirmed"]
        if bad:
            fails.append("%s: prediction outcome %s (want Confirmed)" % (short, bad))
        conf_ms = confirmed[0] if confirmed else None
        if conf_ms is None:
            fails.append("%s: no VeloxPickupPredictConfirmed line" % short)
        elif conf_ms > conf_limit_ms:
            msg = "%s: confirmation after %.0f ms > RTT %.0f + 150 = %.0f ms" % (short, conf_ms, rtt * 1000.0, conf_limit_ms)
            (slow if conf_ms <= conf_limit_ms + retx_ms else fails).append(msg)
        steps = [a for a in attrs if a[1] == attr and a[0] > pt]
        # the first change of this attribute after the predict that is an increase over the value before it
        before = [a for a in attrs if a[1] == attr and a[0] <= pt]
        prev = before[-1][2] if before else None
        step = None
        for a in steps:
            if prev is None or a[2] > prev:
                step = a; break
            prev = a[2]
        if step is None:
            fails.append("%s: no %s increase observed on the client after the take" % (short, attr))
            step_s = None
        else:
            step_s = step[0] - pt
            if step_s > attr_limit_s:
                msg = "%s: %s step after %.0f ms > RTT %.0f + 200 = %.0f ms" % (short, attr, step_s * 1000.0, rtt * 1000.0, attr_limit_s * 1000.0)
                (slow if step_s <= attr_limit_s + retx_ms / 1000.0 else fails).append(msg)
        if len(granted.get(item, [])) != 1:
            fails.append("%s: %d server grants (VeloxPickup item= gain>0), want 1" % (short, len(granted.get(item, []))))
        detail.append("%s:hide=%sf,conf=%s,%s=%s" % (short, hide_frames, ("%.0fms" % conf_ms) if conf_ms is not None else "-", attr, ("%.0fms" % (step_s * 1000.0)) if step_s is not None else "-"))
    if len(slow) > 1:
        fails.extend(slow)
        fails.append("more than one slow event (%d): one retransmitted bunch is tolerated, two are not" % len(slow))
    elif slow:
        notes.append("loss recovery tolerated: " + slow[0])
    other = [i for i in predicts if i not in want]
    if other:
        notes.append("also predicted: %s" % ",".join(sorted(i.split("DA_Item_")[-1] for i in other)))
    lost = [l for l in C if re.search(r"VeloxPickupPredict(Lost|Refused)", l)]
    if lost:
        fails.append("%d prediction(s) lost or refused: %s" % (len(lost), lost[0].split("VeloxPickupPredict", 1)[1].strip()[:100]))
    # 0 corrections: the item grants do not touch movement, so the whole run has none (echoes included).
    if corr:
        fails.append("%d client correction(s) during ItemRun (want 0): %s" % (len(corr), fmt(corr)))

if script in ("BotRun", "BotDuel"):
    # M4 task 07 / ADR 0005 section 14 and D36: the bot soak. Everything is judged from the SERVER log (velox.bots.Debug 1
    # writes one VeloxBot line per bot per second); the client log only supplies the report-only corrections and the
    # velox.bots.Watch lines. The per-run numbers go to <script>.stats.json for the set evaluation after the last round
    # (D36: sums and per-round caps over the repeat set). Port of scratchpad/post04f/summary.py with the D36 bands.
    import collections, datetime, json
    duel = script == "BotDuel"
    duration = 120.0 if duel else 90.0
    TSR = re.compile(r"^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d):(\d{3})\]")
    POSR = re.compile(r"VeloxBot name=(\S+) (.*?) pos=V\(X=([-\d.]+), Y=([-\d.]+), Z=([-\d.]+)\)")
    ATR = re.compile(r"at=V\(X=([-\d.]+), Y=([-\d.]+), Z=([-\d.]+)\)")
    MAJOR = re.compile(r"item=DA_Item_(Health_Mega|Armor_Red|Armor_Yellow|Powerup_[A-Za-z]+)")
    MAJOR_ALL = ["Health_Mega", "Armor_Red", "Armor_Yellow", "Powerup_BattleSuit", "Powerup_Haste", "Powerup_Quad", "Powerup_Regen"]
    DWELL_RADIUS, DWELL_MAX, LINK_DT_MAX, BUDGET_MAX = 150.0, 8, 8.0, 300.0
    STUCK_MAX, PICKUPS_MIN = 6, 15

    def stamp(line):
        m = TSR.match(line)
        if not m:
            return None
        y_, mo, d_, h, mi, s_, ms = (int(v) for v in m.groups())
        return datetime.datetime(y_, mo, d_, h, mi, s_, ms * 1000).timestamp()

    bots = set()
    for l in S:
        m = re.search(r"VeloxBot name=(\S+) ", l)
        if m:
            bots.add(m.group(1))
    nav_built = any("VeloxNav built" in l for l in S)
    phases = [re.search(r"VeloxMatch phase=(\w+)", l).group(1) for l in S if "VeloxMatch phase=" in l]
    pickups = collections.Counter()
    mega = []                      # (t, by, respawn_in)
    mega_resp = []                 # t of every explicit "VeloxPickupRespawn item=DA_Item_Health_Mega t=" line (D42)
    kills = frags_total = suicides = 0
    stuck = []                     # (bot, t, x, y, rule, goal)
    rules = collections.Counter(); reasons = collections.Counter(); offmesh = collections.Counter()
    link_ok = link_total = 0
    link_max, link_max_desc = 0.0, "-"
    link_over = []
    refused = giveup = no_nav = recovered = repaths = hear = dodges = camp_lines = 0
    allun = collections.Counter()
    errors = []
    goals = collections.Counter(); vis_armed = vis_armed_fight = supp1 = debug_lines = noammo = 0
    live = False; camp = {}; dwell = {}
    max_dwell, max_dwell_at = 0, "-"
    budget_windows = []
    route_stats = []
    for l in S:
        if "Error:" in l or "Ensure condition" in l:
            errors.append(l.strip()[:160])
        if "VeloxMatch phase=Live" in l:
            live = True; dwell.clear(); continue
        if "VeloxPickupRespawn item=DA_Item_Health_Mega" in l:
            mega_resp.append(num(kv(l), "t")); continue
        if "VeloxPickup item=" in l:
            m = re.search(r"by=(\S+)", l)
            if m and m.group(1) in bots:
                im = re.search(r"item=(\S+)", l)
                pickups[im.group(1)] += 1
                if "item=DA_Item_Health_Mega" in l:
                    d = kv(l); mega.append((num(d, "t"), m.group(1), num(d, "respawn_in")))
            continue
        if "VeloxKill victim=" in l and "ignored" not in l:
            m = re.search(r"killer=(\S+) suicide=(\d)", l)
            if m and m.group(2) == "1":
                suicides += 1
            elif m:
                frags_total += 1
                if m.group(1) in bots:
                    kills += 1
            continue
        if "VeloxBotStuck bot=" in l:
            d = kv(l); at = ATR.search(l)
            x_, y_ = (float(at.group(1)), float(at.group(2))) if at else (0.0, 0.0)
            stuck.append((d.get("bot"), stamp(l), x_, y_, d.get("rule", "?"), d.get("goal", "?")))
            rules[d.get("rule", "?")] += 1
            continue
        if "VeloxBotRepath" in l:
            repaths += 1; continue
        if "VeloxBotHear " in l:
            hear += 1; continue
        if "VeloxBotDodge " in l:
            dodges += 1; continue
        if "VeloxBotOffMesh" in l:
            if "result=no_nav_point" in l: no_nav += 1
            if "result=giveup" in l: giveup += 1
            if " recovered " in l: recovered += 1
            continue
        if "VeloxBotLink bot=" in l and "result=" in l:
            d = kv(l); link_total += 1
            if d.get("result") == "ok": link_ok += 1
            else: reasons[d.get("reason", "?")] += 1
            dt = num(d, "dt")
            if dt > link_max: link_max, link_max_desc = dt, "%s:%s:%s" % (d.get("bot"), d.get("link"), d.get("reason"))
            if dt > LINK_DT_MAX: link_over.append("%s:%s:%s:%.2f" % (d.get("bot"), d.get("link"), d.get("reason"), dt))
            continue
        m = POSR.search(l)
        if m:
            name, body = m.group(1), m.group(2)
            x_, y_ = float(m.group(3)), float(m.group(4))
            gm = re.search(r"goal=(\S+)", body); g = gm.group(1).lower() if gm else "?"
            goals[g] += 1; debug_lines += 1
            if " supp=1" in l: supp1 += 1
            if "fire=noammo" in body: noammo += 1
            w = re.search(r"wstr=(-?[\d.]+)", body)
            if " vis=1" in body and w and float(w.group(1)) >= 0.5:
                vis_armed += 1
                if g == "fight": vis_armed_fight += 1
            if live:
                # D34: consecutive 1 Hz samples within 150 cm (2-D) of the run's first sample; camp=1 samples excluded.
                if camp.get(name, 0):
                    dwell.pop(name, None)
                else:
                    dd = dwell.get(name)
                    if dd and math.hypot(x_ - dd[0], y_ - dd[1]) <= DWELL_RADIUS:
                        dd[2] += 1
                    else:
                        dd = dwell[name] = [x_, y_, 1]
                    if dd[2] > max_dwell:
                        max_dwell, max_dwell_at = dd[2], "%s@(%.0f,%.0f),goal=%s" % (name, dd[0], dd[1], g)
            continue
        m = re.search(r"VeloxBotRoute bot=(\S+) (.*)", l)
        if m:
            if " refused " in l: refused += 1
            cm = re.search(r" camp=(\d)", l)
            if cm:
                camp[m.group(1)] = int(cm.group(1))
                camp_lines += int(cm.group(1))
            c = re.search(r"candidates=\[(.*)\]", l)
            if c:
                st = re.search(r"start=(\w+)", l); start = st.group(1) if st else "-"
                cands = [v for v in c.group(1).split(",") if v]
                if cands and all("!unreachable" in v for v in cands):
                    allun[start] += 1
            continue
        if "VeloxBotBudget bots=" in l:
            d = kv(l); budget_windows.append(d); continue
        if "VeloxBotRouteStats " in l:
            route_stats.append(kv(l))

    bursts = []
    per_bot = collections.defaultdict(list)
    for s_ in stuck:
        if s_[1] is not None: per_bot[s_[0]].append(s_[1])
    for b_, ts_ in per_bot.items():
        ts_.sort()
        if any(ts_[i + 2] - ts_[i] <= 10.0 for i in range(len(ts_) - 2)): bursts.append(b_)
    pocket = [s_ for s_ in stuck if abs(s_[2]) < 150.0 and abs(s_[3]) < 150.0]
    means = [num(d, "mean_us") for d in budget_windows]
    budget_mean = sum(means) / len(means) if means else 0.0
    run_mean = num(budget_windows[-1], "run_mean_us") if budget_windows else 0.0
    fire_windows = [num(d, "max_fire_us") for d in budget_windows]
    fire_over = sum(1 for v in fire_windows if v > 2000.0)
    nbots = sum(pickups.values())
    major_taken = sorted({MAJOR.search("item=" + k).group(1) for k in pickups if MAJOR.search("item=" + k)})
    # D40 contention verdict: share of ticks over the 600 us limit per VeloxBotBudget window (over= / ticks=); more than 5 %
    # in more than 2 windows of the round = CONTENDED (another process competed for the machine): budget, max_fire and
    # deferred are not judged and the round is left out of the set evaluation (rerun it).
    over_shares = [100.0 * num(d, "over") / num(d, "ticks") if num(d, "ticks") > 0 else 0.0 for d in budget_windows]
    contended_windows = sum(1 for v in over_shares if v > 5.0)
    contended = contended_windows > 2
    # Duel timing (D42): a Mega take by a bot within 3 s after the Mega became available again, taken from the explicit
    # log lines: the last "VeloxPickupRespawn ... t=" at or before the take, or the Countdown -> Live level reset
    # (every pickup is available again at the Live time).
    live_t = next((num(kv(l), "now") for l in S if "VeloxMatch phase=Live" in l), 0.0)
    mega_lags = []
    for (t_, _by, _resp) in mega:
        if t_ >= live_t:
            avail = max([live_t] + [r_ for r_ in mega_resp if live_t <= r_ <= t_ + 1e-3])
            mega_lags.append(t_ - avail)
    mega_fast = sum(1 for v in mega_lags if -0.5 <= v <= 3.0)
    total_goals = sum(goals.values())
    fight_share = 100.0 * goals.get("fight", 0) / total_goals if total_goals else 0.0
    armed_share = 100.0 * vis_armed_fight / vis_armed if vis_armed else 0.0
    supp_share = 100.0 * supp1 / debug_lines if debug_lines else 0.0

    # Client corrections are report-only (rocket hits on the human are legitimate corrections): drop the M1 convergence rules.
    if script == "BotRun":
        fails[:] = [fl for fl in fails if not fl.startswith("replay ")]
    if not nav_built: fails.append("no 'VeloxNav built' line (nav not built)")
    if not bots: fails.append("no VeloxBot debug lines (velox.bots.Debug 1 inactive or no bots)")
    if stuck and len(stuck) > STUCK_MAX: fails.append("VeloxBotStuck %d > %d" % (len(stuck), STUCK_MAX))
    if bursts: fails.append("strike burst (>= 3 strikes of one bot within 10 s): %s" % ",".join(bursts))
    if max_dwell > DWELL_MAX: fails.append("max_dwell_s %d > %d at %s" % (max_dwell, DWELL_MAX, max_dwell_at))
    if link_over: fails.append("VeloxBotLink dt > %.0f s: %s" % (LINK_DT_MAX, ",".join(link_over[:5])))
    if no_nav: fails.append("VeloxBotOffMesh result=no_nav_point x%d" % no_nav)
    if giveup: fails.append("VeloxBotOffMesh result=giveup x%d" % giveup)
    if refused: fails.append("VeloxBotRoute refused x%d" % refused)
    if allun.get("air", 0): fails.append("all-candidates-unreachable evaluations with start=air: %d (D33)" % allun["air"])
    if errors: fails.append("%d Error:/ensure line(s) in the server log: %s" % (len(errors), errors[0]))
    if budget_mean > BUDGET_MAX and not contended: fails.append("VeloxBotBudget mean_us %.1f > %.0f" % (budget_mean, BUDGET_MAX))
    if not budget_windows: fails.append("no VeloxBotBudget lines")
    if duel:
        if "Countdown" not in phases or "Live" not in phases or phases.index("Countdown") > phases.index("Live"):
            fails.append("phase did not go Countdown -> Live without a human: %s" % ">".join(phases))
        if frags_total < 1: fails.append("no frag in %.0f s" % duration)
        if mega_fast < 1: fails.append("no Mega take by a bot within 3 s of its respawn (takes=%d lags=%s)" % (len(mega), ["%.1f" % v for v in mega_lags]))
        if camp_lines < 1: fails.append("no VeloxBotRoute camp=1 line")
    else:
        if kills < 1: fails.append("no kill by a bot")
        if nbots < PICKUPS_MIN: fails.append("pickups by bots %d < %d" % (nbots, PICKUPS_MIN))
        if not mega: fails.append("no DA_Item_Health_Mega pickup by a bot")
        if link_ok < 2: fails.append("VeloxBotLink result=ok %d < 2" % link_ok)

    pos_txt = " ".join("%s@(%.0f,%.0f)" % (b_, x_, y_) for b_, _, x_, y_, _, _ in stuck)
    rs_paths = [num(d, "paths_per_s") for d in route_stats]
    detail_lines = [
        "kills_by_bots=%d frags=%d suicides=%d pickups_by_bots=%d [%s] major=[%s]" % (kills, frags_total, suicides, nbots,
            " ".join("%s=%d" % (k.replace("DA_Item_", ""), v) for k, v in sorted(pickups.items())), ",".join(major_taken)),
        "links ok=%d/%d longest_dt=%.2f [%s] failed_reasons=[%s]" % (link_ok, link_total, link_max, link_max_desc, " ".join("%s=%d" % kv_ for kv_ in sorted(reasons.items()))),
        "stuck=%d rules=[%s] bursts=%d pocket_strikes=%d at: %s" % (len(stuck), " ".join("%s=%d" % kv_ for kv_ in sorted(rules.items())), len(bursts), len(pocket), pos_txt or "-"),
        "max_dwell_s=%d [%s] repaths=%d%s offmesh recovered=%d no_nav_point=%d giveup=%d refused=%d hear=%d dodge=%d camp_route_lines=%d" % (
            max_dwell, max_dwell_at, repaths, " (FLAG > %d)" % int(duration) if repaths > duration else "", recovered, no_nav, giveup, refused, hear, dodges, camp_lines),
        "budget mean_us=%.1f run_mean_us=%.1f max_us=%.0f max_path_us=%.0f " % (
            budget_mean, run_mean, max([num(d, "max_us") for d in budget_windows] or [0]), max([num(d, "max_path_us") for d in budget_windows] or [0]))
        + ("max_fire_us=not judged (CONTENDED) deferred=not judged (CONTENDED)" if contended else
           "max_fire_us=%.0f (windows over 2000 us: %d/%d%s) paths/s=%.1f max_deferred_frames=%d" % (
            max(fire_windows or [0]), fire_over, len(fire_windows), " FLAG D14" if fire_windows and fire_over * 4 > len(fire_windows) else "",
            sum(rs_paths) / len(rs_paths) if rs_paths else 0.0, int(max([num(d, "max_deferred_frames") for d in route_stats] or [0]))))
        + " over_share_pct=[%s] windows_over_5pct=%d/%d" % (",".join("%.0f" % v for v in over_shares), contended_windows, len(over_shares)),
        "goals=[%s] fight_share=%.1f%% armed_fight_share=%.1f%% (%d/%d) supp1=%.1f%% fire_noammo=%d/%d allunreachable air=%d ground=%d" % (
            " ".join("%s=%d" % kv_ for kv_ in sorted(goals.items())), fight_share, armed_share, vis_armed_fight, vis_armed, supp_share, noammo, debug_lines,
            allun.get("air", 0), allun.get("ground", 0)),
    ]
    if duel:
        detail_lines.append("phases=%s mega_takes=%d lags_after_respawn_s=[%s] within_3s=%d" % (">".join(phases), len(mega), ",".join("%.1f" % v for v in mega_lags), mega_fast))
    else:
        cmax_ = max([c[1] for c in corr] or [0.0])
        detail_lines.append("client corrections (report-only) non-echo=%d max=%.1f cm echoes=%d" % (len(nonecho), cmax_, len(echoes)))
    watch = [l for l in C if "VeloxBotWatch" in l]
    detail_lines.append("client watch lines=%d (feed=%d phase=%d pickup=%d)" % (len(watch), sum(1 for l in watch if " feed " in l), sum(1 for l in watch if " phase=" in l and "old=" in l), sum(1 for l in watch if " pickup " in l)))
    stats = {"script": script, "pass": not fails, "stuck": len(stuck), "kills": kills, "frags": frags_total, "pickups": nbots, "major": major_taken,
             "pocket": len(pocket), "bursts": len(bursts), "max_dwell": max_dwell, "repaths": repaths, "mega_fast": mega_fast, "contended": contended}
    with open(os.path.join(out_dir, script + ".stats.json"), "w") as sf:
        json.dump(stats, sf)
    verdict = "CONTENDED" if contended else ("FAIL" if fails else "PASS")
    head = "%-18s %s kills=%d frags=%d pickups=%d stuck=%d max_dwell_s=%d link_max_dt=%.2f budget_mean_us=%.1f" % (script, verdict, kills, frags_total, nbots, len(stuck), max_dwell, link_max, budget_mean)
    with open(result_path, "w") as out:
        out.write(head + "\n")
        for fl in fails:
            out.write("    - " + fl + "\n")
        if contended:
            out.write("    ~ CONTENDED (D40): %d of %d VeloxBotBudget windows had > 5 %% of ticks over 600 us; budget, max_fire and deferred are not judged and this round is excluded from the set evaluation - rerun it on an idle machine\n" % (contended_windows, len(over_shares)))
        for dl in detail_lines:
            out.write("    . " + dl + "\n")
    sys.exit(1 if fails else 0)

# ---- global rules on non-echo corrections (the intended ones are excluded)
rest = [c for c in nonecho if c not in excluded]
xs = [c[1] for c in rest]
x = max(xs) if xs else 0.0
y = sum(xs) / len(xs) if xs else 0.0
if script in ("StrafeRun", "KnockbackRun", "KnockbackGroundRun", "FireRun", "FireRunControl", "FireRunHold", "HasteRun"):
    if len(rest) > 3 * Lexp:
        fails.append("corrections C=%d > 3 x L_exp (%.1f)" % (len(rest), 3 * Lexp))
    if x > 30.0:
        fails.append("max correction %.1f cm > 30" % x)
    if y > 5.0:
        fails.append("mean correction %.2f cm > 5" % y)

# A34(ii): with 2 % loss each way the server must see lost RPCs: repaired by the next RPC's OldMove (A32) or, for burst
# loss, merged into a longer move. RPCs are counted from the client's VeloxResend/VeloxSend lines (LogVeloxNet Verbose).
nrpc = len(rpc_lines)
if nrpc == 0:
    fails.append("no VeloxResend/VeloxSend lines on the client (binary older than ADR 0002 A32, or LogVeloxNet not Verbose)")
elif nrpc >= 500 and not gaps and not repaired:
    fails.append("loss simulation inactive: no VeloxMoveRepaired and no VeloxMoveGap on the server over %d RPCs" % nrpc)
# Before the first ack there is no LastAckedMove and nothing to resend (one RTT); count from the first resend on.
first_resend = next((i for i, d in enumerate(rpc_lines) if "old" in d), None)
nrpc_acked = nrpc - first_resend if first_resend is not None else nrpc
if nrpc and len(resends) < 0.9 * nrpc_acked:
    fails.append("resent=%d < 90 %% of the %d RPCs after the first ack: the OldMove does not ride along (A32)" % (len(resends), nrpc_acked))

# Summary over non-echo corrections (echoes counted separately, A24).
ncorr = len(nonecho)
nx = [c[1] for c in nonecho]
cmax = max(nx) if nx else 0.0
cmean = sum(nx) / len(nx) if nx else 0.0
strays_le = sum(1 for c in nonecho if c[1] <= 2.5)   # A34(v): A31-class strays at merged moves during speed changes
# Task M1-07 (A32's deferred cl_packetdup 2): RPCs that also carried an extra old move (VeloxResend extra= stamp != -1).
extras = sum(1 for d in resends if num(d, "extra", -1.0) != -1.0)
detail.append("replays=%d acks_ignored=%d rpcs=%d" % (len(replays), acks_ignored, nrpc))
row = "%-18s moves=%-5d C=%-3d echoes=%-3d gaps=%-3d repaired=%-3d repaired_extra=%-3d resent=%-5d resent_extra=%-4d strays_le_2_5=%-2d max=%-6.2f mean=%-5.2f %s  %s  %s" % (
    script, moves, ncorr, len(echoes), len(gaps), len(repaired), repaired_extra, len(resends), extras, strays_le, cmax, cmean, "FAIL" if fails else "PASS", " ".join(detail), ("; ".join(notes)))
with open(result_path, "w") as out:
    out.write(row + "\n")
    for fl in fails:
        out.write("    - " + fl + "\n")
sys.exit(1 if fails else 0)
PY
}

# bot_set_eval <script> <base_dir> <rounds> : ADR 0005 D36 set bands over the per-round <script>.stats.json files
# (round-<k>/ directories for --repeat > 1). Judged for --repeat 3 only; other counts are reported, not judged.
bot_set_eval() {
  python3 -I - "$1" "$2" "$3" <<'PY'
import json, os, sys
script, base, rounds = sys.argv[1], sys.argv[2], int(sys.argv[3])
MAJOR_ALL = ["Health_Mega", "Armor_Red", "Armor_Yellow", "Powerup_BattleSuit", "Powerup_Haste", "Powerup_Quad", "Powerup_Regen"]
runs = []
for k in range(1, rounds + 1):
    d = os.path.join(base, "round-%d" % k) if rounds > 1 else base
    try:
        runs.append(json.load(open(os.path.join(d, script + ".stats.json"))))
    except (OSError, ValueError):
        print("=== %s set evaluation: round %d has no stats (run aborted) ===" % (script, k)); sys.exit(1)
all_runs = runs
contended_rounds = [k + 1 for k, r in enumerate(all_runs) if r.get("contended")]
runs = [r for r in all_runs if not r.get("contended")]
S = lambda key: sum(r[key] for r in runs)
major = sorted({m for r in runs for m in r["major"]})
missing = [m for m in MAJOR_ALL if m not in major]
fails = []
judged = rounds == 3 and not contended_rounds
if script == "BotRun":
    if S("stuck") > 10: fails.append("sum VeloxBotStuck %d > 10" % S("stuck"))
    if S("kills") < 12: fails.append("sum kills by bots %d < 12" % S("kills"))
    if missing: fails.append("bMajorItem never taken by a bot: %s" % ",".join(missing))
else:
    if S("frags") < 9: fails.append("sum frags %d < 9" % S("frags"))
    if S("stuck") > 10: fails.append("sum VeloxBotStuck %d > 10" % S("stuck"))
print("=== %s set evaluation over %d round(s) (D36%s) ===" % (script, rounds, "" if judged else "; bands are defined for --repeat 3: reported, not judged"))
print("  per round: kills=%s frags=%s stuck=%s pickups=%s max_dwell_s=%s repaths=%s" % (
    [r["kills"] for r in runs], [r["frags"] for r in runs], [r["stuck"] for r in runs], [r["pickups"] for r in runs], [r["max_dwell"] for r in runs], [r["repaths"] for r in runs]))
print("  sums: stuck=%d kills=%d frags=%d pocket_strikes=%d%s major_covered=%d/7 [%s]%s" % (
    S("stuck"), S("kills"), S("frags"), S("pocket"), " (FLAG: pocket strike -> architect)" if S("pocket") else "", len(major) if not missing else 7 - len(missing), ",".join(major),
    " missing=[%s]" % ",".join(missing) if missing else ""))
if contended_rounds:
    print("  CONTENDED round(s) %s (D40) excluded from the sums above: set not judged, rerun %s on an idle machine" % (
        ",".join(str(k) for k in contended_rounds), "them" if len(contended_rounds) > 1 else "it"))
    sys.exit(1 if rounds == 3 else 0)
if not judged:
    sys.exit(0)
for f in fails:
    print("  - " + f)
print("  set %s" % ("FAIL" if fails else "PASS"))
sys.exit(1 if fails else 0)
PY
}

main() {
  DRY_RUN=0
  local scripts=() s
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --scripts) [[ $# -ge 2 ]] || die "--scripts needs a value"; SCRIPTS_ARG="$2"; shift 2 ;;
      --port)    [[ $# -ge 2 ]] || die "--port needs a value"; PORT="$2"; shift 2 ;;
      --timeout) [[ $# -ge 2 ]] || die "--timeout needs a value"; CLIENT_TIMEOUT="$2"; shift 2 ;;
      --lag)     [[ $# -ge 2 && "$2" =~ ^[0-9]+$ && "$2" -ge 20 ]] || die "--lag needs an integer >= 20 (ms)"; LAG_OVERRIDE="$2"; shift 2 ;;
      --repeat)  [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || die "--repeat needs a positive integer"; REPEAT="$2"; shift 2 ;;
      --keep-going) KEEP_GOING=1; shift ;;
      -h|--help) nettest_usage; exit 0 ;;
      *) nettest_usage >&2; die "unknown option: $1" ;;
    esac
  done
  IFS=',' read -r -a scripts <<< "$SCRIPTS_ARG"
  cd "$REPO_ROOT" || exit 1
  load_env_file
  ci_resolve_engine
  EDITOR_CMD="$(find_editor_cmd)" || die "UnrealEditor-Cmd not found under $UE_ROOT/Engine/Binaries/Linux"
  VELOX_OUT="${VELOX_OUT:-$REPO_ROOT/Saved/CI}"
  local base_dir
  if [[ -n "${VELOX_CI_LOG_DIR:-}" ]]; then base_dir="$VELOX_CI_LOG_DIR/nettest"; else base_dir="$VELOX_OUT/$(date +%Y%m%d-%H%M%S)-$$/nettest"; fi
  mkdir -p "$base_dir"
  local lag_desc
  if [[ -n "$LAG_OVERRIDE" ]]; then lag_desc="${LAG_OVERRIDE} ms (all scripts)"; else lag_desc="${LAG_MS} ms (FireRun/FireRunControl ${FIRE_LAG_MS} ms)"; fi
  info "Net soak: scripts=${scripts[*]} rounds=$REPEAT port=$PORT lag=${lag_desc} logs=$base_dir"
  for s in "${scripts[@]}"; do
    case "$s" in
      FireRunControl|FireRunHold) [[ -f "$REPO_ROOT/Content/Data/MoveScripts/FireRun.json" ]] || die "$s needs FireRun.json" ;;
      RocketJumpRun|RocketJumpRunHold) ;;   # drive velox.combat.RocketJumpTest, no JSON script
      BotRun) [[ -f "$REPO_ROOT/Content/Data/MoveScripts/BotWatch.json" ]] || die "BotRun needs BotWatch.json" ;;
      BotDuel) ;;   # a spectator client: velox.bots.Watch, no JSON script (BotRun uses BotWatch.json)
      *) [[ -f "$REPO_ROOT/Content/Data/MoveScripts/$s.json" ]] || die "unknown script '$s'" ;;
    esac
  done

  local failed=0 round round_failed passed_rounds=0 rounds_run=0
  for (( round = 1; round <= REPEAT; round++ )); do
    if (( REPEAT > 1 )); then OUT_DIR="$base_dir/round-$round"; else OUT_DIR="$base_dir"; fi
    mkdir -p "$OUT_DIR"
    round_failed=0
    for s in "${scripts[@]}"; do
      if ! run_one "$s"; then round_failed=1; fi
      [[ -f "$OUT_DIR/$s.result" ]] || { echo "$s: no result (run aborted)" > "$OUT_DIR/$s.result"; round_failed=1; }
    done
    rounds_run=$round
    echo
    echo "=== Velox net soak round $round/$REPEAT (one way ${lag_desc} / 2 % loss both directions, server ${SERVER_FPS} Hz, client ${CLIENT_FPS} Hz) ==="
    for s in "${scripts[@]}"; do cat "$OUT_DIR/$s.result"; done
    if [[ "$round_failed" == 1 ]]; then
      failed=1
      if [[ "$KEEP_GOING" != 1 ]]; then break; fi
    else
      passed_rounds=$((passed_rounds + 1))
    fi
  done
  echo "Rounds passed: $passed_rounds/$rounds_run (requested $REPEAT)"
  for s in "${scripts[@]}"; do
    if [[ "$BOT_SCRIPTS" == *" $s "* ]]; then
      if (( rounds_run == REPEAT )); then
        bot_set_eval "$s" "$base_dir" "$REPEAT" || failed=1
      else
        echo "=== $s set evaluation skipped: only $rounds_run of $REPEAT rounds ran ==="
      fi
    fi
  done
  echo "Logs: $base_dir"
  if [[ "$failed" == 1 ]]; then echo "NET SOAK FAILED" >&2; exit 1; fi
  info "NET SOAK OK"
}

main "$@"
