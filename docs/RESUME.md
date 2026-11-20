# Resume — state of 2026-11-14 (M7 paused)

`main` = M0–M6 (VM part). `m6-online` is merged and can be left as is. No agent, soak or UE process is running except production.

## Production on this host
- `velox-server@ffa` — 0.6.1+6d8f35f (`/srv/velox/current`), L_DM1 FFA, 3 Medium bots, UDP 7777; rollback `sudo infra/deploy/release.sh --switch 0.6.0+d6cf8a2`.
- `veloxmaster` — 127.0.0.1:8790, writes `/var/www/games.inceptum.at/status/servers.json`.
- nftables `inet velox_fw` persisted (`/etc/nftables.conf`, old file `.pre-velox`): 22/80/443 tcp, 7777 udp, ICMP.
- Landing page from `infra/www`, Linux client 0.6.1 under `/downloads/`. Release archives in `/mnt/oldroot/velox-scratch/t13/` (vda1, survives reboot).
- Agents must never touch these (preamble rule 9).

## Next
-1. **M7 (paused 2026-11-14):** read `STATUS.md` on branch `m7-maps` (In progress line) and `docs/tasks/m7-maps/README.md`. Resume order: (1) B3 Culmen from `origin/wip/m7-19-b3-culmen` (rebase/merge on `origin/m7-maps`, verify, finish task 19); (2) task 38 Fornax items from `origin/wip/m7-38-fornax-items`; (3) task 31; (4) task 35; (5) B6 (architect); (6) merge `m7-maps` → `main`. Agent preamble rules below (rewrite scratchpad `m6-preamble.txt`; add rule 6 disk swing + wait, and "base branch per task"). Agent types `dev`/`dev-hard`/`architect` may be missing in a new session → general-purpose with model sonnet/opus/fable + "read .claude/agents/<name>.md".
0. ~~Map concepts~~ — done 2026-11-12 (Fable 5.2 verified): `docs/maps/{sacrarium,culmen,orbita,nemus,fornax}.md` on main. **Waiting for the human to pick two** (STATUS q53); then M7 blockout planning.
1. **Mac Studio (~2026-11-20):** `docs/mac-studio-handover.md` — setup, M5 bake + checklist + decision 40, playtests M1–M4, M6 public-server checklist `docs/playtests/m6-online-checklist.md`, task 11 macOS client (write the task's `build-mac.sh --package` first), test whether Linux x86_64 packages can be built there (decides whether the VM engine stays).
2. **M7 planning** (ROADMAP M7: CTF, replays, two maps, perf pass) — architect ADR first. Carry-overs in STATUS.md's M6 entry (bot hop-at-link-start, RocketJumpRun intermittent, orphan drop row, release-smoke.sh, tagged release builds, navlint L_ItemTest, 8–9 ms server frame).
3. Open human questions: q29, q30; whether anything was lost in the caches deleted on 2026-11-01.

## Rules learnt
- Shared agent preamble lives on tmpfs (scratchpad `m6-preamble.txt`, lost on session change) — rewrite from: worktree reset, own `infra/.env` with `VELOX_PROJECT=<worktree>/Velox.uproject`, soak lock, kill only own PIDs, never delete outside own worktree/scratch, disk pause line, no root unless "apply", production off-limits, `/mnt/oldroot/velox-scratch` for durable package output, `/usr/bin/grep -a`, commit trailer.
- Disk on / is tight because of other projects' Docker; package via tmpfs symlinks; the lead may not prune Docker (classifier) — ask the human.
- Fable quota can run out: reviews may be done by Opus as stand-in (human-approved), new design decisions need the human.
- Remote branch deletion is blocked — merged `wip/*` branches stay on origin for the human to clean up.
