#!/usr/bin/env bash
# macOS CI: build Velox Editor/Game targets and run headless automation tests. See infra/ci/README.md.
set -euo pipefail
CI_SCRIPT_NAME="infra/ci/build-mac.sh"
CI_PLATFORM="Mac"
CI_BUILD_SH="Engine/Build/BatchFiles/Mac/Build.sh"
CI_DEFAULT_UE_ROOTS=("/Users/Shared/Epic Games/UE_5.8")
# macOS ships no coreutils `timeout` (lib.sh ci_tests uses it): prefer Homebrew's gtimeout, else run unguarded.
if ! command -v timeout >/dev/null 2>&1; then
  if command -v gtimeout >/dev/null 2>&1; then
    timeout() { gtimeout "$@"; }
  else
    echo "[ci] WARNING: no 'timeout' or 'gtimeout' found (brew install coreutils); test run will not be time-limited" >&2
    timeout() { shift 2; "$@"; }  # drop '--kill-after=60 <seconds>'
  fi
fi
# shellcheck source=infra/ci/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"
ci_main "$@"
