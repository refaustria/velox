#!/usr/bin/env bash
# Headless networked movement soak (M1 task 05, ADR 0002 section 5 and amendments A17-A22).
# Starts a dedicated server and one scripted -nullrhi client on this machine with 100 ms / 2 % packet simulation in both
# directions, runs the four soak scripts and applies the per-script pass criteria from
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
SCRIPTS_ARG="StrafeRun,PadRun,KnockbackRun,LatchRun"
CLIENT_TIMEOUT="${VELOX_NETTEST_TIMEOUT:-120}"
SERVER_START_TIMEOUT="${VELOX_NETTEST_SERVER_START_TIMEOUT:-180}"
SERVER_FPS=60
CLIENT_FPS=120
# Client-side packet simulation: outgoing (PktLag/PktLoss) and incoming (PktIncoming*), RTT about 200 ms, 2 % loss each way.
PKT_ARGS=(-PktLag=100 -PktLagVariance=20 -PktLoss=2 -PktIncomingLagMin=80 -PktIncomingLagMax=120 -PktIncomingLoss=2)

nettest_usage() {
  cat <<USAGE
Usage: ${CI_SCRIPT_NAME} [options]

Runs dedicated server + scripted -nullrhi client (100 ms / 2 % loss both directions) per script and checks the
net-soak pass criteria. Exit code is non-zero if any script fails.

Options:
  --scripts a,b    Scripts to run (default: ${SCRIPTS_ARG})
  --port N         Server UDP port (default ${PORT}; must be free)
  --timeout S      Seconds to wait for a client's VeloxNetSoak line (default ${CLIENT_TIMEOUT})
  -h, --help       Show this help

Environment: UE_ROOT, VELOX_PROJECT, VELOX_OUT (as for build.sh), VELOX_NETTEST_TIMEOUT.
Logs: \$VELOX_OUT/<timestamp>-<pid>/nettest/<script>-{server,client}.log
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

port_busy() { ss -H -ulnp 2>/dev/null | "$GREP" -q ":${PORT}[[:space:]]"; }

# run_one <script> : leaves results in $OUT_DIR/<script>.result; returns 0 pass / 1 fail
run_one() {
  local script="$1" slog clog extra=() crc=0 waited=0
  slog="$OUT_DIR/$script-server.log"; clog="$OUT_DIR/$script-client.log"
  # Real-time pacing, not -UseFixedTimeStep: that mode runs frames as fast as possible, so the (wall-clock) packet
  # simulation would add seconds of game time per 100 ms. The server is paced by NetServerMaxTickRate=60, the client by t.MaxFPS.
  local cexec="t.MaxFPS ${CLIENT_FPS}" sexec=""
  if [[ "$script" == LatchRun ]]; then cexec="velox.move.Preset Q3, $cexec"; sexec="velox.move.Preset Q3"; fi
  # Debug knob: VELOX_NETTEST_EXEC="p.NetShowCorrections 1" runs extra console commands on both processes.
  if [[ -n "${VELOX_NETTEST_EXEC:-}" ]]; then cexec="$cexec, $VELOX_NETTEST_EXEC"; sexec="${sexec:+$sexec, }$VELOX_NETTEST_EXEC"; fi
  if [[ -n "$sexec" ]]; then extra=(-ExecCmds="$sexec"); fi
  if port_busy; then echo "FAILED: UDP port $PORT is in use (ss -ulnp)" >&2; return 1; fi

  info "[$script] starting dedicated server (${SERVER_FPS} Hz), log $slog"
  "$EDITOR_CMD" "$VELOX_PROJECT" L_MovementTest -server -log -stdout -FullStdOutLogOutput -nullrhi -unattended -nopause -nosplash \
    -port="$PORT" "${extra[@]}" >"$slog" 2>&1 &
  SERVER_PID=$!
  while ! "$GREP" -aq "Game Engine Initialized" "$slog"; do
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then echo "FAILED: server exited during startup (log: $slog)" >&2; SERVER_PID=""; return 1; fi
    if (( waited >= SERVER_START_TIMEOUT )); then echo "FAILED: server not ready after ${SERVER_START_TIMEOUT}s (log: $slog)" >&2; cleanup; return 1; fi
    sleep 1; waited=$((waited + 1))
  done
  sleep 2

  info "[$script] starting client (${CLIENT_FPS} Hz, packet simulation), log $clog"
  "$EDITOR_CMD" "$VELOX_PROJECT" "127.0.0.1:$PORT" -game -log -stdout -FullStdOutLogOutput -nullrhi -nosound -unattended -nopause -nosplash \
    -ExecCmds="$cexec" -VeloxScript="$script" -WINDOWED -ResX=320 -ResY=240 \
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

  python3 -I - "$script" "$slog" "$clog" "$crc" "$server_alive" "$OUT_DIR/$script.result" <<'PY'
import re, sys

script, slog, clog, crc, server_alive, result_path = sys.argv[1:7]
crc = int(crc); server_alive = server_alive == "1"

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

final = next((l for l in reversed(C) if "VeloxNetSoak: script=" in l), None)
if final is None:
    fails.append("no VeloxNetSoak line (client timeout or crash)")
    f = {}
else:
    f = kv(final)
    if f.get("result") != "PASS":
        fails.append("script result=%s (%s)" % (f.get("result"), final.split("reason=", 1)[-1]))
moves = int(num(f, "moves")); ncorr = int(num(f, "corrections"))
cmax = num(f, "max"); cmean = num(f, "mean")

# ---- client events
corr = []     # (t, cm, fields)
for l in C:
    if "VeloxCorrection t=" in l:
        d = kv(l); corr.append((num(d, "t"), num(d, "cm"), d))
info_line = next((kv(l) for l in C if "event=info" in l), {})
ground_speed = num(info_line, "ground_speed", 800.0)
jump_velocity = num(info_line, "jump_velocity", 0.0)
gravity = abs(num(info_line, "gravity_z", 0.0))
responses = [(num(kv(l), "t"), num(kv(l), "grace"), num(kv(l), "latch")) for l in C if "VeloxCorrectionResponse t=" in l]
samples = [kv(l) for l in C if "event=post_correction_sample" in l]
lands = [kv(l) for l in C if "event=land" in l and "VeloxNetSoak" in l]
kb = [kv(l) for l in C if "VeloxNetSoak event=knockback " in l]

def in_window(t0, t1):
    return [c for c in corr if t0 <= c[0] <= t1]

def stray_only(t0, t1, what, exclude=()):
    w = [c for c in in_window(t0, t1) if c not in exclude]
    if len(w) > 1 or any(c[1] > 5.0 for c in w):
        fails.append("%s: corrections in window [%.2f,%.2f]: %s (allowed: one <= 5 cm)" % (what, t0, t1, [(round(c[0], 2), round(c[1], 1)) for c in w]))
    return w

Lexp = 0.02 * moves
detail = ["Lexp=%.1f" % Lexp]
excluded = []

if script == "StrafeRun":
    pk = num(f, "peak2d"); pt = num(f, "peak_t", -1.0)
    detail.append("peak=%.0f (%.2fx)" % (pk, pk / ground_speed if ground_speed else 0))
    if ground_speed and pk < 1.4 * ground_speed:
        fails.append("peak speed %.0f < 1.4 x %.0f" % (pk, ground_speed))
    bad = [c for c in in_window(pt, pt + 0.5) if c[1] > 5.0]
    if bad:
        fails.append("correction > 5 cm within 0.5 s after the speed peak: %s" % [(round(c[0], 2), round(c[1], 1)) for c in bad])

elif script == "PadRun":
    def pads(lines):
        out = {}
        for l in lines:
            if "VeloxJumpPad: pad=" in l:
                d = kv(l); out.setdefault(d["pad"], []).append(num(d, "t"))
        return out
    cp, sp = pads(C), pads(S)
    detail.append("pads=%s" % sorted(cp))
    if len(cp) != 3:
        fails.append("expected 3 distinct pads launched on the client, got %d: %s" % (len(cp), sorted(cp)))
    for pad in sorted(set(cp) | set(sp)):
        if len(cp.get(pad, [])) != 1:
            fails.append("pad %s: %d client launch lines (want 1)" % (pad, len(cp.get(pad, []))))
        if len(sp.get(pad, [])) != 1:
            fails.append("pad %s: %d server launch lines (want 1)" % (pad, len(sp.get(pad, []))))
    for pad, ts in sorted(cp.items()):
        for t in ts:
            stray_only(t, t + 0.6, "pad %s launch" % pad)
    if final and "checks=3/3" not in final:
        fails.append("landing checkpoints: %s" % kv(final).get("checks"))

elif script == "KnockbackRun":
    if not kb:
        fails.append("no knockback event on the client")
    else:
        tk = num(kb[0], "t")
        w = in_window(tk, tk + 0.5)
        if not w:
            fails.append("no correction in [t_k, t_k+0.5] after the knockback (t_k=%.2f)" % tk)
        else:
            intended = max(w, key=lambda c: c[1])
            others = [c for c in w if c is not intended]
            if len(others) > 1 or any(c[1] > 5.0 for c in others):
                fails.append("more than one stray correction beyond the intended one: %s" % [(round(c[0], 2), round(c[1], 1)) for c in others])
            excluded.append(intended)
            tc, cm, d = intended
            resp = [r for r in responses if r[0] <= tc + 1e-6]
            rgrace = resp[-1][1] if resp else -1.0
            detail.append("intended=%.1f cm at +%.2fs response_grace=%.3f grace_after_replay=%.3f air=%s" % (cm, tc - tk, rgrace, num(d, "grace"), d.get("air")))
            if d.get("air") != "1":
                fails.append("client not airborne right after the knockback correction")
            # The task file asks for GetKnockbackGrace() > 0.1 after the replay. With 200 ms RTT the replay spans more than the
            # maximum grace (0.2 s), so the live value is 0 by construction; the response container's value is what proves delivery.
            if rgrace <= 0.1:
                fails.append("knockback grace in the server's response is %.3f (<= 0.1): response container lost it" % rgrace)
            smp = [s for s in samples if abs(num(s, "t") - (tc + 0.15)) < 0.1]
            if not smp:
                fails.append("no speed sample 0.15 s after the correction")
            else:
                sp = num(smp[0], "speed2d")
                detail.append("speed+0.15s=%.0f" % sp)
                if sp < 0.9 * 1250:
                    fails.append("speed 0.15 s after the correction %.0f < %.0f (friction not suppressed)" % (sp, 0.9 * 1250))
            late = in_window(tc + 0.05, tc + 0.6)
            late = [c for c in late if c is not intended]
            if late:
                fails.append("further correction in [t_corr+0.05, t_corr+0.6]: %s" % [(round(c[0], 2), round(c[1], 1)) for c in late])

elif script == "LatchRun":
    if len(lands) < 2:
        fails.append("expected >= 2 landing observations, got %d" % len(lands))
    if not kb:
        fails.append("no knockback event on the client")
    if len(lands) >= 2 and kb:
        tk = num(kb[0], "t")
        l1, l2 = lands[0], lands[1]
        t1, t2 = num(l1, "t"), num(l2, "t")
        mid = [c for c in corr if tk <= c[0] <= t1 and c[2].get("air") == "1"]
        detail.append("midair_corr=%d" % len(mid))
        if not mid:
            fails.append("no mid-air correction between the knockback and the first landing (latch-through-replay not exercised)")
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

# ---- global rules (corrections that are the intended ones are excluded)
rest = [c for c in corr if c not in excluded]
if script != "LatchRun" or True:
    xs = [c[1] for c in rest]
    x = max(xs) if xs else 0.0
    y = sum(xs) / len(xs) if xs else 0.0
    if script in ("StrafeRun", "KnockbackRun"):
        if len(rest) > 3 * Lexp + (0 if script == "StrafeRun" else 1):
            fails.append("corrections C=%d > 3 x L_exp (%.1f)" % (len(rest), 3 * Lexp))
        if x > 30.0:
            fails.append("max correction %.1f cm > 30" % x)
        if y > 5.0:
            fails.append("mean correction %.2f cm > 5" % y)

row = "%-12s moves=%-5d C=%-3d max=%-6.2f mean=%-5.2f %s  %s  %s" % (
    script, moves, ncorr, cmax, cmean, "FAIL" if fails else "PASS", " ".join(detail), ("; ".join(notes)))
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
  if [[ -n "${VELOX_CI_LOG_DIR:-}" ]]; then OUT_DIR="$VELOX_CI_LOG_DIR/nettest"; else OUT_DIR="$VELOX_OUT/$(date +%Y%m%d-%H%M%S)-$$/nettest"; fi
  mkdir -p "$OUT_DIR"
  info "Net soak: scripts=${scripts[*]} port=$PORT logs=$OUT_DIR"

  local failed=0
  for s in "${scripts[@]}"; do
    [[ -f "$REPO_ROOT/Content/Data/MoveScripts/$s.json" ]] || die "unknown script '$s'"
    if ! run_one "$s"; then failed=1; fi
    [[ -f "$OUT_DIR/$s.result" ]] || { echo "$s: no result (run aborted)" > "$OUT_DIR/$s.result"; failed=1; }
  done

  echo
  echo "=== Velox net soak (100 ms / 2 % loss both directions, server ${SERVER_FPS} Hz, client ${CLIENT_FPS} Hz) ==="
  for s in "${scripts[@]}"; do cat "$OUT_DIR/$s.result"; done
  echo "Logs: $OUT_DIR"
  if [[ "$failed" == 1 ]]; then echo "NET SOAK FAILED" >&2; exit 1; fi
  info "NET SOAK OK"
}

main "$@"
