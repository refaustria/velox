#!/usr/bin/env bash
# Linux CI: build Velox targets and run headless automation tests. See infra/ci/README.md.
set -euo pipefail
CI_SCRIPT_NAME="infra/ci/build.sh"
CI_PLATFORM="Linux"
CI_BUILD_SH="Engine/Build/BatchFiles/Linux/Build.sh"
CI_DEFAULT_UE_ROOTS=(/opt/UnrealEngine-5.8)
# shellcheck source=infra/ci/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

# Map audit (task m5-16 part A, ADR 0006 section 8): after a green build + tests, audit the DM1 maps that exist with
# Tools/editor/audit_map.py. Non-fatal (a WARNING) until L_DM1 is in MapsToCook, fatal afterwards. Skipped for --dry-run / --no-tests.
# Run one audit commandlet. The commandlet prints "[VeloxAudit] RESULT ..." as its last script line but UnrealEditor-Cmd can then hang
# in engine shutdown (task m5-16c), so: poll, and once the RESULT line is in the log give the process VELOX_AUDIT_GRACE seconds (default 60)
# to exit by itself, then terminate that one PID. VELOX_AUDIT_TIMEOUT (default 1500 s) bounds the whole run; a timeout without a RESULT
# line is reported as a missing verdict and handled by the caller's fatal/non-fatal rule.
ci_run_audit_editor() {
  local editor="$1" out="$2" script_args="$3" pid t0 done_at="" now
  local grace="${VELOX_AUDIT_GRACE:-60}" limit="${VELOX_AUDIT_TIMEOUT:-1500}"
  t0=$SECONDS
  "$editor" "$VELOX_PROJECT" -run=pythonscript -script="$script_args" \
    -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput > "$out" 2>&1 &
  pid=$!
  while kill -0 "$pid" 2>/dev/null; do
    now=$((SECONDS - t0))
    if [[ -z "$done_at" ]] && grep -a -q '\[VeloxAudit\] RESULT' "$out"; then done_at=$now; fi
    if [[ -n "$done_at" && $((now - done_at)) -ge $grace ]]; then
      echo "[ci] audit: RESULT printed ${done_at}s in, editor still alive after ${grace}s of shutdown; terminating pid $pid" >&2
      ci_kill_pid "$pid"; break
    fi
    if (( now >= limit )); then
      echo "WARNING: audit exceeded VELOX_AUDIT_TIMEOUT=${limit}s without a RESULT line; terminating pid $pid" >&2
      ci_kill_pid "$pid"; break
    fi
    sleep 2
  done
  wait "$pid" 2>/dev/null || true
  info "audit run took $((SECONDS - t0))s (RESULT at ${done_at:+${done_at}s}${done_at:-none})"
  return 0
}

# Terminate one process by PID: TERM, then KILL after 15 s. Never by name.
ci_kill_pid() {
  local pid="$1" i
  kill -TERM "$pid" 2>/dev/null || return 0
  for i in $(seq 1 15); do kill -0 "$pid" 2>/dev/null || return 0; sleep 1; done
  kill -KILL "$pid" 2>/dev/null || true
}

ci_audit() {
  local editor map out rc fatal=0 args
  editor="$(find_editor_cmd)" || { echo "WARNING: audit skipped, no UnrealEditor-Cmd" >&2; return 0; }
  if [[ -f "$REPO_ROOT/Config/DefaultGame.ini" ]] && grep -q 'FilePath="/Game/Maps/DM1/L_DM1"' "$REPO_ROOT/Config/DefaultGame.ini"; then fatal=1; fi
  for map in L_DM1_Blockout L_DM1; do
    [[ -f "$REPO_ROOT/Content/Maps/DM1/$map.umap" ]] || continue
    args="/Game/Maps/DM1/$map"
    if [[ "$map" == L_DM1 ]]; then
      args="$args --protrusion --lighting"
      [[ -f "$REPO_ROOT/Content/Maps/DM1/L_DM1_Blockout.umap" ]] && args="$args --diff-collision /Game/Maps/DM1/L_DM1_Blockout /Game/Maps/DM1/L_DM1"
    fi
    out="$LOG_DIR/audit-$map.log"
    info "Auditing $map"
    rc=0
    ci_run_audit_editor "$editor" "$out" "$REPO_ROOT/Tools/editor/audit_map.py $args" || rc=$?
    # The commandlet exit code is not reliable for script failures; the RESULT line is.
    if grep -a -q '\[VeloxAudit\] RESULT PASS' "$out"; then
      grep -a '\[VeloxAudit\]' "$out" | sed 's/^.*\[VeloxAudit\]/[audit]/' | head -40
    else
      grep -a '\[VeloxAudit\]\|Error' "$out" | head -20 >&2
      if [[ "$fatal" == 1 ]]; then echo "FAILED: audit of $map (log: $out)" >&2; exit 1
      else echo "WARNING: audit of $map did not pass (non-fatal until L_DM1 is in MapsToCook; log: $out)" >&2; fi
    fi
  done
}

ci_main "$@"
case " $* " in
  *" --dry-run "*|*" --no-tests "*) ;;
  *) ci_audit ;;
esac
