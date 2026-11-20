# Resume — handover 2026-11-20 (M7 paused, new session)

`main` = M0–M6 (VM part). **M7 work lives on branch `m7-maps`** (not yet merged to main). No agent, soak or UE process is running except production. Engine UE 5.8.4 reinstalled at `/opt/UnrealEngine-5.8` (2026-11-12, untrimmed except docs/templates/samples/debug, 47 GB).

## Production on this host
- `velox-server@ffa` — 0.6.1+6d8f35f (`/srv/velox/current`), L_DM1 FFA, 3 Medium bots, UDP 7777; rollback `sudo infra/deploy/release.sh --switch 0.6.0+d6cf8a2`.
- `veloxmaster` — 127.0.0.1:8790, writes `/var/www/games.inceptum.at/status/servers.json`.
- nftables `inet velox_fw` persisted (`/etc/nftables.conf`, old file `.pre-velox`): 22/80/443 tcp, 7777 udp, ICMP.
- Landing page from `infra/www`, Linux client 0.6.1 under `/downloads/`. Release archives in `/mnt/oldroot/velox-scratch/t13/` (vda1, survives reboot).
- Agents must never touch these (preamble rule 9).

## Next — M7 (ADR 0008, five map blockouts)
State: soak gate passed for **Sacrarium, Nemus, Orbita**. Bot/nav track complete (H1–H15 merged on `m7-maps`). Task files `docs/tasks/m7-maps/01–38`, README there; STATUS.md on `m7-maps` has the detail.

Open, in this order (two slots: **max 1 Opus agent + 1 Sonnet agent at a time; Fable unavailable → architect work on Opus**):
1. **B3 Culmen** (Opus, `docs/tasks/m7-maps/19-b3-culmen-build.md`): resume from `origin/wip/m7-19-b3-culmen` (9fc6ca9 = earlier unverified WIP 52e488c + merge of `m7-maps`, clean). Check out that branch (don't reset), verify the WIP (validate, golden, navlint, fts row, CulmenRun, Haste asset Duel-disabled), then FFA ×10 at 4 bots + BotRun ×3; raw logs `/mnt/oldroot/velox-scratch/m7-culmen/`.
2. **Task 38 Fornax minor items** (Sonnet, `38-fornax-minor-items.md`): resume from `origin/wip/m7-38-fornax-items` (22b20f9 = unverified item moves ba8651b + merge of `m7-maps`). Regenerate, golden write, navlint, fts row, FornaxRun, FFA ×10 at 3 + BotDuel ×3. Fornax passes everything else.
3. **Task 31** (Sonnet): SacrariumRun Quad-pad route + Armor_Yellow pedestal path.
4. **Task 35** (Sonnet; Opus if it is a real regression): FireRunControl 3/4 retargets — regression from H10 unique bot PlayerIds or flake.
5. **B6 band rulings** (Opus as architect stand-in, `22-b6-band-rulings.md`, has lead notes: allunreach_air).
6. **Merge `m7-maps` → `main`** once Editor+Game build, automation (144+) and `infra/ci/fts.sh` (20+ rows) are green; update STATUS/ROADMAP.

How to dispatch: one task per agent, `isolation: worktree`, agent prompt = "act as dev/dev-hard: read `.claude/agents/<name>.md`, commit+push on own branch" + task file + preamble. Project agent types may be missing → `general-purpose` with `model: opus|sonnet`. Merge each result into `m7-maps` yourself (`git merge --no-ff origin/<branch>`, check conflicts — `infra/ci/fts.sh` and `nettest.sh` conflict often: keep both sides' map rows; check embedded Python in nettest.sh compiles), then `create_map.py --map <m> --golden check` for all maps.

Later, Mac Studio only (`docs/mac-studio-handover.md`): lighting bakes, GPU contrast checks, draw-call measurement for the five maps; M5 bake/playtests.

## Rules learnt
- Agent preamble (scratchpad `m6-preamble.txt`, tmpfs — rewrite if lost) must contain: worktree reset to the task's base branch (M7: `origin/m7-maps`), own `infra/.env` with `VELOX_PROJECT=<worktree>/Velox.uproject`, soak lock `flock -o <scratchpad>/soak.lock`, kill only own PIDs, never delete outside own worktree/scratch, disk: check `df -h /` before every build/soak, < 5 GB → wait 60 s polls up to 20 min then "DISK PAUSE" (other projects swing / by 15 GB), bulky output under `/mnt/oldroot/velox-scratch/<task>/`, no root, production off-limits, `/usr/bin/grep -a` for logs, commit trailer with Claude-Session line.
- Host is loaded by other projects (load1 often 3–30): cost-window-only failures are judged by the host-load / A/B rule (H4/H5 notes), behaviour must pass.
- Usage limit: the 5-hour limit was hit 5× on 2026-11-13/14 with 2 Opus agents → max 1 Opus + 1 Sonnet; tell agents to reuse logs and avoid re-runs. After a limit reset, resume cut-off agents with SendMessage instead of new agents.
- `lfs.locksverify=false` is set in the repo config (GitHub LFS lock API unsupported).
- Shared agent preamble lives on tmpfs (scratchpad `m6-preamble.txt`, lost on session change) — rewrite from: worktree reset, own `infra/.env` with `VELOX_PROJECT=<worktree>/Velox.uproject`, soak lock, kill only own PIDs, never delete outside own worktree/scratch, disk pause line, no root unless "apply", production off-limits, `/mnt/oldroot/velox-scratch` for durable package output, `/usr/bin/grep -a`, commit trailer.
- Disk on / is tight because of other projects' Docker; package via tmpfs symlinks; the lead may not prune Docker (classifier) — ask the human.
- Fable quota can run out: reviews may be done by Opus as stand-in (human-approved), new design decisions need the human.
- Remote branch deletion is blocked — merged `wip/*` branches stay on origin for the human to clean up.
