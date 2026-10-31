#!/usr/bin/env bash
# Nav-coverage lint (ADR 0006 section 6; task M5-03). Loads a map as a headless dedicated server, runs
# velox.bots.NavCoverage after the runtime nav build and judges its lines: walkable floor without nav mesh (clusters,
# D41's class) and nav polys under low clearance (lowclear, D32's class). Gate: clusters - edge = 0 and lowclear polys = 0 (edge strips, ADR 0006 E0.2 (e), are reported, not gated).
set -uo pipefail

CI_SCRIPT_NAME="infra/ci/navlint.sh"
CI_PLATFORM="Linux"
CI_BUILD_SH="Engine/Build/BatchFiles/Linux/Build.sh"
CI_DEFAULT_UE_ROOTS=(/opt/UnrealEngine-5.8)
# shellcheck source=infra/ci/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

GREP=/usr/bin/grep   # the interactive shell's grep is a ugrep wrapper; use the real one
PORT=7777
LINT_TIMEOUT="${VELOX_NAVLINT_TIMEOUT:-120}"
GRID=""
MIN_M2=""
REPORT_ONLY=0
VERBOSE=0

navlint_usage() {
  cat <<USAGE
Usage: ${CI_SCRIPT_NAME} <Map> [--report-only] [--verbose] [--grid CM] [--min-m2 M2] [--port N] [--timeout S]

Runs
  timeout ${LINT_TIMEOUT} UnrealEditor-Cmd Velox.uproject <Map> -server -nullrhi -unattended -nopause -nosplash -nosound
      -stdout -FullStdOutLogOutput -port=N -ExecCmds="velox.bots.NavCoverage [Grid=CM] [MinClusterM2=M2] [Verbose] Quit"
parses the VeloxNavCoverage lines and prints
  [navlint] <Map> walkable_m2=<x> covered_pct=<y> clusters=<n> (walkin=<w> isolated=<i> edge=<e>) lowclear=<p> result=PASS|FAIL|REPORT
followed by one '[navlint]   cluster ...' / '[navlint]   lowclear_poly ...' line per finding.

Exit: 0 PASS (clusters - edge = 0 and lowclear polys = 0) or --report-only; 1 findings; 2 the run produced no summary
(timeout, crash, no nav data). <Map> is a map name or package path (L_ItemTest or /Game/Maps/L_ItemTest).

Options:
  --report-only  Print the findings but exit 0 when the run itself succeeded
  --verbose      Also log one VeloxNavCoverage uncovered line per uncovered sample (in the log only)
  --grid CM      Lattice spacing handed to the command (default: the command's, 50)
  --min-m2 M2    Smallest reported cluster (default: the command's, 1)
  --port N       Server port (default ${PORT})
  --timeout S    Seconds before the server is killed (default ${LINT_TIMEOUT}, VELOX_NAVLINT_TIMEOUT)
  -h, --help     Show this help

Environment: UE_ROOT, VELOX_PROJECT, VELOX_OUT (as for build.sh). Log: \$VELOX_OUT/<timestamp>-<pid>/navlint-<map>.log
USAGE
}

main() {
  local map=""
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --report-only) REPORT_ONLY=1; shift ;;
      --verbose) VERBOSE=1; shift ;;
      --grid)    [[ $# -ge 2 && "$2" =~ ^[0-9]+([.][0-9]+)?$ ]] || die "--grid needs a number"; GRID="$2"; shift 2 ;;
      --min-m2)  [[ $# -ge 2 && "$2" =~ ^[0-9]+([.][0-9]+)?$ ]] || die "--min-m2 needs a number"; MIN_M2="$2"; shift 2 ;;
      --port)    [[ $# -ge 2 && "$2" =~ ^[0-9]+$ ]] || die "--port needs a number"; PORT="$2"; shift 2 ;;
      --timeout) [[ $# -ge 2 && "$2" =~ ^[1-9][0-9]*$ ]] || die "--timeout needs a positive integer"; LINT_TIMEOUT="$2"; shift 2 ;;
      -h|--help) navlint_usage; exit 0 ;;
      -*) navlint_usage >&2; die "unknown option: $1" ;;
      *) [[ -z "$map" ]] || die "only one map per run"; map="$1"; shift ;;
    esac
  done
  [[ -n "$map" ]] || { navlint_usage >&2; die "no map given"; }

  DRY_RUN=0
  cd "$REPO_ROOT" || exit 2
  load_env_file
  ci_resolve_engine
  EDITOR_CMD="$(find_editor_cmd)" || die "UnrealEditor-Cmd not found under $UE_ROOT/Engine/Binaries/Linux"
  VELOX_OUT="${VELOX_OUT:-$REPO_ROOT/Saved/CI}"
  local out_dir
  if [[ -n "${VELOX_CI_LOG_DIR:-}" ]]; then out_dir="$VELOX_CI_LOG_DIR"; else out_dir="$VELOX_OUT/$(date +%Y%m%d-%H%M%S)-$$"; fi
  mkdir -p "$out_dir"
  local map_name="${map##*/}"
  local log="$out_dir/navlint-${map_name}.log"

  local cmd="velox.bots.NavCoverage"
  [[ -n "$GRID" ]] && cmd+=" Grid=$GRID"
  [[ -n "$MIN_M2" ]] && cmd+=" MinClusterM2=$MIN_M2"
  (( VERBOSE )) && cmd+=" Verbose"
  cmd+=" Quit"
  local -a args=("$VELOX_PROJECT" "$map" -server -nullrhi -unattended -nopause -nosplash -nosound -stdout -FullStdOutLogOutput
    -port="$PORT" -ExecCmds="$cmd")
  info "navlint $map_name: $(printf '%q ' "$EDITOR_CMD" "${args[@]}")"
  # timeout signals the server's whole process group (no --foreground), then SIGKILLs it 10 s later.
  timeout --kill-after=10 "$LINT_TIMEOUT" "$EDITOR_CMD" "${args[@]}" >"$log" 2>&1
  local rc=$?
  if (( rc == 124 || rc == 137 )); then
    echo "[navlint] $map_name timeout after ${LINT_TIMEOUT}s (log: $log)" >&2
  fi

  local summary lowclear
  summary="$("$GREP" -aoE 'VeloxNavCoverage walkable_m2=.*' "$log" | tail -n 1)"
  lowclear="$("$GREP" -aoE 'VeloxNavCoverage lowclear polys=[0-9]+' "$log" | tail -n 1)"
  if [[ -z "$summary" || -z "$lowclear" ]]; then
    echo "[navlint] $map_name result=ERROR exit=$rc (no VeloxNavCoverage summary; log: $log)"
    "$GREP" -aE 'VeloxNavCoverage result=error|VeloxNav built' "$log" | tail -n 3 | sed 's/^/    /'
    exit 2
  fi
  local walkable covered clusters polys
  walkable="$(sed -nE 's/.*walkable_m2=([0-9.]+).*/\1/p' <<< "$summary")"
  covered="$(sed -nE 's/.*covered_pct=([0-9.]+).*/\1/p' <<< "$summary")"
  clusters="$(sed -nE 's/.*clusters=([0-9]+).*/\1/p' <<< "$summary")"
  polys="$(sed -nE 's/.*lowclear polys=([0-9]+).*/\1/p' <<< "$lowclear")"
  local walkin isolated edge
  edge="$(sed -nE 's/.* edge=([0-9]+).*/\1/p' <<< "$summary")"
  walkin="$(sed -nE 's/.* walkin=([0-9]+).*/\1/p' <<< "$summary")"
  isolated="$(sed -nE 's/.* isolated=([0-9]+).*/\1/p' <<< "$summary")"
  [[ "$edge" =~ ^[0-9]+$ ]] || edge=0
  [[ "$clusters" =~ ^[0-9]+$ && "$polys" =~ ^[0-9]+$ ]] || { echo "[navlint] $map_name result=ERROR (unparsable summary; log: $log)"; exit 2; }

  local result
  if (( clusters - edge <= 0 && polys == 0 )); then result=PASS
  elif (( REPORT_ONLY )); then result=REPORT
  else result=FAIL; fi
  echo "[navlint] $map_name walkable_m2=$walkable covered_pct=$covered clusters=$clusters (walkin=${walkin:-?} isolated=${isolated:-?} edge=$edge) lowclear=$polys result=$result"
  "$GREP" -aoE 'VeloxNavCoverage (cluster|lowclear_poly) .*' "$log" | sed -E 's/^VeloxNavCoverage /[navlint]   /'
  echo "[navlint] log: $log"
  [[ "$result" == FAIL ]] && exit 1
  exit 0
}

main "$@"
