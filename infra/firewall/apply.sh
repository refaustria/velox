#!/usr/bin/env bash
# Apply infra/firewall/nftables.conf with an armed rollback (task m6-online/06). Run as root, ONLY with the human's word.
#   sudo infra/firewall/apply.sh [--check] [--timeout 10min]     # --check: validate only (nft -c), change nothing
#   sudo infra/firewall/apply.sh --confirm                       # cancel the pending rollback and persist the rules
#   sudo infra/firewall/apply.sh --rollback                      # remove the table now
# Flow: validate -> arm a transient systemd timer that deletes our table after the timeout -> load the rules -> you verify SSH
# from a SECOND session -> --confirm. If you lose SSH, the timer removes the table and access returns by itself.
# (No `at` on this host; systemd-run timers are used.) Only table inet velox_fw is touched; docker's tables are not.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RULES="$HERE/nftables.conf"
NFT="$(command -v nft || echo /usr/sbin/nft)"
UNIT=velox-fw-rollback
TIMEOUT=10min
mode=apply

while (( $# )); do
  case "$1" in
    --check) mode=check; shift ;;
    --confirm) mode=confirm; shift ;;
    --rollback) mode=rollback; shift ;;
    --timeout) TIMEOUT="${2:?}"; shift 2 ;;
    -h|--help) sed -n '2,9p' "$0"; exit 0 ;;
    *) echo "unknown option $1" >&2; exit 64 ;;
  esac
done

say() { echo "ROOT: $*"; }
[[ $EUID -eq 0 ]] || { echo "must run as root (sudo)" >&2; exit 1; }

case "$mode" in
  check)
    say "validate $RULES ($NFT -c -f, no change)"
    "$NFT" -c -f "$RULES" && echo "ruleset valid"
    ;;
  rollback)
    say "delete table inet velox_fw and stop the rollback timer"
    systemctl stop "$UNIT.timer" 2>/dev/null || true
    "$NFT" delete table inet velox_fw 2>/dev/null || true
    ;;
  confirm)
    say "cancel the rollback timer $UNIT.timer"
    systemctl stop "$UNIT.timer" 2>/dev/null || true
    say "persist: install $RULES as /etc/nftables.conf (old file saved as /etc/nftables.conf.pre-velox) and enable nftables.service"
    [[ -e /etc/nftables.conf.pre-velox ]] || cp -p /etc/nftables.conf /etc/nftables.conf.pre-velox
    install -m 0644 -o root -g root "$RULES" /etc/nftables.conf
    systemctl enable nftables.service
    echo "Persisted. The ruleset is live now and is reloaded at boot by nftables.service."
    ;;
  apply)
    say "validate $RULES (nft -c -f)"
    "$NFT" -c -f "$RULES"
    say "arm rollback: in $TIMEOUT run '$NFT delete table inet velox_fw' (timer $UNIT.timer)"
    systemctl stop "$UNIT.timer" 2>/dev/null || true
    systemd-run --unit="$UNIT" --on-active="$TIMEOUT" --description="Velox firewall rollback" \
      /bin/sh -c "$NFT delete table inet velox_fw"
    say "load $RULES (nft -f): inbound filtering starts NOW"
    "$NFT" -f "$RULES"
    cat <<MSG

Rules are live; rollback fires in $TIMEOUT. From a SECOND terminal verify now:
  ssh <this host>                    # new session must connect
  curl -sI https://games.inceptum.at/ | head -1
Then:  sudo $0 --confirm              (cancels the rollback and persists the rules)
Or:    sudo $0 --rollback             (removes the table immediately)
MSG
    ;;
esac
