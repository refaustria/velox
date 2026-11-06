# veloxmaster

Tiny Rust service (axum + tokio) behind the Velox server browser (ADR 0007 section 2).
Game servers POST a heartbeat to `127.0.0.1:8790`; the service keeps an in-memory table
(TTL 90 s) and writes `servers.json` atomically (temp file + rename) into the nginx
webroot's `status/` directory, on every change and at least every 15 s. nginx serves
that static file; if the service dies the file just goes stale (`generated_at` tells).

**Nothing public:** the listener binds loopback only and nginx does not proxy it.
No database, no TLS, no outbound network.

## Configuration (environment only)

| Variable | Default | Meaning |
|---|---|---|
| `VELOXMASTER_LISTEN` | `127.0.0.1:8790` | bind address |
| `VELOXMASTER_OUTPUT` | `./servers.json` | output file (prod: `/var/www/games.inceptum.at/status/servers.json`) |
| `VELOXMASTER_PUBLIC_ADDRESS` | required | replaces loopback/private/link-local source addresses |
| `VELOXMASTER_TTL_S` | 90 | entry lifetime |
| `VELOXMASTER_WRITE_INTERVAL_S` | 15 | max time between file writes |
| `VELOXMASTER_TOKEN` | unset | when set, a heartbeat needs the same `token` field, else 401 |

## API

- `POST /api/v1/heartbeat` JSON body (ADR section 2), max 8 KB: 204 ok, 400 bad body, 401 bad token, 413 too large.
  `net_version` may be a string or an integer (stored as string). Unknown fields are ignored.
- `GET /api/v1/servers`: the same JSON as the file. `GET /healthz`: `{"entries":N,"last_write":"..."}`.

```
curl -s -o /dev/null -w '%{http_code}\n' -X POST 127.0.0.1:8790/api/v1/heartbeat -d @tests/fixtures/fixture_ffa.json
curl -s 127.0.0.1:8790/api/v1/servers
```

## Build and test

```
cd infra/master
cargo build --release     # target/release/veloxmaster (~1.3 MB)
cargo test
UPDATE_GOLDEN=1 cargo test golden   # regenerate tests/golden/servers.json after a deliberate schema change
```

## Install (applied by task 06, needs sudo)

```
sudo useradd --system --home /srv/veloxmaster --shell /usr/sbin/nologin veloxmaster
sudo install -d -o root -g root /srv/veloxmaster
sudo install -m 0755 target/release/veloxmaster /srv/veloxmaster/veloxmaster
sudo install -d -o veloxmaster -g www-data -m 2755 /var/www/games.inceptum.at/status
sudo install -m 0640 -o root -g veloxmaster veloxmaster.env.example /etc/veloxmaster.env   # then edit
sudo install -m 0644 veloxmaster.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now veloxmaster
```
