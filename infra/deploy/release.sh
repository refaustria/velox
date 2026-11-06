#!/usr/bin/env bash
# Install a LinuxServer archive as a release and switch the "current" symlink (task m6-online/06). Run as root.
#   sudo infra/deploy/release.sh <ver> <Velox-<ver>-LinuxServer.tar.zst> [--no-restart]
#   sudo infra/deploy/release.sh --switch <ver>        # roll back/forward to an installed release
# The archive's SHA256SUMS (same directory) is verified first. Releases live in /srv/velox/releases/<ver> (root:velox, read-only);
# the two newest are kept (the one in use is never pruned). Enabled velox-server@* instances are restarted unless --no-restart.
# Every root action is announced with "ROOT:" before it runs.
set -euo pipefail

ROOT="${VELOX_ROOT:-/srv/velox}"
FLOOR_GB="${VELOX_FLOOR_GB:-15}"
TESTMODE="${VELOX_DEPLOY_NOROOT:-0}"   # 1 = skip chown/systemctl (offline test of the file logic as a normal user)

die() { echo "ERROR: $*" >&2; exit 1; }
say() { echo "ROOT: $*"; }

restart=1; switch_only=0; ver=""; archive=""
while (( $# )); do
  case "$1" in
    --no-restart) restart=0; shift ;;
    --switch) switch_only=1; ver="${2:?--switch needs a version}"; shift 2 ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    -*) die "unknown option $1" ;;
    *) if [[ -z "$ver" ]]; then ver="$1"; elif [[ -z "$archive" ]]; then archive="$1"; else die "too many arguments"; fi; shift ;;
  esac
done
[[ -n "$ver" ]] || die "usage: release.sh <ver> <archive> | --switch <ver>"
[[ "$ver" =~ ^[A-Za-z0-9._+-]+$ ]] || die "bad version string '$ver'"
if [[ "$TESTMODE" != 1 && $EUID -ne 0 ]]; then die "must run as root (sudo)"; fi

rel="$ROOT/releases"
[[ -d "$rel" ]] || die "$rel missing (run install-server.sh first)"

enabled_instances() {
  systemctl list-unit-files 'velox-server@*.service' --state=enabled --no-legend 2>/dev/null | awk '{print $1}' | grep -v '^velox-server@\.service$' || true
}

switch_to() { # <ver>
  local target="$rel/$1" prev=""
  [[ -x "$target/Velox/Binaries/Linux/Velox" ]] || die "$target is not a valid release"
  prev="$(readlink -f "$ROOT/current" 2>/dev/null || true)"
  say "switch $ROOT/current -> releases/$1 (atomic: ln -sfn + mv -T)"
  ln -sfn "releases/$1" "$ROOT/current.new"
  mv -T "$ROOT/current.new" "$ROOT/current"
  if (( restart )) && [[ "$TESTMODE" != 1 ]]; then
    local u
    while read -r u; do
      [[ -n "$u" ]] || continue
      say "restart $u"
      systemctl restart "$u"
    done < <(enabled_instances)
  fi
  echo "Now running: releases/$1"
  if [[ -n "$prev" && "$prev" != "$target" ]]; then
    echo "Rollback:    sudo $0 --switch $(basename "$prev")"
  fi
}

list_releases() { # newest first
  find "$rel" -mindepth 1 -maxdepth 1 -type d ! -name '*.tmp' -printf '%T@ %p\n' | sort -rn | cut -d' ' -f2-
}

prune() {
  local cur keep=2 n=0 d
  cur="$(readlink -f "$ROOT/current")"
  # newest first by directory mtime; always keep the one in use, then fill up to $keep
  while read -r d; do
    [[ -d "$d" ]] || continue
    if [[ "$(readlink -f "$d")" == "$cur" ]]; then n=$((n+1)); continue; fi
    if (( n < keep )); then n=$((n+1)); continue; fi
    say "prune old release $d"
    chmod -R u+w "$d"
    rm -rf -- "$d"
  done < <(list_releases)
}

if (( switch_only )); then
  switch_to "$ver"
  exit 0
fi

[[ -n "$archive" && -f "$archive" ]] || die "archive '$archive' not found"
adir="$(cd "$(dirname "$archive")" && pwd)"; abase="$(basename "$archive")"

echo "== verify checksum"
[[ -f "$adir/SHA256SUMS" ]] || die "$adir/SHA256SUMS missing (package.sh writes it next to the archives)"
(cd "$adir" && grep -F -- " $abase" SHA256SUMS | sha256sum -c -) || die "sha256 mismatch or archive not listed"

echo "== disk floor"
size_b="$(stat -c %s "$archive")"
need_gb=$(( (size_b * 3) / 1073741824 + 1 ))   # extraction estimate: archive x3 (pak data compresses poorly; this is conservative)
free_gb="$(df -BG --output=avail "$rel" | tail -1 | tr -dc 0-9)"
echo "archive $((size_b/1048576)) MiB, estimated extraction ${need_gb} GB, free ${free_gb} GB, floor ${FLOOR_GB} GB"
(( free_gb - need_gb >= FLOOR_GB )) || die "would leave $((free_gb - need_gb)) GB free, below the ${FLOOR_GB} GB floor"

if [[ -d "$rel/$ver" ]]; then
  echo "release $ver already installed (idempotent): switching only"
else
  tmp="$rel/$ver.tmp"
  say "extract $abase into $tmp"
  rm -rf -- "$tmp"; mkdir -p "$tmp"
  tar -C "$tmp" -I zstd -xf "$archive"
  [[ -x "$tmp/Velox/Binaries/Linux/Velox" ]] || die "archive has no Velox/Binaries/Linux/Velox"
  if [[ "$TESTMODE" != 1 ]]; then
    say "chown -R root:velox $tmp"; chown -R root:velox "$tmp"
  fi
  say "make release read-only (chmod -R a-w, a+rX)"
  chmod -R a-w "$tmp"; chmod -R a+rX "$tmp"
  say "rename $tmp -> $rel/$ver"
  mv -T "$tmp" "$rel/$ver"
fi

switch_to "$ver"
prune
echo "Installed releases:"; list_releases
