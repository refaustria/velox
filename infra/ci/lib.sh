#!/usr/bin/env bash
# Shared functions for infra/ci/build.sh and infra/ci/build-mac.sh. Source only.
# The caller must define before calling ci_main: CI_PLATFORM (Linux|Mac),
# CI_BUILD_SH (relative to UE_ROOT), CI_DEFAULT_UE_ROOTS (array), CI_SCRIPT_NAME.

CI_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$CI_LIB_DIR/../.." && pwd)"

die() { echo "ERROR: $*" >&2; exit 1; }
info() { echo "[ci] $*"; }

usage() {
  cat <<USAGE
Usage: ${CI_SCRIPT_NAME} [options]

Builds Velox targets for ${CI_PLATFORM} and runs headless automation tests.

Options:
  --config X       Build configuration (default: Development, or VELOX_CONFIG)
  --targets a,b    Comma-separated targets (default: VeloxEditor,Velox,VeloxServer,VeloxClient;
                   Server/Client are SKIPPED on installed engines)
  --no-tests       Build only
  --tests-only     Run tests only, no build
  --clean          Clean the selected targets before building
  --dry-run        Print commands without running them (no engine required)
  -h, --help       Show this help

Environment (or infra/.env; real environment wins):
  UE_ROOT          Unreal Engine 5.8 root (required)
  VELOX_PROJECT    Path to Velox.uproject (default: <repo>/Velox.uproject)
  VELOX_OUT        Output root (default: <repo>/Saved/CI); logs go to \$VELOX_OUT/<YYYYmmdd-HHMMSS>-<pid>/
  VELOX_CONFIG     Default build configuration
  VELOX_TEST_TIMEOUT  Seconds before the test run is killed (default 1800)
Exit code is non-zero on any build or test failure.
USAGE
}

