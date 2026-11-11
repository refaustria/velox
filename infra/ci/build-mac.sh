#!/usr/bin/env bash
# macOS CI: build Velox Editor/Game targets and run headless automation tests. See infra/ci/README.md.
set -euo pipefail
CI_SCRIPT_NAME="infra/ci/build-mac.sh"
CI_PLATFORM="Mac"
CI_BUILD_SH="Engine/Build/BatchFiles/Mac/Build.sh"
CI_DEFAULT_UE_ROOTS=("/Users/Shared/Epic Games/UE_5.8")
# --bake <Map> <Quality>: Metal lighting bake (ADR 0006 fallback B); same commandlet as bake.sh. Run by the human on the Mac.
if [[ "${1:-}" == "--bake" ]]; then
  shift
  VELOX_BAKE_PLATFORM=Mac exec "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/bake.sh" "$@"
fi
# shellcheck source=infra/ci/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"
ci_main "$@"
