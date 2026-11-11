# Firewall (nftables, task m6-online/06, human decision q44)

`nftables.conf` owns one table, `inet velox_fw`, with an `input` chain of policy drop. Allowed: established/related, loopback, docker bridges (`docker0`, `br-*`, container-to-host only), ICMP/ICMPv6 (echo rate-limited, ND allowed), DHCP replies, TCP 22/80/443, UDP 7777. Everything else is dropped, including TCP 8765 and 3000 (closed on purpose). It never flushes the ruleset, so docker's tables are untouched; `forward`/`output` are not filtered. Docker publishes only on 127.0.0.1 here, so published ports are unaffected.

More instances: add a `udp dport <port> accept` line (commented example in the file) and reload.

## Apply (only with the human's word; `apply.sh` prints each root action first)

```
sudo infra/firewall/apply.sh --check      # nft -c -f, no change
sudo infra/firewall/apply.sh              # validate, arm a 10 min rollback timer (systemd-run, no `at` on this host), load the rules
# from a SECOND terminal: ssh in again, curl -sI https://games.inceptum.at/ | head -1
sudo infra/firewall/apply.sh --confirm    # cancel the rollback, install as /etc/nftables.conf (old saved as .pre-velox), enable nftables.service
sudo infra/firewall/apply.sh --rollback   # alternative: remove the table now
```

If the session is lost, the timer `velox-fw-rollback.timer` deletes the table after 10 minutes and access returns. `--timeout 30min` changes the delay.

Note: persisting replaces the Debian default `/etc/nftables.conf` (`flush ruleset` plus empty accept chains); the new file has no `flush`, so a later `systemctl reload nftables` cannot wipe docker's rules. A provider-side firewall, if any, must also allow UDP 7777 (nothing in this repo can check that).

Verify: `sudo nft list ruleset`, `ss -tulnp` (only UDP 7777 is new), `nc -zv games.inceptum.at 8765` from outside must time out.
