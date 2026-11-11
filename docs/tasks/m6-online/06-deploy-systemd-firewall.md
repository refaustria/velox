# 06 — Deployment: service user, systemd units, release switching, firewall, publish script

**Milestone:** M6 Online
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0007 §3; files can be written now; **applying** needs human answers 43 and 44 and task 02's gate PASS (and task 01's server archive, task 03's binary)

## Goal
Everything needed to run `velox-server@ffa` and `veloxmaster` on this host as unprivileged users, switch releases atomically, publish client downloads into the vhost, and (with the human's word) filter inbound traffic to the ports actually used. Two phases: **A — files** (repo only, no root), **B — apply** (stated root actions, one by one, only after the answers). Nothing in this task touches game code.

## Scope — files/modules the agent may touch
- `infra/systemd/velox-server@.service`, `infra/systemd/velox-server.env.example` (instance env template), `infra/systemd/velox-tmpfiles.conf` (log age rule), `infra/systemd/README.md`
- `infra/deploy/run-server.sh` (the `ExecStart` wrapper: builds the URL and switches from the env; `VELOX_SERVER_MODE=listen|editor`), `infra/deploy/install-server.sh` (idempotent; creates users `velox`/`veloxmaster`, `/srv/velox/{releases,config,saved,bin}`, `/srv/veloxmaster`, `/var/www/games.inceptum.at/status` ownership, installs units + tmpfiles, enables nothing by itself), `infra/deploy/release.sh <ver> <LinuxServer archive>` (extract, verify sha, switch `current`, restart the instances, keep two releases), `infra/deploy/publish-download.sh <ver> <file>…` (rsync to `downloads/<ver>/`, `SHA256SUMS`, `latest.json`, `downloads/index.html`, keep two versions), `infra/deploy/README.md`
- `infra/firewall/nftables.conf`, `infra/firewall/README.md` (apply procedure with the armed rollback)
- `infra/master/veloxmaster.service` (from task 03; only install, no edit)
- `docs/machine.md` — append the service inventory after phase B
- `docs/tasks/m6-online/notes/06-report.md`
(No `Source/`, no nginx config change — `/downloads/` and `/status/` locations exist.)

## Relevant ADRs / references
- ADR 0007 §3 (layout, unit contents, hardening, firewall policy, downloads layout), §1 (a)/(b) (listen vs editor mode), §4 (artefact names), §5.7, facts 5 and 8 (no firewall active; SIGTERM graceful; `-UserDir`)
- CLAUDE.md §2 (state root actions before running them; systemd under an unprivileged user; only the used UDP ports open; nothing else exposed)
- `infra/nginx/README.md`, `infra/nginx/games.inceptum.at.conf`
- Human answers 43 (instances/ports/bot defaults), 44 (firewall, which public ports stay), 45 (server name, password)

## Acceptance criteria — phase A (files)
- [ ] `velox-server@.service` matches ADR 0007 §3: `User=velox`, `EnvironmentFile=/srv/velox/config/%i.env`, `ExecStart=/srv/velox/bin/run-server.sh %i`, `Restart=on-failure`, `RestartSec=5`, `StartLimitIntervalSec=300`, `StartLimitBurst=5`, `TimeoutStopSec=30`, `KillSignal=SIGTERM`, `KillMode=control-group`, `NoNewPrivileges=yes`, `ProtectSystem=strict`, `ProtectHome=yes`, `PrivateTmp=yes`, `ReadWritePaths=/srv/velox/saved`, `MemoryMax=6G`, `LimitCORE=0`, `Nice=-5`, `After=network-online.target`, `Wants=network-online.target`.
- [ ] `run-server.sh`: reads the env (`VELOX_MAP`, `VELOX_MODE`, `VELOX_PORT`, `VELOX_MAXPLAYERS`, `VELOX_MAXSPECTATORS` 4, `VELOX_BOT_OPTIONS` e.g. `?Bots=3?BotsFill=1?BotDifficulty=Medium`, `VELOX_SERVER_NAME`, `VELOX_SERVER_MODE`, optional `VELOX_SERVER_PASSWORD`, `VELOX_RELEASE_DIR` default `/srv/velox/current`); **listen** mode runs `<release>/Velox/Binaries/Linux/Velox ${MAP}?listen?game=${MODE}?SpectatorOnly=1?Name=Server?MaxPlayers=…?MaxSpectators=…${BOT_OPTIONS} -nullrhi -nosound -unattended -nopause -nosplash -log -port=${PORT} -UserDir=/srv/velox/saved/<inst> -ExecCmds="t.MaxFPS 60" -ServerId=<inst> -ServerName="…" -ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:InitialConnectTimeout=30 -ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:ConnectionTimeout=20` plus the switch that disables the trace server (verify the 5.8 spelling: try `-notrace`, `-trace=none`, `-NoUnrealTraceServer`; report which the log honours — `UnrealTraceServer` must not be forked); **editor** mode runs `UnrealEditor-Cmd <checkout>/Velox.uproject ${MAP}?game=… -server …` per ADR §1 (b). `set -euo pipefail`, `exec` the binary (systemd sees the real PID).
- [ ] `install-server.sh`: states each root step in one line before running it; idempotent (re-run changes nothing); creates the users (`--system --home /srv/velox --shell /usr/sbin/nologin`), directories with modes (`config` 0750 root:velox, env files 0640), installs units to `/etc/systemd/system/`, `velox-tmpfiles.conf` to `/etc/tmpfiles.d/` (`e /srv/velox/saved/*/Logs - - - 14d`), `daemon-reload`; prints what to do next; **does not** enable or start anything and **does not** touch the firewall.
- [ ] `release.sh`: `df` floor check (15 GB after extraction, estimated from the archive size × 1.2), `sha256sum -c`, extract to `releases/<ver>.tmp` → rename, `chown -R root:velox`, read-only (`chmod -R a-w`), switch the `current` symlink atomically (`ln -sfn` + `mv -T`), `systemctl restart 'velox-server@*'` for enabled instances, prune to two releases, rollback command printed.
- [ ] `publish-download.sh`: copies archives to `/var/www/games.inceptum.at/downloads/<ver>/` (`rsync --chmod=F644`), writes `SHA256SUMS`, regenerates `downloads/latest.json` (schema ADR §3) and a static `downloads/index.html` (version, per-file link + size + sha, previous version link), prunes to two versions; nginx needs no reload.
- [ ] `infra/firewall/nftables.conf`: `table inet filter` with `input` policy drop: accept `lo`, `ct state established,related`, `icmp`/`icmpv6` (rate-limited), TCP 22/80/443, UDP 7777 (plus a commented line per extra UDP port for further instances), and TCP 8765/3000 **behind a `# q44:` marker** (removed or kept per the answer); `forward` and `output` chains untouched (docker manages `forward`; keep its policy accept). README: apply = `sudo nft -c -f` (check) → `echo 'nft -f /etc/nftables.conf.rollback' | at now + 10 minutes` with the current (empty) ruleset saved as rollback → `sudo nft -f infra/firewall/nftables.conf` → verify SSH in a second session → `atrm` → `sudo install -m 0644 … /etc/nftables.conf && sudo systemctl enable --now nftables`.
- [ ] `shellcheck` clean on all scripts; READMEs explain the layout and the two modes.

## Acceptance criteria — phase B (apply; only with answers 43/44 and the 02 gate; every root action stated first)
- [ ] `sudo infra/deploy/install-server.sh` → users and dirs exist; `id velox` ok; `systemctl cat velox-server@.service` matches the repo file.
- [ ] `veloxmaster` installed to `/srv/veloxmaster/`, `/etc/veloxmaster.env` (`VELOXMASTER_PUBLIC_ADDRESS=77.244.242.34`, output path, no token in M6), `systemctl enable --now veloxmaster` → `curl -s 127.0.0.1:8790/healthz` 200; `/var/www/games.inceptum.at/status/servers.json` exists with zero entries.
- [ ] `sudo infra/deploy/release.sh <ver> <LinuxServer archive>`; `/srv/velox/config/ffa.env` from the template with the answers to 43/45; `systemctl enable --now velox-server@ffa` → `active (running)` after ≤ 60 s; `ss -ulnp | grep 7777` shows the process; `journalctl -u velox-server@ffa` shows `Game Engine Initialized`; RSS ≤ 2.5 GB; within 60 s `servers.json` lists `ffa` with `players 0`, `bots 3`.
- [ ] Firewall applied per the README procedure with the armed rollback; `sudo nft list ruleset` equals the file's semantics; SSH still works from a fresh session; `curl -I https://games.inceptum.at/` 200.
- [ ] End-to-end from this VM: `UnrealEditor-Cmd <project> 77.244.242.34:7777 -game -nullrhi -nosound -unattended -nopause -nosplash -log` (the **public** address, so the firewall and the public interface are on the path) joins (client log `Welcome`/`VeloxShotSeqReset`, server journal `PostLogin`), and `servers.json` shows `players 1` within 60 s; disconnect → `players 0`.
- [ ] `systemctl stop velox-server@ffa` exits cleanly within 30 s (journal: exit status 143 or 0, no SIGKILL); `start` again recovers; a `kill -SEGV` of the process is restarted by systemd within 10 s.
- [ ] `docs/machine.md` gains a "Services" section (units, users, ports, paths, the firewall file).

**Amendment (ADR 0007 F2, 2026-11-06):** phase B deploys the **packaged Development Game build** (`Velox-<ver>-LinuxServer.tar.zst`) as headless listen server (`VELOX_SERVER_MODE=listen`), only after task 02b's P1–P3 pass on an idle host. The editor binary is never the production server; an `editor`-mode fallback is allowed only under F2 (ii) (equal tag hash). Decision 52: apply approved once 02b and 07 are green; the lead announces each root step. Decision 51: the instance serves `L_DM1`.

## Verification steps
1. Phase A: `shellcheck infra/deploy/*.sh infra/systemd/*.sh 2>/dev/null; systemd-analyze verify infra/systemd/velox-server@.service` (verify runs without root) → clean; `nft -c -f infra/firewall/nftables.conf` (check-only, may need sudo — state it).
2. Phase B: the commands in the criteria, in order, each stated in one line before it runs; outputs pasted.
3. `ss -tulnp` before and after: only UDP 7777 is new.

## Report back
Standard format plus: which trace-server switch worked, the measured start-up time and RSS of the public instance, the final `nft list ruleset`, and anything the human must still do (e.g. a provider firewall rule).

## Notes / outcome
(Filled by the agent or the lead after completion.)

2026-11-06, dev, phase A (files) done on `wip/m6-06-files`, nothing applied (no root, no UE). Files: `infra/systemd/{velox-server@.service,velox-server.env.example,velox-tmpfiles.conf,README.md,veloxmaster.service.d/10-hardening.conf}`, `infra/deploy/{run-server.sh,install-server.sh,release.sh,publish-download.sh,publish-index.py,README.md,nginx/velox-locations.conf}`, `infra/firewall/{nftables.conf,apply.sh,README.md}`.
Deviations from the text above: (1) password only as `VELOX_SERVER_PASSWORD` (architect ruling ADR 0007 F1.3), never `-ServerPassword=`; (2) `MaxSpectators` default 5 (the headless host takes a spectator slot in `AGameSession::AtCapacity`); (3) trace server switch is `-notraceserver` (engine source `TraceAuxiliary.cpp`; live confirmation owed); (4) firewall table is `inet velox_fw` without `flush ruleset` (docker-safe), rollback by a `systemd-run` timer because `at` is not installed; 8765/3000 closed (q44); docker bridges and DHCP replies allowed; (5) logs land in `saved/<inst>/Saved/Logs` (`-UserDir` -> `ProjectSavedDir = <UserDir>/Saved`), the tmpfiles rule follows that; (6) the veloxmaster unit gets a drop-in instead of an edit.
Offline checks: shellcheck clean, `systemd-analyze verify` (only the not-yet-installed binary paths), `unshare -Urn nft -c -f` and a real load twice (idempotent) inside a user+net namespace, scratch runs of release/publish/run-server with a stub binary.
Phase B open. Owed in B: live confirmation of `-notraceserver`, start-up time/RSS, final `nft list ruleset`.
