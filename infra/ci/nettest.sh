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
SCRIPTS_ARG="StrafeRun,PadRun,KnockbackRun,KnockbackGroundRun,LatchRun,CrouchRun,FireRun,FireRunControl,RocketJumpRun"
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
COMBAT_SCRIPTS=" FireRun FireRunControl RocketJumpRun "

nettest_usage() {
  cat <<USAGE
Usage: ${CI_SCRIPT_NAME} [options]

Runs dedicated server + scripted -nullrhi client (100 ms / 2 % loss both directions) per script and checks the
net-soak pass criteria. Exit code is non-zero if any script fails. FireRun, FireRunControl (FireRun.json with
velox.combat.LagComp 0) and RocketJumpRun (velox.combat.RocketJumpTest -90) run on L_CombatTest.

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
    FireRun|FireRunControl) echo "$FIRE_LAG_MS" ;;
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
  case "$script" in
    FireRun|FireRunControl)
      # Task 08: invulnerable strafing target at FireRun.json's dummyLocation, every weapon, per-pawn rewind lines.
      local dummy lagcomp=1
      dummy="$(python3 -I -c 'import json,sys; print(" ".join("%g" % v for v in json.load(open(sys.argv[1]))["dummyLocation"]))' \
        "$REPO_ROOT/Content/Data/MoveScripts/FireRun.json")" || { echo "FAILED: FireRun.json has no dummyLocation" >&2; return 1; }
      [[ "$script" == FireRunControl ]] && lagcomp=0
      sexec="velox.combat.GiveAllWeapons, velox.combat.SpawnDummy $dummy invulnerable, velox.lagcomp.Debug 1, velox.combat.LagComp $lagcomp"
      script_args=(-VeloxScript=FireRun) ;;
    RocketJumpRun)
      # Task 08 / ADR 0003 B25: wraps the task-05 test command; VeloxCorrection lines come from velox.move.LogCorrections.
      sexec="velox.combat.GiveAllWeapons"
      cexec="$cexec, velox.move.LogCorrections 1, velox.combat.RocketJumpTest -90 quit"
      script_args=() ;;
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
  "$EDITOR_CMD" "$VELOX_PROJECT" "127.0.0.1:$PORT" -game -log -stdout -FullStdOutLogOutput -nullrhi -nosound -unattended -nopause -nosplash \
    -ExecCmds="$cexec" "${script_args[@]}" -WINDOWED -ResX=320 -ResY=240 \
    "${PKT_ARGS[@]}" >"$clog" 2>&1 &
  CLIENT_PID=$!
  waited=0
  while kill -0 "$CLIENT_PID" 2>/dev/null; do
    if (( waited >= CLIENT_TIMEOUT )); then echo "[$script] client timeout after ${CLIENT_TIMEOUT}s" >&2; break; fi
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

if script == "RocketJumpRun":
    # velox.combat.RocketJumpTest's result line instead of a script's VeloxNetSoak line.
    final = next((l for l in reversed(C) if "VeloxRocketJump: apex_cm=" in l), None)
    if final is None:
        fails.append("no VeloxRocketJump result line (client timeout or crash)")
        f = {}
    else:
        f = kv(final)
        if f.get("result") != "PASS":
            fails.append("RocketJumpTest result=%s: %s" % (f.get("result"), final.split("VeloxRocketJump:", 1)[-1].strip()[:200]))
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

elif script in ("FireRun", "FireRunControl"):
    # Task 08 / ADR 0003 section 9, B17, B24, B25: FireRun.json against the invulnerable strafing dummy. Client lines:
    # VeloxShotFired (seq, weapon, aim_err_cm, view_delay_ms, server_est), VeloxHitConfirm (new=), ClientActivateAbilityFailed
    # (GAS, Display). Server lines: VeloxShot (seq, hit, rewind_ms, view_ms, hint, hits, snap), VeloxShotRejected,
    # VeloxProjectileExplode, SpawnDummy, VeloxLagCompRegister/VeloxLagCompRewind (velox.lagcomp.Debug 1).
    control = script == "FireRunControl"
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
    rails = [d for d in fired.values() if "Railgun" in d.get("weapon", "") and judged(d)]
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
    railfile = os.path.join(out_dir, "FireRun.railhits")
    if not control:
        with open(railfile, "w") as rf:
            rf.write("%d %d\n" % (hit_n, len(rails)))
        if len(rails) < 10:
            fails.append("expected >= 10 judged rail shots, got %d" % len(rails))
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
        out_rew = [(s_.get("seq"), num(s_, "rewind_ms")) for _, s_ in rail_srv if s_ and not (60.0 <= num(s_, "rewind_ms") <= 250.0)]
        if out_rew:
            fails.append("rail rewind_ms outside [60, 250]: %s" % out_rew)
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

elif script == "RocketJumpRun":
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
    shots = [kv(l) for l in S if "VeloxProjectileShot seq=" in l and "client_ts=" in l]
    if not shots:
        fails.append("no VeloxProjectileShot line on the server")
    for d in shots:
        gap = abs(num(d, "client_ts") - num(d, "last_ts"))
        detail.append("client_ts-last_ts=%.4f" % gap)
        if gap > 1.0 / 60.0 + rtt:
            fails.append("VeloxProjectileShot seq=%s |client_ts - last_ts| %.4f > 1/60 + RTT %.3f" % (d.get("seq"), gap, rtt))
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

# ---- global rules on non-echo corrections (the intended ones are excluded)
rest = [c for c in nonecho if c not in excluded]
xs = [c[1] for c in rest]
x = max(xs) if xs else 0.0
y = sum(xs) / len(xs) if xs else 0.0
if script in ("StrafeRun", "KnockbackRun", "KnockbackGroundRun", "FireRun", "FireRunControl"):
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
      FireRunControl) [[ -f "$REPO_ROOT/Content/Data/MoveScripts/FireRun.json" ]] || die "FireRunControl needs FireRun.json" ;;
      RocketJumpRun) ;;   # drives velox.combat.RocketJumpTest, no JSON script
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
  echo "Logs: $base_dir"
  if [[ "$failed" == 1 ]]; then echo "NET SOAK FAILED" >&2; exit 1; fi
  info "NET SOAK OK"
}

main "$@"
