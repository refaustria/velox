#!/usr/bin/env bash
# Install the Velox service layout on this host (task m6-online/06, phase B). Run as root: sudo infra/deploy/install-server.sh
# Idempotent: a re-run changes nothing. Every root action is printed ("ROOT: ...") BEFORE it is executed.
# Creates users, directories and installs units + tmpfiles. Does NOT enable/start anything and does NOT touch the firewall or nginx.
#
# Options: --dry-run             print the actions, change nothing (also works as a normal user)
#          --master-binary PATH  veloxmaster binary to install (default infra/master/target/release/veloxmaster)
#          --public-address IP   VELOXMASTER_PUBLIC_ADDRESS for a newly created /etc/veloxmaster.env (default 77.244.242.34)
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
DRY=0
MASTER_BIN="$REPO/infra/master/target/release/veloxmaster"
PUBLIC_ADDR="77.244.242.34"
WEBROOT="${VELOX_WEBROOT:-/var/www/games.inceptum.at}"

while (( $# )); do
  case "$1" in
    --dry-run) DRY=1; shift ;;
    --master-binary) MASTER_BIN="${2:?}"; shift 2 ;;
    --public-address) PUBLIC_ADDR="${2:?}"; shift 2 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 64 ;;
  esac
done

if (( DRY == 0 && EUID != 0 )); then echo "must run as root (sudo), or pass --dry-run" >&2; exit 1; fi

act() { # act "<what and why>" cmd args...
  local why="$1"; shift
  echo "ROOT: $why"
  echo "      \$ $*"
  (( DRY )) || "$@"
}
ok() { echo "ok:   $*"; }

ensure_group() { # name
  if getent group "$1" >/dev/null; then ok "group $1 exists"; else act "create system group $1" groupadd --system "$1"; fi
}
ensure_user() { # name home
  if id "$1" >/dev/null 2>&1; then ok "user $1 exists"
  else act "create unprivileged system user $1 (home $2, no login shell)" \
         useradd --system --gid "$1" --home-dir "$2" --no-create-home --shell /usr/sbin/nologin "$1"; fi
}
ensure_dir() { # path owner:group mode
  local p="$1" og="$2" mode="$3" cur=""
  if [[ -d "$p" ]]; then cur="$(stat -c '%U:%G %a' "$p")"; fi
  local want_mode="${mode#0}"; want_mode="${want_mode#0}"
  if [[ "$cur" == "$og ${mode#0}" || "$cur" == "$og $want_mode" ]]; then ok "dir $p $og $mode"
  else act "directory $p ($og, mode $mode)" install -d -o "${og%%:*}" -g "${og##*:}" -m "$mode" "$p"; fi
}
ensure_file() { # src dst owner:group mode [create-only]
  local src="$1" dst="$2" og="$3" mode="$4" only_new="${5:-}"
  if [[ -e "$dst" && "$only_new" == create-only ]]; then ok "file $dst exists (kept, never overwritten)"; return; fi
  if [[ -f "$dst" ]] && cmp -s "$src" "$dst" && [[ "$(stat -c '%U:%G %a' "$dst")" == "$og ${mode#0}" ]]; then ok "file $dst up to date"; return; fi
  act "install $dst from ${src#"$REPO"/} ($og, mode $mode)" install -o "${og%%:*}" -g "${og##*:}" -m "$mode" "$src" "$dst"
}

echo "== users and groups"
ensure_group velox
ensure_user velox /srv/velox
ensure_group veloxmaster
ensure_user veloxmaster /srv/veloxmaster

echo "== game server tree /srv/velox"
ensure_dir /srv/velox root:root 0755
ensure_dir /srv/velox/releases root:root 0755
ensure_dir /srv/velox/bin root:root 0755
ensure_dir /srv/velox/config root:velox 0750
ensure_dir /srv/velox/saved velox:velox 0750
ensure_file "$REPO/infra/deploy/run-server.sh" /srv/velox/bin/run-server.sh root:root 0755
ensure_file "$REPO/infra/systemd/velox-server.env.example" /srv/velox/config/ffa.env root:velox 0640 create-only

echo "== veloxmaster"
ensure_dir /srv/veloxmaster root:root 0755
if [[ -x "$MASTER_BIN" ]]; then
  ensure_file "$MASTER_BIN" /srv/veloxmaster/veloxmaster root:root 0755
else
  echo "warn: $MASTER_BIN not built (cd infra/master && cargo build --release); veloxmaster binary NOT installed"
fi
if [[ -e /etc/veloxmaster.env ]]; then ok "file /etc/veloxmaster.env exists (kept)"
else
  tmp="$(mktemp)"; trap 'rm -f "$tmp"' EXIT
  sed "s/^VELOXMASTER_PUBLIC_ADDRESS=.*/VELOXMASTER_PUBLIC_ADDRESS=${PUBLIC_ADDR}/" "$REPO/infra/master/veloxmaster.env.example" > "$tmp"
  ensure_file "$tmp" /etc/veloxmaster.env root:veloxmaster 0640 create-only
fi

echo "== status directory (veloxmaster writes servers.json, nginx reads it)"
ensure_dir "$WEBROOT/status" veloxmaster:www-data 2755

echo "== systemd units, drop-in, tmpfiles"
ensure_file "$REPO/infra/systemd/velox-server@.service" /etc/systemd/system/velox-server@.service root:root 0644
ensure_file "$REPO/infra/master/veloxmaster.service" /etc/systemd/system/veloxmaster.service root:root 0644
ensure_dir /etc/systemd/system/veloxmaster.service.d root:root 0755
ensure_file "$REPO/infra/systemd/veloxmaster.service.d/10-hardening.conf" /etc/systemd/system/veloxmaster.service.d/10-hardening.conf root:root 0644
ensure_file "$REPO/infra/systemd/velox-tmpfiles.conf" /etc/tmpfiles.d/velox.conf root:root 0644

# daemon-reload is cheap and harmless; only skipped on --dry-run. systemd-tmpfiles --create only creates/ages, never starts anything.
act "reload systemd unit files" systemctl daemon-reload

cat <<'NEXT'

Done. Nothing was enabled or started and the firewall/nginx were not touched. Next (each is a separate, announced root step):
  1. sudo systemctl enable --now veloxmaster                         # then: curl -s 127.0.0.1:8790/healthz
  2. sudo infra/deploy/release.sh <ver> <Velox-ver-LinuxServer.tar.zst>
  3. edit /srv/velox/config/ffa.env if needed (password: VELOX_SERVER_PASSWORD there, never on a command line)
  4. sudo systemctl enable --now velox-server@ffa                    # then: journalctl -u velox-server@ffa -f
  5. firewall: infra/firewall/README.md (armed rollback first)
NEXT