# Load infra/.env without overriding variables already set in the environment.
# Accepts KEY=value, KEY="value", KEY='value'; skips blank and '#'-leading lines.
# No inline-comment stripping, so values may contain '#'.
load_env_file() {
  local f="$REPO_ROOT/infra/.env" line key val
  [[ -f "$f" ]] || return 0
  while IFS= read -r line || [[ -n "$line" ]]; do
    line="${line#"${line%%[![:space:]]*}"}"
    [[ -z "$line" || "$line" == \#* ]] && continue
    [[ "$line" == *=* ]] || continue
    key="${line%%=*}"; val="${line#*=}"
    key="${key//[[:space:]]/}"
    key="${key#export}"
    [[ "$key" =~ ^(UE_ROOT|VELOX_PROJECT|VELOX_OUT|VELOX_CONFIG|VELOX_TEST_TIMEOUT)$ ]] || continue
    val="${val#"${val%%[![:space:]]*}"}"; val="${val%"${val##*[![:space:]]}"}"
    if [[ "$val" == \"*\" && ${#val} -ge 2 ]]; then val="${val:1:${#val}-2}"
    elif [[ "$val" == \'*\' && ${#val} -ge 2 ]]; then val="${val:1:${#val}-2}"; fi
    val="${val//\$PWD/$REPO_ROOT}"
    if [[ -z "${!key:-}" ]]; then export "$key=$val"; fi
  done < "$f"
}

# Print or run a command. In dry-run, only print.
run() {
  if [[ "$DRY_RUN" == 1 ]]; then
    printf '[dry-run]'; printf ' %q' "$@"; printf '\n'
    return 0
  fi
  "$@"
}

# run_logged <logfile> cmd...  : tee output into the log, preserve exit code.
run_logged() {
  local log="$1"; shift
  if [[ "$DRY_RUN" == 1 ]]; then
    printf '[dry-run]'; printf ' %q' "$@"; printf '   (log: %s)\n' "$log"
    return 0
  fi
  set +e
  "$@" 2>&1 | tee "$log"
  local rc=${PIPESTATUS[0]}
  set -e
  return "$rc"
}

ci_parse_args() {
  CONFIG="${VELOX_CONFIG:-Development}"
  TARGETS_ARG=""; DO_BUILD=1; DO_TESTS=1; DO_CLEAN=0; DRY_RUN=0
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --config)   [[ $# -ge 2 ]] || die "--config needs a value"; CONFIG="$2"; shift 2 ;;
      --targets)  [[ $# -ge 2 ]] || die "--targets needs a value"; TARGETS_ARG="$2"; shift 2 ;;
      --no-tests) DO_TESTS=0; shift ;;
      --tests-only) DO_BUILD=0; shift ;;
      --clean)    DO_CLEAN=1; shift ;;
      --dry-run)  DRY_RUN=1; shift ;;
      -h|--help)  usage; exit 0 ;;
      *) usage >&2; die "unknown option: $1" ;;
    esac
  done
  case "$CONFIG" in
    Debug|DebugGame|Development|Test|Shipping) ;;
    *) die "invalid --config '$CONFIG' (valid: Debug|DebugGame|Development|Test|Shipping)" ;;
  esac
  if [[ "$DO_BUILD" == 0 && "$DO_TESTS" == 0 ]]; then
    die "--no-tests and --tests-only together leave nothing to do"
  fi
}

ci_resolve_engine() {
  if [[ -z "${UE_ROOT:-}" ]]; then
    local c
    for c in "${CI_DEFAULT_UE_ROOTS[@]}"; do
      if [[ -d "$c/Engine/Build/BatchFiles" ]]; then UE_ROOT="$c"; info "UE_ROOT not set, using probed $c"; break; fi
    done
  fi
  [[ -n "${UE_ROOT:-}" ]] || die "UE_ROOT is not set. Export it or put it in infra/.env (see infra/env.example)."
  [[ -d "$UE_ROOT/Engine/Build/BatchFiles" ]] || die "UE_ROOT='$UE_ROOT' has no Engine/Build/BatchFiles; not a valid Unreal Engine root."
  VELOX_PROJECT="${VELOX_PROJECT:-$REPO_ROOT/Velox.uproject}"
  if [[ ! -f "$VELOX_PROJECT" ]]; then
    if [[ "$DRY_RUN" == 1 ]]; then info "WARNING: project file '$VELOX_PROJECT' not found (ignored in dry-run)"
    else die "project file '$VELOX_PROJECT' not found (set VELOX_PROJECT)."; fi
  fi
  if [[ -f "$UE_ROOT/Engine/Build/InstalledBuild.txt" ]]; then ENGINE_INSTALLED=1; else ENGINE_INSTALLED=0; fi
}

# Locate UnrealEditor-Cmd. Echoes path; empty if not found.
find_editor_cmd() {
  local c
  for c in "$UE_ROOT/Engine/Binaries/$CI_PLATFORM/UnrealEditor-Cmd" \
           "$UE_ROOT/Engine/Binaries/$CI_PLATFORM/UnrealEditor.app/Contents/MacOS/UnrealEditor-Cmd"; do
    if [[ -x "$c" ]]; then echo "$c"; return 0; fi
  done
  return 1
}

ci_build() {
  local all=(VeloxEditor Velox VeloxServer VeloxClient) targets=() t
  if [[ -n "$TARGETS_ARG" ]]; then IFS=',' read -r -a targets <<< "$TARGETS_ARG"; else targets=("${all[@]}"); fi
  # Mac cannot build Server/Client targets per ADR 0001 §5; treat like installed-engine skip.
  local build_sh="$UE_ROOT/$CI_BUILD_SH"
  [[ "$DRY_RUN" == 1 || -x "$build_sh" ]] || die "build script not found or not executable: $build_sh"
  for t in "${targets[@]}"; do
    case "$t" in
      VeloxEditor|Velox) ;;
      VeloxServer|VeloxClient)
        if [[ "$ENGINE_INSTALLED" == 1 ]]; then
          echo "SKIPPED (installed engine cannot build Server/Client targets): $t"; continue
        fi
        if [[ "$CI_PLATFORM" == Mac ]]; then
          echo "SKIPPED (Server/Client targets are not built on Mac, see ADR 0001 section 5): $t"; continue
        fi ;;
      *) die "unknown target '$t' (valid: ${all[*]})" ;;
    esac
    if [[ "$DO_CLEAN" == 1 ]]; then
      info "Cleaning $t"
      run_logged "$LOG_DIR/clean-$t.log" "$build_sh" "$t" "$CI_PLATFORM" "$CONFIG" -Project="$VELOX_PROJECT" -WaitMutex -Clean \
        || { echo "FAILED: clean $t (log: $LOG_DIR/clean-$t.log)" >&2; BUILD_FAILED=1; continue; }
    fi
    info "Building $t ($CI_PLATFORM $CONFIG)"
    if run_logged "$LOG_DIR/build-$t.log" "$build_sh" "$t" "$CI_PLATFORM" "$CONFIG" -Project="$VELOX_PROJECT" -WaitMutex; then
      BUILT+=("$t")
    else
      echo "FAILED: build $t (log: $LOG_DIR/build-$t.log)" >&2; BUILD_FAILED=1
    fi
  done
}

