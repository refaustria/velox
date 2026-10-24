#!/usr/bin/env bash
# Stream-install the prebuilt Unreal Engine 5.8 Linux editor without storing the zip.
# Usage: install-ue-linux.sh [--force] <download-url> [dest=/opt/UnrealEngine-5.8]
set -euo pipefail

REQUIRED_GB=52
MIN_ARCHIVE_GB=5
FORCE=0
ARGS=()
for a in "$@"; do
  case "$a" in
    --force) FORCE=1 ;;
    -h|--help) sed -n '2,3p' "$0"; exit 0 ;;
    *) ARGS+=("$a") ;;
  esac
done
if [ "${#ARGS[@]}" -lt 1 ] || [ "${#ARGS[@]}" -gt 2 ]; then
  echo "Usage: $0 [--force] <download-url> [dest=/opt/UnrealEngine-5.8]" >&2
  exit 2
fi
URL="${ARGS[0]}"
DEST="${ARGS[1]:-/opt/UnrealEngine-5.8}"

LOG="$HOME/ue-install-$(date +%F).log"
exec > >(tee -a "$LOG") 2>&1
echo "== UE install $(date -Is) -> $DEST (log: $LOG)"

die() { echo "ERROR: $*" >&2; exit 1; }

# --- Pre-flight -------------------------------------------------------------
for tool in curl bsdtar df; do
  command -v "$tool" >/dev/null || die "'$tool' not found (sudo apt-get install -y curl libarchive-tools)"
done

if [ ! -d "$DEST" ]; then
  parent="$(dirname "$DEST")"
  [ -d "$parent" ] || die "parent directory '$parent' does not exist; create it first (sudo mkdir -p '$parent' && sudo chown \$USER '$parent')"
  [ -w "$parent" ] || die "cannot create '$DEST': '$parent' is not writable"
  mkdir -p "$DEST"
fi
[ -w "$DEST" ] || die "'$DEST' is not writable by $(id -un)"

if [ -n "$(ls -A "$DEST" 2>/dev/null)" ]; then
  if [ "$FORCE" -eq 1 ]; then
    echo "WARNING: '$DEST' is not empty; --force given, emptying it."
    find "$DEST" -mindepth 1 -maxdepth 1 -exec rm -rf -- {} +
  else
    die "'$DEST' is not empty. Re-run with --force to wipe it first."
  fi
fi

free_kb="$(df -Pk "$DEST" | awk 'NR==2{print $4}')"
free_gb=$((free_kb / 1024 / 1024))
echo "Free space on $DEST filesystem: ${free_gb} GB (need >= ${REQUIRED_GB} GB)"
[ "$free_gb" -ge "$REQUIRED_GB" ] || die "not enough free disk space (${free_gb} GB < ${REQUIRED_GB} GB)"

# --- HEAD check -------------------------------------------------------------
echo "Checking URL with HEAD request..."
headers="$(curl -sIL --max-time 60 "$URL")" || die "HEAD request failed (network error or expired link)"
status="$(printf '%s\n' "$headers" | awk '/^HTTP/{s=$2} END{print s}')"
case "$status" in 2??) ;; *) die "server answered HTTP '${status:-none}' (link expired or invalid?)" ;; esac
len="$(printf '%s\n' "$headers" | tr -d '\r' | awk 'tolower($1)=="content-length:"{l=$2} END{print l}')"
[ -n "$len" ] || die "no Content-Length in response; refusing to continue blind"
len_gb=$((len / 1024 / 1024 / 1024))
fname="$(printf '%s\n' "$headers" | tr -d '\r' | sed -n 's/.*filename="\{0,1\}\([^";]*\).*/\1/Ip' | tail -n1)"
echo "HTTP $status, size: ${len_gb} GB ($len bytes), filename: ${fname:-<unknown, from URL: ${URL%%\?*}>}"
[ "$len_gb" -ge "$MIN_ARCHIVE_GB" ] || die "archive only ${len_gb} GB (< ${MIN_ARCHIVE_GB} GB): probably a login/redirect page, not the engine"

# --- Stream download into bsdtar -------------------------------------------
echo "Downloading and extracting (no zip is stored on disk). This takes an hour or more."
echo "If interrupted: re-run with --force (resume is impossible through a pipe)."
if command -v pv >/dev/null; then
  curl -fsSL --retry 5 --retry-delay 10 "$URL" | pv -s "$len" | bsdtar -xf - -C "$DEST"
else
  curl -fL --retry 5 --retry-delay 10 --progress-bar "$URL" | bsdtar -xf - -C "$DEST"
fi

# --- Post-extract -----------------------------------------------------------
if [ ! -d "$DEST/Engine" ]; then
  mapfile -t entries < <(find "$DEST" -mindepth 1 -maxdepth 1)
  if [ "${#entries[@]}" -eq 1 ] && [ -d "${entries[0]}" ]; then
    echo "Flattening top-level directory ${entries[0]}"
    top="${entries[0]}"
    find "$top" -mindepth 1 -maxdepth 1 -exec mv -t "$DEST" {} +
    rmdir "$top"
  fi
fi

EDITOR_BIN="$DEST/Engine/Binaries/Linux/UnrealEditor"
BUILD_SH="$DEST/Engine/Build/BatchFiles/Linux/Build.sh"
[ -e "$EDITOR_BIN" ] || die "missing $EDITOR_BIN"
[ -e "$BUILD_SH" ] || die "missing $BUILD_SH"
for f in "$EDITOR_BIN" "$DEST/Engine/Binaries/Linux/UnrealEditor-Cmd" "$BUILD_SH" \
         "$DEST"/Engine/Build/BatchFiles/Linux/*.sh "$DEST/Engine/Binaries/ThirdParty/DotNet"/*/linux-x64/dotnet; do
  [ -f "$f" ] && chmod +x "$f"
done

echo "--- Engine/Build/Build.version"
cat "$DEST/Engine/Build/Build.version" 2>/dev/null || echo "(missing)"
if [ -e "$DEST/Engine/Build/InstalledBuild.txt" ]; then
  echo "InstalledBuild.txt: present (installed build)"
else
  echo "InstalledBuild.txt: MISSING (not an installed build?)"
fi

cat <<MSG

Install complete. Next step (headless smoke check, no GPU):
  $DEST/Engine/Binaries/Linux/UnrealEditor-Cmd <project>.uproject -nullrhi -unattended -nopause -run=...
  e.g. infra/ci/build.sh with UE_ROOT=$DEST
MSG
