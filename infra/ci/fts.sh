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
DM1_MAP="${VELOX_DM1_MAP:-L_DM1_Blockout}"   # map of FtDm1Reachability (--dm1-map / VELOX_DM1_MAP; default stays the blockout until decision 40)

# name | map | console command (quit is appended)
FTS=(
  "FtItemPickup|L_ItemTest|velox.items.FtItemPickup"
  "FtNavigation|L_ItemTest|velox.bots.FtNavigation"
  "FtStrafeCorridor|L_MovementTest|velox.bots.FtStrafeCorridor"
  "FtItemRouting|L_ItemTest|velox.bots.FtItemRouting"
  "FtOffMesh|L_ItemTest|velox.bots.FtOffMesh"
  "FtSegmentBlocked|L_ItemTest|velox.bots.FtSegmentBlocked"
  "FtCombatEvents|L_CombatTest|velox.bots.FtCombatEvents"
  "FtDm1Reachability|@DM1|velox.bots.FtDm1Reachability"   # the alias of FtMapReachability (ADR 0008 1.6)
  "FtHazard|L_HazardTest|velox.combat.FtHazard"
  "FtMapReachability-L_Sacrarium_Blockout|L_Sacrarium_Blockout|velox.bots.FtMapReachability WalkExempt=DA_Item_Weapon_Railgun"   # M7 B1 (ADR 0008 5.3 c); the Railgun sits on the loft (pads / rocket jumps only)
  "FtMapReachability-L_Nemus_Blockout|L_Nemus_Blockout|velox.bots.FtMapReachability WalkExempt=DA_Item_Weapon_Railgun"   # M7 B2 (ADR 0008 5.3 c); the Railgun sits on the Corona (pad P4 / rocket jumps only)
  "FtBotEdge|L_HazardTest|velox.bots.FtBotEdge"
  "FtBotLavaEscape|L_HazardTest|velox.bots.FtBotLavaEscape"
  "FtBotKnockOff|L_HazardTest|velox.bots.FtBotKnockOff"
  "FtBotLavaAvoid|L_HazardTest|velox.bots.FtBotLavaAvoid"
  "FtBotTargets|L_CombatTest|velox.bots.FtBotTargets"   # M7 H10: unique bot PlayerIds, the target is the visible enemy
)
# One row per M7 map (ADR 0008 1.6): the blockout maps are not in the default list (not cooked, no band yet) and are added with
# --map-ft <Map>[:WalkExempt ids] (repeatable), e.g. --map-ft L_Sacrarium_Blockout:Quad,Railgun. Row name: FtMapReachability-<Map>.
MAP_FTS=()

fts_usage() {
  cat <<USAGE
Usage: ${CI_SCRIPT_NAME} [--port N] [--timeout S] [--only a,b] [--dm1-map NAME] [--map-ft Map[:ids]]...

Runs, in order: FtItemPickup (L_ItemTest), FtNavigation (L_ItemTest), FtStrafeCorridor (L_MovementTest),
FtItemRouting (L_ItemTest), FtOffMesh (L_ItemTest), FtSegmentBlocked (L_ItemTest), FtCombatEvents (L_CombatTest), FtDm1Reachability (${DM1_MAP}, FT_Map_Reachability via the old command name),
FtHazard (L_HazardTest, FT_Hazard: ADR 0008 3.7), FtBotEdge (L_HazardTest, FT_BotEdge: ADR 0008 3.6.1/2, 60 s fight),
FtBotLavaEscape (L_HazardTest, FT_BotLavaEscape: ADR 0008 3.6.4), FtBotKnockOff (L_HazardTest, FT_BotKnockOff: ADR 0008 3.6.3, 10 trials + 10 pad crossings + 10 pad crossings knocked sideways in the flight, M7 H8),
FtBotLavaAvoid (L_HazardTest, FT_BotLavaAvoid: task M7 H7, 8 routes across the lava channel + 8 fights beside it, 0 lava entries),
each as
  UnrealEditor-Cmd Velox.uproject <map> -game -nullrhi -unattended -nosound -UseFixedTimeStep -FPS=60 -log
      -port=N -ExecCmds="velox.bots.Seed ${SEED}, <command> quit"
under a timeout. A test passes when the process exits 0 and its log has a 'Velox*Ft ... result=PASS' line and no
'result=FAIL' line. Prints '[fts] <name> PASS|FAIL' per test and '[fts] OK n/n' (exit 0) or '[fts] FAIL k/n' (k = passed; exit 1).

Options:
  --port N      Port handed to every process (-port=N; default ${PORT}). Standalone -game worlds do not listen, the
                port only keeps a stray process from taking the default one.
  --timeout S   Seconds per test before it is killed and counted as FAIL (default ${FT_TIMEOUT}, VELOX_FTS_TIMEOUT)
  --only a,b    Run only these test names
  --map-ft M[:I] Add a row "FtMapReachability-M" (velox.bots.FtMapReachability on map M; I = comma separated WalkExempt item ids, which
                switches the Walk-filter check on). Repeatable; runs after the default rows.
  --dm1-map N   Map of FtDm1Reachability (default ${DM1_MAP}; env VELOX_DM1_MAP), e.g. L_DM1 for the art map
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
      --map-ft)  [[ $# -ge 2 && "$2" =~ ^[A-Za-z0-9_]+(:[A-Za-z0-9_.,]+)?$ ]] || die "--map-ft needs Map[:id,id]"; MAP_FTS+=("$2"); shift 2 ;;
      --dm1-map) [[ $# -ge 2 && "$2" =~ ^[A-Za-z0-9_]+$ ]] || die "--dm1-map needs a map name"; DM1_MAP="$2"; shift 2 ;;
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
    [[ "$map" == "@DM1" ]] && map="$DM1_MAP"
    if [[ -n "$only" && "$only" != *",$name,"* ]]; then continue; fi
    total=$((total + 1))
    if run_ft "$name" "$map" "$cmd" "$out_dir/fts-$name.log"; then passed=$((passed + 1)); fi
  done
  local mf mname mids
  for mf in "${MAP_FTS[@]+"${MAP_FTS[@]}"}"; do
    mname="${mf%%:*}"; mids=""; [[ "$mf" == *:* ]] && mids="${mf#*:}"
    name="FtMapReachability-$mname"
    if [[ -n "$only" && "$only" != *",$name,"* ]]; then continue; fi
    total=$((total + 1))
    if run_ft "$name" "$mname" "velox.bots.FtMapReachability${mids:+ WalkExempt=$mids}" "$out_dir/fts-$name.log"; then passed=$((passed + 1)); fi
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
