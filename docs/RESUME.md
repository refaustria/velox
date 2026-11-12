# Resume — state of 2026-11-12

`main` = M0–M6 (VM part). `m6-online` is merged and can be left as is. No agent, soak or UE process is running except production.

## Production on this host
- `velox-server@ffa` — 0.6.1+6d8f35f (`/srv/velox/current`), L_DM1 FFA, 3 Medium bots, UDP 7777; rollback `sudo infra/deploy/release.sh --switch 0.6.0+d6cf8a2`.
- `veloxmaster` — 127.0.0.1:8790, writes `/var/www/games.inceptum.at/status/servers.json`.
- nftables `inet velox_fw` persisted (`/etc/nftables.conf`, old file `.pre-velox`): 22/80/443 tcp, 7777 udp, ICMP.
- Landing page from `infra/www`, Linux client 0.6.1 under `/downloads/`. Release archives in `/mnt/oldroot/velox-scratch/t13/` (vda1, survives reboot).
- Agents must never touch these (preamble rule 9).

## Next
0. **Map concepts (requested 2026-11-12):** dispatch five `architect` agents (Fable 5.2 — `.claude/agents/architect.md` now says `model: claude-fable-5-2`; first verify with a one-line probe agent that it really runs as 5.2) per `docs/maps/BRIEF.md`, one theme each, in parallel, docs only, branches `wip/map-<slug>`. Then summarise for the human, who picks two for M7 blockouts. (A first attempt on 2026-11-12 ran on 5.1 because this session could not reach 5.2; it was stopped before writing anything.)
1. **Mac Studio (~2026-11-20):** `docs/mac-studio-handover.md` — setup, M5 bake + checklist + decision 40, playtests M1–M4, M6 public-server checklist `docs/playtests/m6-online-checklist.md`, task 11 macOS client (write the task's `build-mac.sh --package` first), test whether Linux x86_64 packages can be built there (decides whether the VM engine stays).
2. **M7 planning** (ROADMAP M7: CTF, replays, two maps, perf pass) — architect ADR first. Carry-overs in STATUS.md's M6 entry (bot hop-at-link-start, RocketJumpRun intermittent, orphan drop row, release-smoke.sh, tagged release builds, navlint L_ItemTest, 8–9 ms server frame).
3. Open human questions: q29, q30; whether anything was lost in the caches deleted on 2026-11-01.

## Rules learnt
- Shared agent preamble lives on tmpfs (scratchpad `m6-preamble.txt`, lost on session change) — rewrite from: worktree reset, own `infra/.env` with `VELOX_PROJECT=<worktree>/Velox.uproject`, soak lock, kill only own PIDs, never delete outside own worktree/scratch, disk pause line, no root unless "apply", production off-limits, `/mnt/oldroot/velox-scratch` for durable package output, `/usr/bin/grep -a`, commit trailer.
- Disk on / is tight because of other projects' Docker; package via tmpfs symlinks; the lead may not prune Docker (classifier) — ask the human.
- Fable quota can run out: reviews may be done by Opus as stand-in (human-approved), new design decisions need the human.
- Remote branch deletion is blocked — merged `wip/*` branches stay on origin for the human to clean up.
