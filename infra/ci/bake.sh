#!/usr/bin/env bash
# Lighting bake (ADR 0006 section 5): ResavePackages -BuildLighting through the local UnrealLightmass.
# Usage: infra/ci/bake.sh <Map, e.g. /Game/Maps/L_ItemTest> <Preview|Medium|High|Production>
# Env: UE_ROOT, VELOX_PROJECT (or infra/.env), VELOX_OUT, SOAK_LOCK (default /tmp/velox-soak.lock),
#      VELOX_BAKE_TIMEOUT seconds (default 5400), VELOX_BAKE_PLATFORM (Linux|Mac, default Linux),
#      VELOX_BAKE_EXTRA (extra commandlet args, e.g. -nullrhi for the probe).
# Linux: software Vulkan (lavapipe) under the soak lock. Mac: Metal, no lock/ICD (run by the human).
# Exit non-zero unless the log shows Lightmass completion and the map was saved.
set -euo pipefail
CI_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$CI_LIB_DIR/../.." && pwd)"
# shellcheck source=infra/ci/lib.sh
source "$CI_LIB_DIR/lib.sh"
load_env_file
PLATFORM="${VELOX_BAKE_PLATFORM:-Linux}"
CI_PLATFORM="$PLATFORM"
MAP="${1:-}"; QUALITY="${2:-}"
[[ -n "$MAP" && -n "$QUALITY" ]] || die "usage: bake.sh <Map> <Preview|Medium|High|Production>"
case "$QUALITY" in Preview|Medium|High|Production) ;; *) die "invalid quality '$QUALITY'" ;; esac
if [[ "$PLATFORM" == Mac ]]; then CI_DEFAULT_UE_ROOTS=("/Users/Shared/Epic Games/UE_5.8"); else CI_DEFAULT_UE_ROOTS=(/opt/UnrealEngine-5.8); fi
DRY_RUN=0
ci_resolve_engine
EDITOR="$(find_editor_cmd)" || die "UnrealEditor-Cmd not found under $UE_ROOT"
OUT="${VELOX_OUT:-$REPO_ROOT/Saved/CI}/$(date +%Y%m%d-%H%M%S)-bake"
mkdir -p "$OUT"; LOG="$OUT/bake.log"
SOAK_LOCK="${SOAK_LOCK:-/tmp/velox-soak.lock}"
TMO="${VELOX_BAKE_TIMEOUT:-5400}"

CMD=("$EDITOR" "$VELOX_PROJECT" -run=ResavePackages -BuildLighting "-Quality=$QUALITY" -MapsOnly "-Map=$MAP"
     -AllowCommandletRendering -unattended -nopause -nosplash -stdout -FullStdOutLogOutput)
# shellcheck disable=SC2206
CMD+=(${VELOX_BAKE_EXTRA:-})
WRAP=(timeout --kill-after=60 "$TMO")
if [[ "$PLATFORM" == Linux ]]; then
  CMD+=(-vulkan -RenderOffscreen)
  export VK_ICD_FILENAMES="${VK_ICD_FILENAMES:-/usr/share/vulkan/icd.d/lvp_icd.json}"
  WRAP=(flock -o "$SOAK_LOCK" "${WRAP[@]}")
fi
[[ -x /usr/bin/time ]] && WRAP+=(/usr/bin/time -v)
info "Bake $MAP $QUALITY platform=$PLATFORM log=$LOG"
START=$(date +%s)
set +e
"${WRAP[@]}" "${CMD[@]}" 2>&1 | tee "$LOG"
RC=${PIPESTATUS[0]}
set -e
info "Bake exit=$RC wall=$(( $(date +%s) - START ))s log=$LOG"
FAIL=0
if [[ "$RC" != 0 ]]; then echo "FAILED: commandlet exit $RC" >&2; FAIL=1; fi
if ! /usr/bin/grep -a -q 'LogLightmassSolver\|Lightmass.*[Cc]omplete\|LogStaticLightingSystem.*[Ss]uccess' "$LOG"; then
  echo "FAILED: no Lightmass completion lines in log" >&2; FAIL=1; fi
MAPNAME="${MAP##*/}"
if ! /usr/bin/grep -a -E -q "Saving? .*${MAPNAME}|Saved .*${MAPNAME}|Resaving.*${MAPNAME}|Checking out.*${MAPNAME}|Wrote .*${MAPNAME}" "$LOG"; then
  echo "FAILED: map '$MAPNAME' was not saved (no save line in log)" >&2; FAIL=1; fi
exit "$FAIL"
