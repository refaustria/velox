#!/usr/bin/env bash
# Linux CI: build Velox targets and run headless automation tests. See infra/ci/README.md.
set -euo pipefail
CI_SCRIPT_NAME="infra/ci/build.sh"
CI_PLATFORM="Linux"
CI_BUILD_SH="Engine/Build/BatchFiles/Linux/Build.sh"
CI_DEFAULT_UE_ROOTS=(/opt/UnrealEngine-5.8)
# shellcheck source=infra/ci/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"
ci_main "$@"