ci_tests() {
  local editor report="$LOG_DIR/automation" log="$LOG_DIR/tests.log"
  if ! editor="$(find_editor_cmd)"; then
    if [[ "$DRY_RUN" == 1 ]]; then editor="$UE_ROOT/Engine/Binaries/$CI_PLATFORM/UnrealEditor-Cmd"
    else echo "FAILED: UnrealEditor-Cmd not found under $UE_ROOT/Engine/Binaries/$CI_PLATFORM" >&2; TESTS_FAILED=1; return; fi
  fi
  run mkdir -p "$report"
  info "Running automation tests (Velox)"
  local rc=0
  run_logged "$log" timeout --kill-after=60 "${VELOX_TEST_TIMEOUT:-1800}" "$editor" "$VELOX_PROJECT" \
    -ExecCmds="Automation RunTests Velox; Quit" \
    -TestExit="Automation Test Queue Empty" -ReportExportPath="$report" \
    -nullrhi -unattended -nopause -nosplash -log || rc=$?
  if [[ "$DRY_RUN" == 1 ]]; then info "dry-run: would parse $report/index.json and grep the log"; return; fi
  if [[ "$rc" == 124 || "$rc" == 137 ]]; then
    echo "FAILED: tests timed out after ${VELOX_TEST_TIMEOUT:-1800}s (exit $rc; log: $log)" >&2; TESTS_FAILED=1
  elif [[ "$rc" != 0 ]]; then
    echo "Editor exit code $rc (log: $log)" >&2; TESTS_FAILED=1
  fi
  if [[ ! -f "$report/index.json" ]]; then
    echo "FAILED: test report missing: $report/index.json (log: $log)" >&2; TESTS_FAILED=1; return
  fi
  python3 -I - "$report/index.json" <<'PY' || TESTS_FAILED=1
import json, sys
try:
    d = json.load(open(sys.argv[1], encoding="utf-8-sig"))
except (OSError, ValueError) as e:
    print("FAILED: cannot read test report: %s" % e); sys.exit(1)
tests = d.get("tests", [])
if not tests:
    print("FAILED: report contains zero tests (an empty run does not pass)"); sys.exit(1)
w = max(len(t.get("fullTestPath") or t.get("testDisplayName", "")) for t in tests)
bad = 0
print("%-*s  %s" % (w, "TEST", "STATE"))
for t in tests:
    name = t.get("fullTestPath") or t.get("testDisplayName", "?")
    st = t.get("state", "?")
    if st != "Success": bad += 1
    print("%-*s  %s" % (w, name, st))
print("%d tests, %d not successful" % (len(tests), bad))
failed = int(d.get("failed", 0) or 0); not_run = int(d.get("notRun", 0) or 0)
if failed > 0 or not_run > 0:
    print("FAILED: report counters failed=%d notRun=%d" % (failed, not_run)); bad += 1
sys.exit(1 if bad else 0)
PY
  if grep -q 'Test Completed. Result={Fail' "$log"; then
    echo "FAILED: log contains failed test results (log: $log)" >&2; TESTS_FAILED=1
  fi
}

ci_main() {
  cd "$REPO_ROOT"
  load_env_file
  ci_parse_args "$@"
  ci_resolve_engine
  TS="$(date +%Y%m%d-%H%M%S)-$$"
  VELOX_OUT="${VELOX_OUT:-$REPO_ROOT/Saved/CI}"
  LOG_DIR="$VELOX_OUT/$TS"
  run mkdir -p "$LOG_DIR"
  BUILT=(); BUILD_FAILED=0; TESTS_FAILED=0
  info "Platform=$CI_PLATFORM Config=$CONFIG UE_ROOT=$UE_ROOT installed=$ENGINE_INSTALLED dry-run=$DRY_RUN"
  info "Log dir: $LOG_DIR"
  if [[ "$DO_BUILD" == 1 ]]; then ci_build; fi
  if [[ "$DO_TESTS" == 1 ]]; then
    if [[ "$DO_BUILD" == 1 && -n "$TARGETS_ARG" && ",$TARGETS_ARG," != *,VeloxEditor,* ]]; then
      echo "WARNING: --targets omits VeloxEditor; tests need an editor binary. Skipping tests (use --no-tests to build only)." >&2
      TESTS_FAILED=1
    elif [[ "$BUILD_FAILED" == 1 ]]; then info "Skipping tests because the build failed"; else ci_tests; fi
  fi
  if [[ "$BUILD_FAILED" == 1 || "$TESTS_FAILED" == 1 ]]; then
    echo "CI FAILED. Logs: $LOG_DIR" >&2; exit 1
  fi
  info "CI OK. Logs: $LOG_DIR"
}
