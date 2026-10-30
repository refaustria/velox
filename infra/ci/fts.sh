#!/usr/bin/env bash
# Headless functional tests (ADR 0005 section 14, amendment D9; task M4-04b). Runs the console-command FTs one after
# another as standalone -game -nullrhi worlds with a fixed 60 Hz step and checks each one's exit code and its
# `result=PASS` log line. Not part of build.sh (about 3-4 minutes wall). See infra/ci/README.md.
set -uo pipefail

CI_SCRIPT_NAME="infra/ci/fts.sh"
CI_PLATFORM="Linux"
CI_BUILD_SH="Engine/Build/BatchFiles/Linux/Build.sh"
CI_DEFAULT_UE_ROOTS=(/opt/UnrealEngine-5.8)
# shellcheck source=infra/ci/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

GREP=/usr/bin/grep   # the interactive shell's grep is a ugrep wrapper; use the real one
PORT=7777
FT_TIMEOUT="${VELOX_FTS_TIMEOUT:-240}"
SEED=1

# name | map | console command (quit is appended)
FTS=(
  "FtItemPickup|L_ItemTest|velox.items.FtItemPickup"
  "FtNavigation|L_ItemTest|velox.bots.FtNavigation"
  "FtStrafeCorridor|L_MovementTest|velox.bots.FtStrafeCorridor"
  "FtItemRouting|L_ItemTest|velox.bots.FtItemRouting"
  "FtOffMesh|L_ItemTest|velox.bots.FtOffMesh"
)

fts_usage() {
  cat <<USAGE
Usage: ${CI_SCRIPT_NAME} [--port N] [--timeout S] [--only a,b]

Runs, in order: FtItemPickup (L_ItemTest), FtNavigation (L_ItemTest), FtStrafeCorridor (L_MovementTest),
FtItemRouting (L_ItemTest), FtOffMesh (L_ItemTest), each as
  UnrealEditor-Cmd Velox.uproject <map> -game -nullrhi -unattended -nosound -UseFixedTimeStep -FPS=60 -log
      -port=N -ExecCmds="velox.bots.Seed ${SEED}, <command> quit"
under a timeout. A test passes when the process exits 0 and its log has a 'Velox*Ft ... result=PASS' line and no
'result=FAIL' line. Prints '[fts] <name> PASS|FAIL' per test and '[fts] OK n/n' (exit 0) or '[fts] FAIL k/n' (k = passed; exit 1).

Options:
  --port N      Port handed to every process (-port=N; default ${PORT}). Standalone -game worlds do not listen, the
                port only keeps a stray process from taking the default one.
  --timeout S   Seconds per test before it is killed and counted as FAIL (default ${FT_TIMEOUT}, VELOX_FTS_TIMEOUT)
  --only a,b    Run only these test names
  -h, --help    Show this help

Environment: UE_ROOT, VELOX_PROJECT, VELOX_OUT (as for build.sh).
Logs: \$VELOX_OUT/<timestamp>-<pid>/fts-<name>.log
USAGE
}

run_ft() {
  local name="$1" map="$2" cmd="$3" log="$4" rc
  local -a args=("$VELOX_PROJECT" "$map" -game -nullrhi -unattended -nosound -nopause -nosplash -UseFixedTimeStep -FPS=60 -log
    -port="$PORT" -ExecCmds="velox.bots.Seed ${SEED}, ${cmd} quit")
  info "[$name] $(printf '%q ' "$EDITOR_CMD" "${args[@]}")"
  timeout --kill-after=15 "$FT_TIMEOUT" "$EDITOR_CMD" "${args[@]}" >"$log" 2>&1
  rc=$?
  if (( rc == 124 || rc == 137 )); then
    echo "[fts] $name timeout after ${FT_TIMEOUT}s (log: $log)" >&2
  fi
  local pass_line
  pass_line="$("$GREP" -aE 'Velox[A-Za-z]*Ft( test=[A-Za-z0-9_]+)? result=PASS' "$log" | tail -n 1)"
  if (( rc == 0 )) && [[ -n "$pass_line" ]] && ! "$GREP" -aqE 'Velox[A-Za-z]*Ft( test=[A-Za-z0-9_]+)? result=FAIL' "$log"; then
    echo "[fts] $name PASS  (${pass_line##*\] })"
    return 0
  fi
  echo "[fts] $name FAIL  (exit=$rc, log: $log)"
  "$GREP" -aE 'Velox[A-Za-z]*Ft check=.* ok=0|result=FAIL' "$log" | tail -n 5 | sed 's/^/    /'
  return 1
}

main() {
  local only=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --port)    [[ $# -ge 2 && "$2" =~ ^[0-9]+$ ]] || die "--port needs a number"; PORT="$2"; shift 2 ;;
      --timeout) [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || die "--timeout needs a positive integer"; FT_TIMEOUT="$2"; shift 2 ;;
      --only)    [[ $# -ge 2 ]] || die "--only needs a value"; only=",$2,"; shift 2 ;;
      -h|--help) fts_usage; exit 0 ;;
      *) fts_usage >&2; die "unknown option: $1" ;;
    esac
  done
  DRY_RUN=0
  cd "$REPO_ROOT" || exit 1
  load_env_file
  ci_resolve_engine
  EDITOR_CMD="$(find_editor_cmd)" || die "UnrealEditor-Cmd not found under $UE_ROOT/Engine/Binaries/Linux"
  VELOX_OUT="${VELOX_OUT:-$REPO_ROOT/Saved/CI}"
  local out_dir
  if [[ -n "${VELOX_CI_LOG_DIR:-}" ]]; then out_dir="$VELOX_CI_LOG_DIR"; else out_dir="$VELOX_OUT/$(date +%Y%m%d-%H%M%S)-$$"; fi
  mkdir -p "$out_dir"
  info "Functional tests: port=$PORT timeout=${FT_TIMEOUT}s logs=$out_dir"

  local total=0 passed=0 entry name map cmd
  for entry in "${FTS[@]}"; do
    IFS='|' read -r name map cmd <<< "$entry"
    if [[ -n "$only" && "$only" != *",$name,"* ]]; then continue; fi
    total=$((total + 1))
    if run_ft "$name" "$map" "$cmd" "$out_dir/fts-$name.log"; then passed=$((passed + 1)); fi
  done
  (( total > 0 )) || die "no test selected"
  echo "Logs: $out_dir"
  if (( passed == total )); then
    echo "[fts] OK $passed/$total"
    exit 0
  fi
  echo "[fts] FAIL $passed/$total"
  exit 1
}

main "$@"
