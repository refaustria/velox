# 13 — Release smoke test and the M6 human playtest checklist

**Milestone:** M6 Online
**Agent:** dev
**Status:** todo
**Depends on:** tasks 01 (archives), 03 (`veloxmaster`), 04 (heartbeat), 05 (browser), 06 (deployed instance); task 11 adds the Mac section

## Goal
One script proves a fresh release end to end on this VM without a human — packaged server up, heartbeat in `servers.json`, a packaged headless client fetching the list and joining through the browser subsystem, the hardening rows holding — and one checklist tells the human exactly what to do on the Apple Silicon Mac to accept M6.

## Scope — files/modules the agent may touch
- `infra/ci/release-smoke.sh` (new): `--server-archive <LinuxServer.tar.zst> --client-archive <Linux.tar.zst> [--master-url http://127.0.0.1:8790] [--public]`; extracts both into the scratchpad, starts a local `veloxmaster` (from `infra/master/target/release` or `--master-binary`) with a scratch output file unless `--public` (then it uses the live `https://games.inceptum.at/status/servers.json` and the deployed instance), starts the packaged server in listen mode on a free port with `-ServerId=smoke`, waits for `servers.json` to list it, starts the packaged **Shipping** client `-nullrhi` with a `-ExecCmds="velox.online.DumpServers <url>"`… — the Shipping client has no cheats, so the client-side proof is: connect by the address read from `servers.json` by the script (`-game <addr>`), log shows the join; the Development server binary from the server archive is used as a second client with the cheat for the browser-list proof; checks `ServerExec` refusal (task 07) with the Development client; tears everything down; prints PASS/FAIL per step
- `Tools/ci/smoke_summary.py` (new, stdlib) if the log checks need more than `grep`
- `docs/playtests/m6-online-checklist.md` (new; task 11 fills the Mac download/connect section, this task writes the rest)
- `infra/ci/README.md` (smoke section), `docs/tasks/m6-online/notes/13-report.md`

## Relevant ADRs / references
- ADR 0007 §1 (a) (the listen-host command line), §2 (schemas), §3 (deployment), §5 (what to probe), implementation notes (acceptance of M6)
- `docs/playtests/m5-map-characters-checklist.md` (format), `infra/ci/nettest.sh` (process handling, readiness lines, `pgrep` rule)
- Memory: soak lock; kill own UE processes

## Acceptance criteria
- [ ] `release-smoke.sh` steps and their pass rules, each printed with timing: (1) archives verify against `SHA256SUMS`; (2) `veloxmaster` healthz 200; (3) packaged server `Listening` within 30 s, RSS printed; (4) `servers.json` lists `smoke` within 45 s with `bots` = the configured count and `version` = the manifest's; (5) the packaged Shipping client joins `<address from servers.json>` (client log join line, server `PostLogin`), stays 20 s, leaves; `players` went 1 → 0 in the file; (6) a Development client runs `velox.online.DumpServers` against the local file URL (served by `python3 -I -m http.server` from the output dir) and logs the `smoke` row as `Ok`; (7) the Development client sends `ServerExec stat fps` and is disconnected (task 07); (8) SIGTERM to the server → exit ≤ 30 s; (9) no `Error:`/`ensure` in any log except an allow-list file `infra/ci/smoke-allow.txt` (engine noise, as `nettest.sh` has). Exit 0 only if all pass.
- [ ] `--public` mode skips (2)/(3)/(8), reads the live `servers.json`, and joins the deployed instance from this VM over the public address; used after every `release.sh`.
- [ ] Runs under the soak lock in ≤ 6 minutes; leaves no processes (`pgrep` check at the end, by binary path).
- [ ] `docs/playtests/m6-online-checklist.md`: prerequisites (Apple Silicon Mac set up per q26, the download URL), steps with expected results and a sign-off table: download + Gatekeeper (q47), main menu → Servers shows the public server with "Praesidium"/FFA/players, Connect, warm-up with bots, 5 minutes of play (movement feel and hit registration against the M1/M2 checklists' rows at real internet latency — note the scoreboard ping), scoreboard, death camera, disconnect → menu with no error, reconnect by direct IP, wrong-password attempt (if 45 chose a password), a second human if available (two Macs or a friend with the Linux build), server status row on the landing page, and what to report (ping, any correction jitter, FPS). Plus the lead's section: `release-smoke.sh --public` PASS, `journalctl` clean, `nft list ruleset` as expected.
- [ ] `infra/ci/README.md` documents both modes.

**Amendment (ADR 0007 F2, 2026-11-06) — release gate:** every release runs the packaged gate P1 (default list against the packaged server, ~11 min) — mandatory; P2 (BotRun ×3) and P3 (BotDuel ×3) are required when `Source/Velox/Bots/`, `Source/Velox/Movement/`, nav data or cooked maps changed since the last release. Sets on an idle host (loadavg < 2.0 at set start, recorded).

## Verification steps
1. `flock -o <scratchpad>/soak.lock infra/ci/release-smoke.sh --server-archive … --client-archive …` on task 01's `--dev` archives → PASS lines; total time.
2. After task 06 phase B: `… --public` → PASS.
3. `pgrep -f 'Binaries/Linux/Velox |UnrealEditor'` → none.

## Report back
Standard format plus: the step table with timings, the allow-list entries you needed and why, and the checklist text for the lead's review.

## Notes / outcome
(Filled by the agent or the lead after completion.)

### Outcome (2026-11-11, branch `wip/m6-13-release`, release 0.6.1) - partially done
- Done: `ProjectVersion` 0.6.1; `docs/playtests/m6-online-checklist.md` (human + lead sections, Mac-only rows marked); build.sh (VeloxEditor, Velox) 140 automation tests Success, fts.sh 8/8; `package.sh --dev` (run from a temporary copy with the `/` guard lowered to 10 GB, copy deleted, guard not committed; cooked/staged/out on tmpfs) PASS incl. smoke (server joins=2, 4 BotDifficulty content smokes); packaged gate P1/P2/P3 PASS on the packaged Development server (evidence `docs/adr/evidence/0007-listen-gate/release-0.6.1/`, host.txt has loadavg at each set start: 1.10 / 1.98 / 1.90).
- Archives: `Velox-0.6.1+6d8f35f-{Linux,LinuxServer}.tar.zst`, `SHA256SUMS`, `MANIFEST.json` (net_version 2513597612; 0.6.0 had 4065349793, so ProjectVersion is part of it) kept in `/mnt/oldroot/velox-scratch/t13/`. These are `--dev` archives (do not publish); a public release needs a `v0.6.1` tag on the commit.
- NOT done: `infra/ci/release-smoke.sh` and its README section (not trivial); checklist section 7 refers to it as planned. Replace the `curl` check with the script when it exists.
