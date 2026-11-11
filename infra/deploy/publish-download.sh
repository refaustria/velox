#!/usr/bin/env bash
# Publish client archives into the web root (task m6-online/06). Run as root (the downloads dir is root-owned):
#   sudo infra/deploy/publish-download.sh <ver> <file>...
# Copies the files to <webroot>/downloads/<ver>/ (rsync, mode 0644), rewrites that directory's SHA256SUMS, regenerates
# downloads/latest.json (schema ADR 0007 section 3) and downloads/index.html (infra/www/downloads-index.template.html),
# and prunes to the two newest versions. nginx needs no reload. The server archive (Development build) is refused unless
# --include-server is given: it is not a player download.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WEBROOT="${VELOX_WEBROOT:-/var/www/games.inceptum.at}"
DL="$WEBROOT/downloads"
KEEP=2
include_server=0
ver=""; files=()

die() { echo "ERROR: $*" >&2; exit 1; }

while (( $# )); do
  case "$1" in
    --include-server) include_server=1; shift ;;
    -h|--help) sed -n '2,8p' "$0"; exit 0 ;;
    -*) die "unknown option $1" ;;
    *) if [[ -z "$ver" ]]; then ver="$1"; else files+=("$1"); fi; shift ;;
  esac
done
[[ -n "$ver" && ${#files[@]} -gt 0 ]] || die "usage: publish-download.sh <ver> <file>..."
[[ "$ver" =~ ^[A-Za-z0-9._+-]+$ ]] || die "bad version string '$ver'"
[[ -d "$DL" ]] || die "$DL missing"
if [[ -z "${VELOX_WEBROOT:-}" && $EUID -ne 0 ]]; then die "must run as root (sudo)"; fi

for f in "${files[@]}"; do
  [[ -f "$f" ]] || die "no such file: $f"
  case "$(basename "$f")" in
    *LinuxServer*) (( include_server )) || die "$(basename "$f") is the server build, not a player download (use --include-server to force)" ;;
    "Velox-${ver}-Linux.tar.zst"|"Velox-${ver}-macOS.zip"|"Velox-${ver}-Windows.zip") ;;
    *) die "unexpected file name $(basename "$f") (want Velox-${ver}-{Linux.tar.zst,macOS.zip,Windows.zip})" ;;
  esac
done

dest="$DL/$ver"
echo "ROOT: create $dest and copy ${#files[@]} file(s) (rsync --chmod=F644,D755)"
mkdir -p "$dest"
rsync -t --chmod=F644,D755 -- "${files[@]}" "$dest/"

echo "ROOT: write $dest/SHA256SUMS"
(cd "$dest" && find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\n' | sort | xargs sha256sum > SHA256SUMS.tmp && mv SHA256SUMS.tmp SHA256SUMS && chmod 0644 SHA256SUMS)

echo "ROOT: prune to the $KEEP newest versions under $DL"
mapfile -t versions < <(find "$DL" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort -V -r)
for (( i = KEEP; i < ${#versions[@]}; i++ )); do
  echo "ROOT: remove old version ${versions[$i]}"
  rm -rf -- "${DL:?}/${versions[$i]}"
done

echo "ROOT: regenerate $DL/latest.json and $DL/index.html"
python3 -I "$REPO/infra/deploy/publish-index.py" "$DL" "$REPO/infra/www/downloads-index.template.html"

echo "Published $ver:"; ls -l "$dest"
