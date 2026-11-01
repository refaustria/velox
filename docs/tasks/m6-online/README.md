# M6 Online — task index

Spec: `docs/adr/0007-online.md` (ADR 0007). Template: `docs/tasks/TEMPLATE.md`. Evidence goes to `docs/adr/evidence/0007-*/`, reports to `notes/`. Human questions 41–49 are in the ADR and must be copied into `STATUS.md`.

| # | Task | Agent | Est. | Depends on | Wave | Needs answer | Status |
|---|---|---|---|---|---|---|---|
| 01 | [release-package-linux](01-release-package-linux.md) | dev | 3 h | ADR §1 (a), §4 | 0 | — | todo |
| 02 | [headless-listen-server-gate](02-headless-listen-server-gate.md) | dev-hard | 3 h (+ soak time) | ADR §1 gate; editor part now, packaged part after 01 | 0 / 1 | — | todo |
| 03 | [directory-service-rust](03-directory-service-rust.md) | dev | 3 h | ADR §2 | 0 | — | done |
| 04 | [server-heartbeat-and-map-registry](04-server-heartbeat-and-map-registry.md) | dev | 3 h | ADR §2; schema shared with 03/05 | 0 | — | todo |
| 05 | [server-browser](05-server-browser.md) | dev | 3 h | ADR §2; 04's `VeloxServerStatus.h` schema | 0 | — | todo |
| 06 | [deploy-systemd-firewall](06-deploy-systemd-firewall.md) | dev | 3 h (files) + 1 h (apply) | ADR §3; files now; apply after 02 PASS | 1 / 2 | **43, 44** (apply only) | todo |
| 07 | [server-hardening-rpc-gate-and-connection-policy](07-server-hardening-rpc-gate-and-connection-policy.md) | dev-hard | 3 h (+ soak) | ADR §5.1–5.3 | 0 | 45 (password default only) | todo |
| 08 | [spectator-item-timer-gating](08-spectator-item-timer-gating.md) | dev-hard | 3 h (+ soak) | ADR §5.5 | 0 | — | todo |
| 09 | [anomaly-counters](09-anomaly-counters.md) | dev-hard | 2–3 h | ADR §5.4; after 07 if both touch `VeloxPlayerController` (coordinate) | 0 | — | todo |
| 10 | [replication-budget-measurement](10-replication-budget-measurement.md) | dev | 2 h (+ soak) | ADR §5.6; 02 | 2 | — | todo |
| 11 | [mac-client-package](11-mac-client-package.md) | dev + human | 2 h + Mac time | ADR §4; 01 (shared script shape); the Apple Silicon Mac | 3 | 47 | todo |
| 12 | [web-downloads-and-status-page](12-web-downloads-and-status-page.md) | dev | 2 h | ADR §3; schemas from 03/06 | 0 | 45 (privacy line), 47/46 wording | done |
| 13 | [release-smoke-and-playtest-checklist](13-release-smoke-and-playtest-checklist.md) | dev | 2 h | 01, 03, 04, 05, 06 | 2 | — | todo |
| 14 | [windows-client](14-windows-client.md) | dev (script) / human (host) | 1 h + host | ADR §4 | — | **46** | blocked |
| 15 | [server-capable-installed-build](15-server-capable-installed-build.md) | dev-hard | 1 day on a build box + swap | ADR §1 (c) | — | **41** | blocked |

## Wave plan (from ADR 0007 implementation notes)

- **Wave 0 — start now, in parallel:** 01 (package), 02 editor-binary part (listen-mode nettest), 03 (Rust service), 04 (heartbeat + map registry), 05 (browser), 07 (hardening), 08 (timer gating), 09 (anomaly counters), 12 (web page). 04 and 05 share `Source/Velox/Net/VeloxServerStatus.h`; 04 owns the file, 05 reads it (dispatch 04 first or give both the schema from the ADR verbatim). 07 and 09 both touch `GameModes/VeloxPlayerController.*`/`VeloxPlayerState.*` — dispatch 09 after 07 is merged.
- **Wave 1 — once 01 lands:** 02 packaged part (the gate), 06 files (unit, scripts, firewall file; nothing applied).
- **Wave 2 — after answers 43/44 and the 02 gate:** apply 06 (user, unit, firewall, first release), 10 (budget), 13 (release smoke + checklist).
- **Wave 3 — Apple Silicon Mac:** 11, then the human runs `docs/playtests/m6-online-checklist.md`.
- **Blocked:** 14 (q46), 15 (q41). M6 is accepted with Windows listed as an exception unless 46 supplies a host.

Rules repeated for every dispatch: soaks/nettest/fts only under `flock -o <scratchpad>/soak.lock` on an idle host; keep ≥ 15 GB free (package jobs check first and prune after); state every root action in one line before running it and run it only where the task says "apply"; never touch `Movement/` or `Net/VeloxLagCompensationSubsystem.*` unless the task names the file; never commit secrets; one task per agent invocation; agents kill their own UE processes before finishing.

## Questions for the human (copied from ADR 0007; numbers 41–49)

See `docs/adr/0007-online.md` §"Questions for the human". Short form: 41 engine path for the real Server target (recommend: Game-build server for M6, installed-build-with-server on a rented box later); 42 session layer (recommend plain IP + Rust directory); 43 instances/ports (recommend one FFA instance on 7777); 44 firewall (recommend nftables allow-list; confirm 8765/3000); 45 server name, open vs password, player names in JSON (recommend open, counts only); 46 Windows client (recommend defer, no host); 47 macOS signing (recommend ad-hoc for M6); 48 Linux client hardware testing (recommend "headless only" label); 49 GitHub Actions (recommend stay local).
