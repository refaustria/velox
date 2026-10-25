# Resume notes — paused 2026-10-25 for server maintenance

Read this after `CLAUDE.md`, `ROADMAP.md` and `STATUS.md`. It tells the lead where the work stopped and what to do first. Delete or rewrite it when the state has moved on.

## State at the pause

- `main`: M0 + M1 merged and green (M1-06 human playtest still open: needs UE 5.8 on the human's Mac).
- `m2-combat`: tasks 01, 01b, 02, 03, 03b, 04, 07 done and committed; 04 architect-reviewed (ADR 0003 amendments B18–B24, `docs/reviews/m2-04-weapons-hitscan-review.md`).
- The pause-day merges of 04b (review fixes), 05 (projectiles / self-knockback) and 06 (death / respawn / game mode) are recorded in the "Merged before the pause" section below; anything not listed there was **not** merged and its worktree branch may still exist (`git worktree list`, `git branch --list 'worktree-agent-*'`).
- Last known CI state: see STATUS.md "Done"/"In progress" and the newest `Saved/CI/<timestamp>/` directory (`report.json` / `build.log`).
- GitHub: nothing pushed yet. `git push` over HTTPS fails with 403 (the `gh` token lacks the `repo` scope). Human action: `gh auth refresh -h github.com -s repo`, then `git push -u origin main m0-infrastructure m1-movement m2-combat`.

## Environment facts that survive a reboot

- UE 5.8.4 installed build: `/opt/UnrealEngine-5.8` (trimmed; never delete `Engine/Extras/ThirdPartyNotUE`, see `infra/engine/README.md` and `TRIM-REPORT.md`). `infra/.env` (gitignored) holds `UE_ROOT` and `VELOX_PROJECT`.
- `/swapfile` 4 GB is in `/etc/fstab`; nginx vhost `games.inceptum.at` is a systemd-enabled unit with a Let's Encrypt cert (auto-renew via certbot timer).
- `/tmp` is tmpfs: anything an agent left there is gone after a reboot (session scratch only, nothing of value).
- Disk: ~30 GB free of 125 GB, cannot be enlarged. `Saved/CI/*` and `Intermediate/` under worktrees are the first things to delete when space runs low.
- Shell: `grep` is a ugrep wrapper, use `/usr/bin/grep -a` on logs. Headless VM: run the game with `-nullrhi`.
- Parallel agents must use separate `infra/ci/nettest.sh --port NNNN` values (7777 is the default and collides).

## Next steps, in order

1. **Sanity check**: `git status`, `git worktree list`, `infra/ci/build.sh` on `m2-combat` (expect ≥ 39 tests, 0 failures). If a worktree from the pause day is still there, read its task file status and either merge (`git merge --no-ff worktree-agent-…`) after its own build evidence, or re-dispatch the task.
2. **Task 04c** `docs/tasks/m2-combat/04c-server-pattern-seed.md` (dev-hard): server chooses the pellet/MG seed, impact cues server-only/unkeyed, owner predicts only the fire cue. Must land before task 09.
3. **Architect review of 05 and 06** in one pass (both touch GAS/netcode): projectile replication, fake projectiles, recorded self-knockback channel (A17), explosion cue unkeyed (B21), B8 death order, `Inventory->StopBeam()` after `RemoveAllGameplayCues()`. Follow-up fix task 05b/06b if must-fixes come back.
4. **Task 08** `08-fire-nettest.md` (dev): FireRun / RocketJumpRun soak scripts with the addendum from the 04 review (5 s warm-up rule B24, switch sequence RG→MG→RG with 0 rejections, MG 40 shots 0 rejections, hit-confirm accounting, 250 ms repeat, `aim_err_cm`, CrouchRun in the default list).
5. **Task 09** `09-human-playtest.md` (dev): combat playtest checklist for the Mac, including "switch feel at 100 ms" and "redirect a switch mid-drop".
6. **Merge `m2-combat` → `main`** when Editor/Client/Server compile and all tests pass; then update ROADMAP.md (tick M2 items) and STATUS.md.
7. **Mac**: human installs UE 5.8 via the Epic Games Launcher, runs `git lfs pull` and `infra/ci/build-mac.sh` from `main`, then the M1 checklist `docs/playtests/m1-movement-checklist.md` and the M2 checklist from task 09. Feed findings back as tuning tasks.
8. **M3 Game loop**: dispatch `architect` for ADR 0004 (pickups/item timers, FFA/Duel, scoreboard, HUD, menu) before writing tasks.

## Open questions for the human (also in STATUS.md)

- Push rights to GitHub (see above).
- Mac: Apple Silicon or Intel? (affects `build-mac.sh` architecture flags)
- M2 defaults currently taken from ADR 0003 without veto: Q3 vanilla weapon numbers, spawn protection 0 s, bitmask inventory, server-confirmed hit markers, corpse death cam, corpses transparent to hitscan (B17).

## How to restart the lead session

From `/home/bojan/velox`, start Claude Code and use this as the first message:

> Lies CLAUDE.md, ROADMAP.md, STATUS.md und docs/RESUME.md. Du bist der Projektleiter wie in CLAUDE.md beschrieben; die Subagenten dev/dev-hard/architect liegen in .claude/agents/. Mach dort weiter, wo docs/RESUME.md aufhört: zuerst der Sanity-Check (Schritt 1), dann die nächsten Schritte in der angegebenen Reihenfolge. Frag mich nur bei den offenen Fragen aus STATUS.md.

If the agent types `dev`, `dev-hard`, `architect` are not offered in a fresh session, the fallback is `general-purpose` with `model: sonnet | opus | fable` and the instruction "You are acting as the `<name>` subagent: read `.claude/agents/<name>.md` first" (see memory note `velox-agent-routing-workaround`).

## Merged before the pause

(filled in by the lead as the last agents report back)
