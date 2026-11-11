# systemd units

- `velox-server@.service`: template unit, instance = env file name (`/srv/velox/config/<inst>.env`), runs `/srv/velox/bin/run-server.sh <inst>` as user `velox`. Hardened (`NoNewPrivileges`, `ProtectSystem=strict`, only `/srv/velox/saved` writable, empty capability set, address families limited to INET/INET6/UNIX). `Restart=on-failure`, 5 starts per 300 s, `TimeoutStopSec=30`, `MemoryMax=6G`.
- `velox-server.env.example`: instance template (copied to `ffa.env` by `infra/deploy/install-server.sh` if missing). Password only as `VELOX_SERVER_PASSWORD` there.
- `veloxmaster.service.d/10-hardening.conf`: extra sandboxing for `infra/master/veloxmaster.service` (no outbound network, syscall filter, no W+X memory).
- `velox-tmpfiles.conf`: `/etc/tmpfiles.d/velox.conf`, 14 day age-out of `saved/*/Saved/Logs` and `Crashes`.

Install and operation: `infra/deploy/README.md`. Offline check: `systemd-analyze verify infra/systemd/velox-server@.service` (reports the not-yet-installed wrapper path only).
