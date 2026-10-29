#!/usr/bin/env bash
# Settings and key rebinding survive a restart (M3-09, ADR 0004 section 7).
# Three headless `-game -nullrhi` processes on L_CombatTest: `write` (sets a non-default profile, rebinds Fire to
# RightMouseButton), `verify` in a NEW process (values loaded from disk), `reset` (defaults again). Also checks that
# GameUserSettings.ini holds the values and that the Enhanced Input save slot file exists after `write`.
# Usage: infra/ci/settings-restart.sh [--map L_CombatTest] [--timeout S]
# Needs UE_ROOT / VELOX_PROJECT (environment or infra/.env). Logs: <VELOX_OUT|Saved/CI>/settings-restart-<pid>/.
set -uo pipefail

CI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$CI_DIR/../.." && pwd)"
MAP="L_CombatTest"
TIMEOUT=240

while [[ $# -gt 0 ]]; do
  case "$1" in
    --map) MAP="$2"; shift 2 ;;
    --timeout) TIMEOUT="$2"; shift 2 ;;
    -h|--help) sed -n 2,9p "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done

if [[ -f "$REPO_ROOT/infra/.env" ]]; then
  while IFS='=' read -r key val; do
    [[ "$key" =~ ^(UE_ROOT|VELOX_PROJECT|VELOX_OUT)$ ]] || continue
    val="${val//\$PWD/$REPO_ROOT}"
    [[ -z "${!key:-}" ]] && export "$key=$val"
  done < "$REPO_ROOT/infra/.env"
fi
: "${UE_ROOT:?UE_ROOT not set}"
VELOX_PROJECT="${VELOX_PROJECT:-$REPO_ROOT/Velox.uproject}"
EDITOR_CMD="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
PROJECT_DIR="$(dirname "$VELOX_PROJECT")"
OUT="${VELOX_OUT:-$REPO_ROOT/Saved/CI}/settings-restart-$$"
mkdir -p "$OUT"

fail() { echo "[settings-restart] FAIL: $*"; echo "[settings-restart] logs: $OUT"; exit 1; }

run_phase() {
  local phase="$1" log="$OUT/$1.log"
  timeout "$TIMEOUT" "$EDITOR_CMD" "$VELOX_PROJECT" "$MAP" -game -nullrhi -nosound -unattended -nopause -nosplash \
    -log -stdout -FullStdOutLogOutput -ExecCmds="velox.settings.RestartTest $phase quit" > "$log" 2>&1
  /usr/bin/grep -a "VeloxSettingsTest" "$log" | tail -3
  /usr/bin/grep -aq "VeloxSettingsTest phase=$phase result=PASS" "$log" || fail "phase $phase did not pass (see $log)"
}

run_phase write
INI="$(find "$PROJECT_DIR/Saved/Config" -name GameUserSettings.ini 2>/dev/null | head -1)"
[[ -n "$INI" ]] || fail "no GameUserSettings.ini under $PROJECT_DIR/Saved/Config"
for expect in "MouseSensitivity=7.5" "FieldOfView=95" "PlayerName=RestartTest" "bAutoSwitchOnPickup=False" "AntiAliasing=FXAA"; do
  /usr/bin/grep -aq "$expect" "$INI" || fail "$INI lacks $expect"
done
echo "[settings-restart] GameUserSettings.ini holds the written values: $INI"
SLOT="$(find "$PROJECT_DIR/Saved/SaveGames" -name 'EnhancedInputUserSettings*' 2>/dev/null | head -1)"
[[ -n "$SLOT" ]] || fail "no Enhanced Input save slot under $PROJECT_DIR/Saved/SaveGames"
echo "[settings-restart] key binding save slot: $SLOT"

run_phase verify
/usr/bin/grep -a "VeloxSettings applied" "$OUT/verify.log" | head -2
run_phase reset
echo "[settings-restart] OK"
