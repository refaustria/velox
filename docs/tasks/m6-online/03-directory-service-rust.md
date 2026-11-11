# 03 — `veloxmaster`: the Rust server directory and status writer

**Milestone:** M6 Online
**Agent:** dev
**Status:** done
**Depends on:** ADR 0007 §2 (protocol and schemas), §3 (`veloxmaster.service`)

## Goal
A small Rust service receives heartbeats from game servers on loopback, keeps an in-memory table with a TTL, and writes `servers.json` atomically into the nginx webroot's `status/` directory, so the browser (task 05) and the landing page (task 12) read a static file that nginx already serves with `no-cache`. Nothing new is exposed publicly; the bind is `127.0.0.1:8790`. CLAUDE.md §1 reserved Rust for exactly this.

## Scope — files/modules the agent may touch
- `infra/master/` (new crate `veloxmaster`): `Cargo.toml`, `Cargo.lock` (committed), `src/main.rs`, `src/state.rs`, `src/schema.rs`, `tests/`, `README.md`, `veloxmaster.service` (unit file), `veloxmaster.env.example`
- `.gitignore` — `infra/master/target/`
- `infra/nginx/README.md` — one paragraph: `status/` ownership by the `veloxmaster` user (no config change; the `/status/` location exists)
- `docs/tasks/m6-online/notes/03-report.md`
(Nothing is installed or started as root in this task; the unit is applied by task 06.)

## Relevant ADRs / references
- ADR 0007 §2 (heartbeat body, `servers.json` schema, TTL 90 s, write cadence, public-address substitution, optional token), §3 (`veloxmaster.service` hardening), §5.7
- `infra/nginx/games.inceptum.at.conf` (`/status/` serves JSON with `no-cache`)
- Toolchain on the VM: `cargo 1.99` at `~/.cargo/bin`

## Acceptance criteria
- [ ] `cargo build --release` on this VM produces `target/release/veloxmaster` (axum 0.8 + tokio + serde/serde_json + tracing; no database, no TLS, no other network client). Binary size and build time reported; `target/` is gitignored.
- [ ] Configuration by environment only: `VELOXMASTER_LISTEN` (default `127.0.0.1:8790`), `VELOXMASTER_OUTPUT` (default `./servers.json`; production `/var/www/games.inceptum.at/status/servers.json`), `VELOXMASTER_PUBLIC_ADDRESS` (required; e.g. `77.244.242.34`), `VELOXMASTER_TTL_S` (90), `VELOXMASTER_WRITE_INTERVAL_S` (15), `VELOXMASTER_TOKEN` (optional; when set, heartbeats without the matching `token` field are rejected 401).
- [ ] `POST /api/v1/heartbeat` accepts the ADR §2 body (`schema`, `id`, `name`, `port`, `map`, `map_display_name`, `mode`, `phase`, `time_left_s`, `players`, `max_players`, `bots`, `spectators`, `version`, `net_version`, `password`, optional `token`); validates types and lengths (`id` ≤ 32 chars `[a-z0-9_-]`, `name` ≤ 64, strings ≤ 128), returns 204, 400 on a bad body, 401 on a bad token, 413 above 8 KB. The entry's `address` is `<source ip>:<port>`, except a loopback/private source becomes `<VELOXMASTER_PUBLIC_ADDRESS>:<port>`.
- [ ] `GET /api/v1/servers` returns the same JSON the file holds (for local debugging); `GET /healthz` returns 200 with `{"entries":N,"last_write":"…"}`.
- [ ] `servers.json` is written with temp file + `rename` in the output directory (never a partial file), on every change and at least every `WRITE_INTERVAL_S`; schema exactly ADR §2: `{ "schema":1, "generated_at":"<RFC 3339 UTC>", "servers":[ { heartbeat fields…, "address", "host":"games.inceptum.at", "updated_s_ago":N } ] }`, sorted by `id`. Entries older than TTL are dropped and the file rewritten.
- [ ] Unit tests (`cargo test`): TTL expiry, address substitution, validation failures, JSON shape (golden file), atomic write (output readable while a writer runs), token enforcement.
- [ ] `veloxmaster.service`: `User=veloxmaster`, `EnvironmentFile=/etc/veloxmaster.env`, `ExecStart=/srv/veloxmaster/veloxmaster`, `Restart=always`, `RestartSec=2`, `NoNewPrivileges=yes`, `ProtectSystem=strict`, `ProtectHome=yes`, `PrivateTmp=yes`, `ReadWritePaths=/var/www/games.inceptum.at/status`, `MemoryMax=256M`. README: build, install steps (for task 06), curl examples, the "nothing public" note.
- [ ] Runs as an unprivileged user with a 50 ms end-to-end heartbeat→file latency in the verification; RSS ≤ 30 MB idle.

## Verification steps
1. `cd infra/master && cargo build --release && cargo test` → green.
2. `VELOXMASTER_PUBLIC_ADDRESS=77.244.242.34 VELOXMASTER_OUTPUT=<scratchpad>/status/servers.json VELOXMASTER_TTL_S=5 target/release/veloxmaster &` then two `curl -s -o /dev/null -w '%{http_code}' -X POST 127.0.0.1:8790/api/v1/heartbeat -d @fixture_ffa.json` / `fixture_duel.json` → 204; `cat <scratchpad>/status/servers.json | python3 -I -m json.tool` → two entries with `address` `77.244.242.34:7777/7778`.
3. Wait 7 s → file has zero entries, `generated_at` advanced. Bad body → 400; with `VELOXMASTER_TOKEN=x` and no token → 401.
4. `ss -tlnp | grep 8790` → bound to 127.0.0.1 only. Kill the process.

## Report back
Standard format plus: crate dependency list with versions, binary size, idle RSS, the golden `servers.json`, and any schema detail the ADR left open that you had to decide (report, do not improvise silently).

## Notes / outcome
Done; see notes/03-report.md. Verified live on loopback 8421/8422: 204, addresses substituted, TTL expiry rewrites file, 400/401, bound to 127.0.0.1 only.
