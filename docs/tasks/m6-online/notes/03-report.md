# 03 report: veloxmaster

Built `infra/master` (axum 0.8.9, tokio 1.54.1, serde 1.0.229, serde_json 1.0.154, tracing 0.1.44, tracing-subscriber 0.3.23; dev: tower 0.5.3, http-body-util 0.1.5).
Release binary 1.3 MB (opt-level s, LTO, stripped), clean build 24 s. Idle RSS 3.8 MB. Heartbeat to file latency 13 ms. 13 tests green.

Decisions the ADR left open:
- `time_left_s`, `players`, `max_players`, `bots`, `spectators` are non-negative integers (counts capped at 1024); a float or negative gives 400. Task 04 must send integers.
- `net_version` accepted as string or integer, published as string.
- `schema` must equal 1; `port` non-zero; unknown body fields ignored; the token is never published.
- Loopback/private/link-local/ULA/unspecified (and v4-mapped v6) sources become the public address; IPv6 public sources print as `[addr]:port`.
- A heartbeat only triggers an immediate write when published content changed; refreshes are written by the 15 s tick (updated_s_ago in the file can therefore be up to 15 s stale; TTL expiry triggers its own write).
- Added `src/lib.rs` (router/config/writer) so integration tests can use it; `main.rs` is thin.
- Body is parsed from raw bytes so `curl -d` (form content type) works; 8 KB limit gives 413.
