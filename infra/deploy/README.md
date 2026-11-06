# Deployment (task m6-online/06, ADR 0007 section 3)

Files only until the lead announces the apply. Nothing here runs by itself; every script that needs root prints each root action (`ROOT: ...`) before doing it.

## Layout on the host

```
/srv/velox/                 root:root 0755   (user velox, system account, nologin)
  bin/run-server.sh         ExecStart wrapper (copied by install-server.sh)
  releases/<ver>/           root:velox, read-only; contents of Velox-<ver>-LinuxServer.tar.zst (Velox/Binaries/Linux/Velox ...)
  current -> releases/<ver> atomic symlink, switched by release.sh
  config/<inst>.env         root:velox 0640, one per instance (template: infra/systemd/velox-server.env.example)
  saved/<inst>/             velox:velox; -UserDir; logs in saved/<inst>/Saved/Logs (tmpfiles ages them out after 14 d)
/srv/veloxmaster/veloxmaster          binary (user veloxmaster); config /etc/veloxmaster.env (root:veloxmaster 0640)
/var/www/games.inceptum.at/status/    veloxmaster:www-data 2755, servers.json
/var/www/games.inceptum.at/downloads/ root-owned; written by publish-download.sh
```

## Scripts

| Script | What |
|---|---|
| `install-server.sh [--dry-run]` | users, dirs, units, drop-in, tmpfiles, `daemon-reload`; creates `ffa.env` and `/etc/veloxmaster.env` only when missing; enables/starts nothing; no firewall/nginx |
| `release.sh <ver> <LinuxServer.tar.zst>` | sha256 check against the sibling `SHA256SUMS`, disk floor (15 GB left after a 3x estimate), extract to `<ver>.tmp`, `chown root:velox`, read-only, rename, atomic `current` switch, restart enabled `velox-server@*`, keep two releases, print the rollback command. `--switch <ver>` flips between installed releases |
| `publish-download.sh <ver> <file>...` | `downloads/<ver>/`, `SHA256SUMS`, `latest.json`, `index.html`, keeps two versions; refuses the server archive |
| `run-server.sh <inst>` | what systemd runs; see below |
| `nginx/velox-locations.conf` | optional snippet replacing the three location blocks; not applied |

## Server modes (`VELOX_SERVER_MODE` in the instance env)

- `listen` (default, ADR 1a): the packaged Development binary as a headless listen server whose local player is spectator-only: `L_DM1?listen?game=FFA?SpectatorOnly=1?Name=Server?MaxPlayers=16?MaxSpectators=5?Bots=3?BotsFill=1?BotDifficulty=Medium -nullrhi -nosound -unattended -nopause -nosplash -log -port=7777 -UserDir=/srv/velox/saved/ffa -ExecCmds="t.MaxFPS 60" -ServerId=ffa -ServerName="..." -notraceserver -NoVerifyGC` plus the two `IpNetDriver` timeouts.
- `editor` (ADR 1b fallback): `UnrealEditor-Cmd <VELOX_PROJECT_DIR>/Velox.uproject <map>?game=... -server` with the same switches. Needs `VELOX_PROJECT_DIR` and `UE_ROOT` in the env file.

Details worth knowing:
- `MaxSpectators=5` for four real spectators: the engine's `AGameSession::AtCapacity` compares `GetNumSpectators() >= MaxSpectators` and the headless host's own spectator PlayerState counts (the heartbeat subtracts it from `spectators`).
- `-notraceserver` is the 5.8 switch (`TraceAuxiliary.cpp`: skips `FTraceServerControls::Start()`); confirmed from engine source, to be confirmed in the first live log (`pgrep UnrealTraceServer` must be empty).
- `-ServerId=<inst>` makes the game post heartbeats to veloxmaster on loopback (task 04b); without it nothing is posted.
- Password: set `VELOX_SERVER_PASSWORD=...` in `/srv/velox/config/<inst>.env` only (0640 root:velox). The wrapper never passes `-ServerPassword=`, so it is not in argv, `ps` or the engine's command-line log line. Caveat (ADR 0007 F1.3): the engine still echoes the join URL, including `?Password=`, in `LogNet` "Login request:" / "Join request:" lines. Those logs are readable by `velox` and root only and age out after 14 days. Rotate by editing the env file and `systemctl restart velox-server@<inst>`.
- Release switch restarts the instance (a running match ends); logs go to `journalctl -u velox-server@<inst>` and `saved/<inst>/Saved/Logs/`.

## Apply order (lead announces each root step to the human)

1. `sudo infra/deploy/install-server.sh --dry-run`, then without `--dry-run`.
2. `sudo systemctl enable --now veloxmaster`; `curl -s 127.0.0.1:8790/healthz`.
3. `sudo infra/deploy/release.sh <ver> <archive>`; check `/srv/velox/config/ffa.env`.
4. `sudo systemctl enable --now velox-server@ffa`; `ss -ulnp | grep 7777`; `curl -s https://games.inceptum.at/status/servers.json`.
5. Firewall: `infra/firewall/README.md`.
6. Optional: nginx snippet, `publish-download.sh`, landing page rsync (`infra/www/README.md`).

Offline checks used for the files: `shellcheck`, `systemd-analyze verify`, `unshare -Urn nft -c -f`, and a scratch-dir run of `release.sh`/`publish-download.sh`/`run-server.sh` with a stub binary.
