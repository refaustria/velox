#!/usr/bin/env bash
# Linux release packaging (M6 task 01, ADR 0007 section 4): one cook, two archives.
#   Velox-<ver>-Linux.tar.zst        Shipping client (players)
#   Velox-<ver>-LinuxServer.tar.zst  Development build (headless listen-server host)
# See infra/ci/README.md ("Release package").
set -euo pipefail
CI_SCRIPT_NAME="infra/ci/package.sh"
CI_PLATFORM="Linux"
CI_BUILD_SH="Engine/Build/BatchFiles/Linux/Build.sh"
CI_DEFAULT_UE_ROOTS=(/opt/UnrealEngine-5.8)
# shellcheck source=infra/ci/lib.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib.sh"

package_usage() {
  cat <<USAGE
Usage: infra/ci/package.sh [--platform Linux] [--version vX.Y.Z | --dev] [--out DIR] [--skip-build] [--dry-run]

Cooks once, then stages/archives a Shipping client and a Development server package.

  --platform Linux   Only Linux is supported by this script.
  --version vX.Y.Z   Release build. Requires HEAD to be exactly tagged vX.Y.Z and
                     ProjectVersion=X.Y.Z in Config/DefaultGame.ini (else exit 2).
  --dev              Dev build; version is <ProjectVersion>+<short sha>. Prints "DEV BUILD - do not publish".
                     (Without --version/--dev the tag on HEAD is used, as for --version.)
  --out DIR          Output directory (default: \${VELOX_OUT:-Saved/CI}/packages/<ver>/)
  --skip-build       Do not compile (-build); the cook is iterative and reuses Saved/Cooked.
  --dry-run          Print the UAT commands only.
Disk: refuses to start below 12 GB free; deletes Saved/StagedBuilds always and Saved/Cooked only when
free space is below 15 GB after archiving. Environment: UE_ROOT, VELOX_PROJECT, VELOX_OUT (infra/.env),
VELOX_SMOKE_SECONDS (smoke window, default 120, min 30; the server and Development client GC every 20 s in it),
VELOX_SMOKE_PORT (smoke listen port, default 7797).
Exit codes: 0 ok, 1 failure, 2 version rule violated.
USAGE
}

ci_free_gb() { df --output=avail -BG / | tail -1 | tr -dc '0-9'; }

