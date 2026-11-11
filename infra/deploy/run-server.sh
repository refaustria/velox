#!/usr/bin/env bash
# ExecStart wrapper of velox-server@.service (ADR 0007 sections 1-3). Usage: run-server.sh <instance>
# Builds the command line from the instance env file (loaded by systemd) and exec's the engine so systemd sees the real PID.
# The join password is NOT handled here: VELOX_SERVER_PASSWORD stays in the environment and the game reads it itself,
# so it never shows up in argv, `ps` or the engine's command-line log line.
set -euo pipefail

inst="${1:?usage: run-server.sh <instance>}"
[[ "$inst" =~ ^[a-z0-9_-]+$ ]] || { echo "run-server: bad instance name '$inst'" >&2; exit 64; }

mode="${VELOX_SERVER_MODE:-listen}"
map="${VELOX_MAP:-L_DM1}"
game="${VELOX_MODE:-FFA}"
port="${VELOX_PORT:-7777}"
maxplayers="${VELOX_MAXPLAYERS:-16}"
maxspectators="${VELOX_MAXSPECTATORS:-5}"   # +1 for the headless host's own spectator slot
botopts="${VELOX_BOT_OPTIONS:-}"
name="${VELOX_SERVER_NAME:-Velox}"
release="${VELOX_RELEASE_DIR:-/srv/velox/current}"
saved="${VELOX_SAVED_ROOT:-/srv/velox/saved}/$inst"

[[ "$port" =~ ^[0-9]+$ ]] || { echo "run-server: VELOX_PORT not numeric" >&2; exit 64; }
[[ "$map$game$maxplayers$maxspectators$botopts" != *[[:space:]]* ]] || { echo "run-server: URL options must not contain whitespace" >&2; exit 64; }
[[ -z "$botopts" || "$botopts" == \?* ]] || { echo "run-server: VELOX_BOT_OPTIONS must start with '?'" >&2; exit 64; }

mkdir -p "$saved"

common=(
  -nullrhi -nosound -unattended -nopause -nosplash -log
  "-port=${port}"
  "-UserDir=${saved}"
  "-ExecCmds=t.MaxFPS 60"
  "-ServerId=${inst}"
  "-ServerName=${name}"
  -notraceserver   # TraceAuxiliary.cpp: skips FTraceServerControls::Start() (no UnrealTraceServer fork)
  "-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:InitialConnectTimeout=30"
  "-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ConnectionTimeout=20"
  -NoVerifyGC
)

case "$mode" in
  listen)
    # Server-side URL. The listen host's own player is spectator-only (never plays, never counts towards MaxPlayers).
    url="${map}?listen?game=${game}?SpectatorOnly=1?Name=Server?MaxPlayers=${maxplayers}?MaxSpectators=${maxspectators}${botopts}"
    bin="${release}/Velox/Binaries/Linux/Velox"
    [[ -x "$bin" ]] || { echo "run-server: $bin missing or not executable (release.sh not run?)" >&2; exit 66; }
    # Start the binary directly: Velox.sh does not exec and would orphan the engine on SIGTERM.
    exec "$bin" "$url" "${common[@]}"
    ;;
  editor)
    ue="${UE_ROOT:-/opt/UnrealEngine-5.8}"
    proj="${VELOX_PROJECT_DIR:?editor mode needs VELOX_PROJECT_DIR (a checkout with Velox.uproject)}"
    # ADR 0007 1b: dedicated -server through the editor binary; no listen/SpectatorOnly options.
    url="${map}?game=${game}?MaxPlayers=${maxplayers}?MaxSpectators=${maxspectators}${botopts}"
    exec "${ue}/Engine/Binaries/Linux/UnrealEditor-Cmd" "${proj}/Velox.uproject" "$url" -server "${common[@]}"
    ;;
  *)
    echo "run-server: VELOX_SERVER_MODE must be listen or editor (got '$mode')" >&2
    exit 64
    ;;
esac
