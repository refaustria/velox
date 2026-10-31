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
    # shellcheck disable=SC2086
    "$editor" "$VELOX_PROJECT" -run=pythonscript -script="$REPO_ROOT/Tools/editor/audit_map.py $args" \
      -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput > "$out" 2>&1 || rc=$?
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