package_main() {
  local platform="Linux" ver_arg="" dev=0 out="" skip_build=0
  DRY_RUN=0
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --platform) [[ $# -ge 2 ]] || die "--platform needs a value"; platform="$2"; shift 2 ;;
      --version)  [[ $# -ge 2 ]] || die "--version needs a value"; ver_arg="$2"; shift 2 ;;
      --dev)      dev=1; shift ;;
      --out)      [[ $# -ge 2 ]] || die "--out needs a value"; out="$2"; shift 2 ;;
      --skip-build) skip_build=1; shift ;;
      --dry-run)  DRY_RUN=1; shift ;;
      -h|--help)  package_usage; exit 0 ;;
      *) package_usage >&2; die "unknown option: $1" ;;
    esac
  done
  [[ "$platform" == Linux ]] || die "only --platform Linux is supported (Mac: M6 task 11, Windows: task 14)"
  cd "$REPO_ROOT"
  load_env_file
  ci_resolve_engine

  # ---- version rule (ADR 0007 section 2)
  local pv sha tag ver
  pv="$(sed -n 's/^ProjectVersion=//p' Config/DefaultGame.ini | head -1 | tr -d '\r ')"
  [[ -n "$pv" ]] || { echo "ERROR: no ProjectVersion in Config/DefaultGame.ini" >&2; exit 2; }
  sha="$(git rev-parse --short HEAD)"
  if [[ "$dev" == 1 ]]; then
    ver="${pv}+${sha}"
    echo "DEV BUILD - do not publish"
  else
    tag="$(git describe --exact-match --tags 2>/dev/null || true)"
    if [[ ! "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]]; then echo "ERROR: HEAD is not exactly tagged vX.Y.Z (got '${tag:-none}'); use --dev for untagged builds" >&2; exit 2; fi
    if [[ -n "$ver_arg" && "$ver_arg" != "$tag" ]]; then echo "ERROR: --version $ver_arg does not match HEAD tag $tag" >&2; exit 2; fi
    if [[ "${tag#v}" != "$pv" ]]; then echo "ERROR: tag $tag does not match ProjectVersion=$pv in Config/DefaultGame.ini" >&2; exit 2; fi
    ver="${tag#v}"
  fi
  [[ -n "$out" ]] || out="${VELOX_OUT:-$REPO_ROOT/Saved/CI}/packages/$ver"
  info "version=$ver out=$out"

  # ---- disk guard
  local free0 free1
  free0="$(ci_free_gb)"
  info "free space before: ${free0} GB"
  if (( free0 < 12 )); then echo "ERROR: only ${free0} GB free, refusing to start below 12 GB" >&2; exit 1; fi

  # UAT stages Velox.sh via a bare `env` call. A non-executable `env` earlier in PATH (e.g. the rustup/uv shell snippet
  # ~/.local/bin/env) makes staging fail with "Permission denied"; drop PATH entries that shadow /usr/bin/env that way.
  local pe newpath="" IFS_SAVE="$IFS"
  IFS=':'; for pe in $PATH; do
    if [[ -e "$pe/env" && ! -x "$pe/env" ]]; then info "PATH: dropping $pe (non-executable env shadows /usr/bin/env)"; continue; fi
    newpath="${newpath:+$newpath:}$pe"
  done; IFS="$IFS_SAVE"; export PATH="$newpath"
  local uat="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
  [[ "$DRY_RUN" == 1 || -x "$uat" ]] || die "RunUAT.sh not found: $uat"
  local common=(-project="$VELOX_PROJECT" -platform=Linux -noP4 -unattended -utf8output -nocompileeditor -stage -pak -compressed -archive)
  local cookargs=(-cook) buildarg=(-build)
  if [[ "$skip_build" == 1 ]]; then buildarg=(); cookargs=(-cook -iterate); fi
  local staged="$REPO_ROOT/Saved/StagedBuilds" logd="$out/logs"
  run mkdir -p "$out" "$logd"
  local t0=$SECONDS t_cook t_srv
  run rm -rf "${staged:?}"

  info "UAT pass 1: Shipping client (build+cook+stage+pak+archive)"
  run_logged "$logd/uat-shipping.log" "$uat" BuildCookRun "${common[@]}" -clientconfig=Shipping "${buildarg[@]}" "${cookargs[@]}" \
    -archivedirectory="$out/Linux" || die "UAT Shipping pass failed (log: $logd/uat-shipping.log)"
  t_cook=$((SECONDS - t0))
  info "pass 1 took ${t_cook}s"
  run rm -rf "${staged:?}"
  t0=$SECONDS
  info "UAT pass 2: Development server package (-skipcook, reuses Saved/Cooked/Linux)"
  run_logged "$logd/uat-server.log" "$uat" BuildCookRun "${common[@]}" -clientconfig=Development "${buildarg[@]}" -skipcook \
    -archivedirectory="$out/LinuxServer" || die "UAT Development pass failed (log: $logd/uat-server.log)"
  t_srv=$((SECONDS - t0))
  info "pass 2 took ${t_srv}s"
  run rm -rf "${staged:?}"
  if [[ "$DRY_RUN" == 1 ]]; then info "dry-run: stopping before archiving"; return 0; fi

  # ---- archives
  local pa="Velox-${ver}-Linux.tar.zst" sa="Velox-${ver}-LinuxServer.tar.zst" srcP srcS
  srcP="$out/Linux"; srcS="$out/LinuxServer"
  [[ -d "$srcP" && -d "$srcS" ]] || die "UAT archive dirs missing under $out"
  # UAT may put a platform dir (Linux/) below -archivedirectory; descend if so.
  if [[ ! -f "$srcP/Velox.sh" && -f "$srcP/Linux/Velox.sh" ]]; then srcP="$srcP/Linux"; fi
  if [[ ! -f "$srcS/Velox.sh" && -f "$srcS/Linux/Velox.sh" ]]; then srcS="$srcS/Linux"; fi
  [[ -f "$srcP/Velox.sh" && -f "$srcS/Velox.sh" ]] || die "Velox.sh not found in archived output ($srcP, $srcS)"
  rm -f "${out:?}/${pa:?}" "${out:?}/${sa:?}"
  # Player: Shipping only, no symbols. Server: Development binary + symbols, minus any Shipping binary.
  tar -C "$srcP" --exclude='*.debug' --exclude='*.sym' --exclude='Velox/Binaries/Linux/Velox' -I 'zstd -T0 -6' -cf "$out/$pa" .
  tar -C "$srcS" --exclude='Velox/Binaries/Linux/Velox-Linux-Shipping*' -I 'zstd -T0 -6' -cf "$out/$sa" .
  local sp ss
  sp="$(stat -c %s "$out/$pa")"; ss="$(stat -c %s "$out/$sa")"
  info "player archive: $pa $((sp/1048576)) MiB; server archive: $sa $((ss/1048576)) MiB"
  local lim=$((2500*1024*1024))
  (( sp <= lim && ss <= lim )) || die "archive exceeds 2.5 GB"
  (cd "$out" && sha256sum "$pa" "$sa" > SHA256SUMS && sha256sum -c SHA256SUMS)
  # The raw UAT archive dirs are no longer needed (large); the tarballs are the product.
  rm -rf "${out:?}/Linux" "${out:?}/LinuxServer"

  # ---- smoke
  local smoke="$out/smoke" nv="unknown" rc=0
  rm -rf "${smoke:?}"; mkdir -p "$smoke/server" "$smoke/client"
  tar -C "$smoke/server" -I zstd -xf "$out/$sa"
  tar -C "$smoke/client" -I zstd -xf "$out/$pa"
  SMOKE_NETVER=""
  ci_smoke "$smoke" || rc=1
  ci_smoke_content "$smoke" || rc=1
  nv="$SMOKE_NETVER"
  rm -rf "${smoke:?}/server" "${smoke:?}/client"
  [[ "$rc" == 0 ]] || die "smoke failed (logs: $smoke/*.log)"

  # ---- manifest
  {
    printf '{\n  "version": "%s",\n  "net_version": "%s",\n  "built_at": "%s",\n  "commit": "%s",\n  "files": [\n' \
      "$ver" "${nv:-unknown}" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$(git rev-parse HEAD)"
    printf '    {"name": "%s", "sha256": "%s", "size": %s},\n' "$pa" "$(sha256sum "$out/$pa" | cut -d' ' -f1)" "$sp"
    printf '    {"name": "%s", "sha256": "%s", "size": %s}\n  ]\n}\n' "$sa" "$(sha256sum "$out/$sa" | cut -d' ' -f1)" "$ss"
  } > "$out/MANIFEST.json"

  # ---- cleanup + disk report
  free1="$(ci_free_gb)"
  if (( free1 < 15 )); then info "free ${free1} GB < 15 GB: deleting Saved/Cooked"; rm -rf "${REPO_ROOT:?}/Saved/Cooked"; free1="$(ci_free_gb)"; fi
  info "free space after: ${free1} GB (before: ${free0} GB)"
  info "pass 1 (build+cook) ${t_cook}s, pass 2 ${t_srv}s; net_version=${nv:-unknown}"
  info "PACKAGE OK: $out"
}

# Start the engine binary DIRECTLY (not via Velox.sh: that wrapper does not exec, so a signal to its PID would orphan the engine).
# ci_smoke_start <bindir-root> <log> <args...> sets SMOKE_PID. Same arguments as Velox.sh passes: <binary> Velox <args>.
ci_smoke_start() {
  local root="$1" log="$2" bin; shift 2
  bin="$(ls "$root"/Velox/Binaries/Linux/Velox "$root"/Velox/Binaries/Linux/Velox-Linux-Shipping 2>/dev/null | head -1)"
  [[ -x "$bin" ]] || chmod +x "$bin"
  ( cd "$root" && exec "$bin" Velox "$@" ) > "$log" 2>&1 &
  SMOKE_PID=$!
}

# ci_smoke_stop <pid> : SIGTERM that PID, wait up to 30 s; KILL after. Sets SMOKE_TERM_S (seconds to exit, 999 = needed KILL).
ci_smoke_stop() {
  local pid="$1" i
  SMOKE_TERM_S=0
  kill -0 "$pid" 2>/dev/null || return 0
  kill -TERM "$pid" 2>/dev/null || true
  for i in $(seq 1 30); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
  SMOKE_TERM_S=$i
  if kill -0 "$pid" 2>/dev/null; then kill -KILL "$pid" 2>/dev/null || true; SMOKE_TERM_S=999; fi
  wait "$pid" 2>/dev/null || true
}

# Smoke (ADR 0007 fact 8, task M6-01b): server archive runs as a headless listen host on port 7797 for VELOX_SMOKE_SECONDS (default
# 120 s); a Development client from the same archive joins it (proves the join handshake and makes the server log its network
# version) and stays for the whole window; the Shipping binary from the player archive runs 20 s with -game -nullrhi and must stay
# alive (a Shipping build has no log and cannot be made to join headless, see the task notes). GC: gc.CollectGarbageEveryFrame stays
# at its default 0; both Development processes force a GC at start (`obj gc`) and run periodic GC every 20 s instead of 61 s, so the
# window holds several GCs after the audio preload, each with VerifyGCAssumptions (Development default). The first periodic GC is
# where a Disregard-for-GC violation used to be fatal (task 01 finding 2). Then every process gets SIGTERM by PID and must exit
# within 30 s. Sets SMOKE_NETVER.
ci_smoke() {
  local d="$1" ok=1 line i spid dpid cpid secs="${VELOX_SMOKE_SECONDS:-120}" port="${VELOX_SMOKE_PORT:-7797}" gcs dgcs
  local slog="$d/server.log" dlog="$d/devclient.log" clog="$d/client.log"
  local gcexec="t.MaxFPS 60, gc.TimeBetweenPurgingPendingKillObjects 20, obj gc"
  (( secs >= 30 )) || secs=30
  info "smoke: Development server on port ${port} (${secs} s, periodic GC 20 s), Development client joining, Shipping client -game (20 s)"
  ci_smoke_start "$d/server" "$slog" "L_ItemTest?listen?game=FFA?SpectatorOnly=1?Name=Server" \
    -nullrhi -nosound -unattended -nopause -nosplash -log -port="$port" -LogCmds="LogNetVersion Log, LogGarbage Log" -UserDir="$d/saved" -ExecCmds="$gcexec"
  spid=$SMOKE_PID
  local tstart=$SECONDS
  for i in $(seq 1 30); do /usr/bin/grep -a -q 'Game Engine Initialized' "$slog" 2>/dev/null && break; kill -0 "$spid" 2>/dev/null || break; sleep 1; done
  ci_smoke_start "$d/server" "$dlog" "127.0.0.1:$port" -nullrhi -nosound -unattended -nopause -nosplash -log -LogCmds="LogVeloxAudio Verbose, LogGarbage Log" \
    -UserDir="$d/saved-devclient" -ExecCmds="$gcexec"
  dpid=$SMOKE_PID
  ci_smoke_start "$d/client" "$clog" /Game/Maps/L_MainMenu -game -nullrhi -nosound -unattended -nopause -nosplash -log -UserDir="$d/saved-client"
  cpid=$SMOKE_PID
  for i in $(seq 1 20); do kill -0 "$cpid" 2>/dev/null || break; sleep 1; done
  local c_alive=0 s_alive=0 d_alive=0
  kill -0 "$cpid" 2>/dev/null && c_alive=1
  ci_smoke_stop "$cpid"
  while (( SECONDS - tstart < secs )); do kill -0 "$spid" 2>/dev/null || break; kill -0 "$dpid" 2>/dev/null || break; sleep 2; done
  kill -0 "$spid" 2>/dev/null && s_alive=1
  kill -0 "$dpid" 2>/dev/null && d_alive=1
  ci_smoke_stop "$dpid"; ci_smoke_stop "$spid"; local sterm=$SMOKE_TERM_S
  /usr/bin/grep -a -q 'Game Engine Initialized' "$slog" || { echo "smoke: no 'Game Engine Initialized' in server log" >&2; ok=0; }
  line="$(/usr/bin/grep -a -E "IpNetDriver listening on port ${port}|LogNet: .*Listening" "$slog" | head -1 || true)"
  if [[ -n "$line" ]]; then info "smoke: listen line: ${line:0:200}"; else echo "smoke: no net driver listen line on ${port}" >&2; ok=0; fi
  /usr/bin/grep -a -q 'Welcomed by server' "$dlog" || { echo "smoke: Development client was not welcomed by the server (see $dlog)" >&2; ok=0; }
  if /usr/bin/grep -a -E 'Error:|ensure|Fatal error' "$slog" | head -5 | /usr/bin/grep -a .; then echo "smoke: Error:/ensure in server log" >&2; ok=0; fi
  if /usr/bin/grep -a -E 'Fatal error|breaking Disregard for GC' "$dlog" | head -5 | /usr/bin/grep -a .; then echo "smoke: fatal in Development client log" >&2; ok=0; fi
  # One "Verify GC Assumptions" line per GC (LogGarbage, Development builds; LogGarbage defaults to Warning, hence -LogCmds);
  # need the forced one plus at least two periodic ones.
  gcs="$(/usr/bin/grep -a -c 'for Verify GC Assumptions' "$slog" || true)"; dgcs="$(/usr/bin/grep -a -c 'for Verify GC Assumptions' "$dlog" || true)"
  (( ${gcs:-0} >= 3 )) || { echo "smoke: only ${gcs:-0} verified GCs in the server log (need >= 3)" >&2; ok=0; }
  (( ${dgcs:-0} >= 3 )) || { echo "smoke: only ${dgcs:-0} verified GCs in the Development client log (need >= 3)" >&2; ok=0; }
  /usr/bin/grep -a -q 'VeloxAudio preloaded [1-9]' "$dlog" || { echo "smoke: Development client did not preload its audio assets (see $dlog)" >&2; ok=0; }
  [[ "$s_alive" == 1 ]] || { echo "smoke: server exited on its own (crash?)" >&2; ok=0; }
  [[ "$d_alive" == 1 ]] || { echo "smoke: Development client exited on its own" >&2; ok=0; }
  [[ "$c_alive" == 1 ]] || { echo "smoke: Shipping client exited on its own within 20 s (see $clog)" >&2; ok=0; }
  (( sterm <= 30 )) || { echo "smoke: server did not exit within 30 s of SIGTERM" >&2; ok=0; }
  # ADR 0007 F2: the version in force is the delegate value (tag hash folded in), not the base 'Checksum:'.
  SMOKE_NETVER="$(/usr/bin/grep -a 'LogNetVersion: Checksum from delegate' "$slog" | /usr/bin/grep -a -o 'delegate: [0-9]*' | head -1 | tr -dc '0-9' || true)"
  if [[ -z "$SMOKE_NETVER" ]]; then
    SMOKE_NETVER="$(/usr/bin/grep -a 'LogNetVersion: Velox' "$slog" | /usr/bin/grep -a -o 'Checksum: [0-9]*' | head -1 | tr -dc '0-9' || true)"
    echo "smoke: NOTE no 'Checksum from delegate' line in the server log; net_version falls back to the base engine checksum '${SMOKE_NETVER:-none}' (tag hash not covered)" >&2
  fi
  info "smoke: ${secs} s window; verified GCs server=${gcs:-0} devclient=${dgcs:-0}; server exit ${sterm}s after SIGTERM; net version (delegate) ='${SMOKE_NETVER:-none}'; Shipping client log lines: $(wc -l < "$clog")"
  [[ "$ok" == 1 ]]
}

# Content smoke (M6 task 02b, findings B2 and decision 51): the cooked server must load L_DM1 and every bot preset. Bot presets are
# resolved by string (?BotDifficulty=<name>), so a missing cook entry only shows at runtime ("Failed to find object '/Game/Bots/DA_Bot_X'",
# "VeloxBot url bots not added"). One short listen-host run per preset on L_DM1, each must log "VeloxBot add ... difficulty=DA_Bot_<P>".
ci_smoke_content() {
  local d="$1" ok=1 p slog spid i port="${VELOX_SMOKE_PORT:-7797}"
  port=$((port + 1))
  for p in Easy Medium Hard Expert; do
    slog="$d/content-$p.log"
    ci_smoke_start "$d/server" "$slog" "L_DM1?listen?game=FFA?Bots=2?BotDifficulty=$p?SpectatorOnly=1?Name=Server" \
      -nullrhi -nosound -unattended -nopause -nosplash -log -port="$port" -UserDir="$d/saved-content-$p" -ExecCmds="t.MaxFPS 60"
    spid=$SMOKE_PID
    for i in $(seq 1 60); do
      /usr/bin/grep -a -q "VeloxBot add .*difficulty=DA_Bot_$p" "$slog" 2>/dev/null && break
      kill -0 "$spid" 2>/dev/null || break
      sleep 1
    done
    sleep 5
    ci_smoke_stop "$spid"
    if /usr/bin/grep -a -q "VeloxBot add .*difficulty=DA_Bot_$p" "$slog"; then
      info "smoke content: BotDifficulty=$p ok: $(/usr/bin/grep -a -m1 "VeloxBot add .*difficulty=DA_Bot_$p" "$slog" | cut -c1-200)"
    else
      echo "smoke content: no bot with difficulty DA_Bot_$p on L_DM1 (see $slog): $(/usr/bin/grep -a -m2 -E "Failed to find object|bots not added" "$slog" | tr '\n' ' ')" >&2; ok=0
    fi
    /usr/bin/grep -a -q "Bringing World /Game/Maps/DM1/L_DM1\.L_DM1 up for play" "$slog" || { echo "smoke content: L_DM1 was not loaded ($p, see $slog)" >&2; ok=0; }
    if /usr/bin/grep -a -E 'Fatal error|Ensure condition failed' "$slog" | head -3 | /usr/bin/grep -a .; then echo "smoke content: fatal/ensure in $slog" >&2; ok=0; fi
  done
  [[ "$ok" == 1 ]]
}

package_main "$@"
