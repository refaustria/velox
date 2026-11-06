# Resume — state of 2026-11-06 (waiting for disk)

Session of 2026-11-06 stopped by the human: wait until the micasa Docker builds are done (they pushed / below 10 GB free); the human re-triggers manually. No agent, soak or UE process is running; everything is pushed. `git worktree list` should show only the main checkout.

## Merged into `m6-online` today
04b (heartbeat only with `-ServerId`), 02 part A + gate run (ADR 0007 F2: listen gate PASSED; packaged server is the deploy target after 02b), 05 (server browser, 130 tests), 06 phase A (deploy/systemd/firewall files, not applied), Mac Studio runbook `docs/mac-studio-handover.md` + Mac bake-script fixes, task files 02b, 16, F2 amendments to 06/13. Decisions 50–52 in STATUS.md.

## WIP branches (pushed, not merged) — resume in this order
1. `wip/m6-02b-cook` @ 7b684ff — StateTree/GameplayCameras/CameraShakePreviewer disabled (B1 root cause), `/Game/Bots` + `L_DM1` cooked, tag hash folded into the net version (F2), content smoke in package.sh, nettest taghash compare, `L_DM1` exempt from `LightingBuilt` until the bake (lead: accepted). **Not yet compiled with the F2 code.** Next: build + tests, tagprobe parity, negative NMT_Hello run, package.sh, P1–P3 on an idle host (gate scripts hard-code the removed worktree path `agent-af959…` — copy and adapt). Needs ≥ 15 GB free.
2. `wip/m6-07-hardening` @ 76e448a — F1 checklist done, 131 tests, Strafe/Pad ×10, Knockback ×10, RpcGate/Password ×3 PASS. **Architect ruling needed:** server-side speed-hack result −20.5 % vs the F1.1 "±5 % of honest" criterion (agent found two engine losses: unsimulated payback client time ≈ −6 %, standstill after each payback ≈ −14 %); restate as "≤ honest + 5 %" or demand a fix. Remaining: full default list `--repeat 1`, heartbeat `bPassword` from `HasServerPassword()` (VeloxServerHeartbeatSubsystem.cpp:242), rerun the three SpeedHackRun rows with the committed `dx_*` rule.
3. `wip/m6-08-spectator` @ 41f5ec4 — code + tests done (132 tests). ItemGateSpectator PASS ×3; ItemGatePlayer FAIL ×3 from the harness (FFA Countdown→Live reset replaces the scripted pawn; no leak, all n=0) — fix committed, not run. Remaining: ItemGateRun ×3 dedicated + listen, ItemRun/DeathSpectateRun, BotRun ×3, fts.sh. Design review by architect after (netcode).

## Then
16 (L_ItemTest JumpPad links, after 02b — both need an idle host), 01c, 09 (after 07), 06 apply (decision 52: after 02b and 07 green; announce each root step), 10, 13. Follow-ups: infra/ci/README.md smoke duration (120 s); heartbeat host via `IsLocalController()`; `docs/adr/evidence/0007-listen-gate/SUMMARY.md` (subagents may not write it — lead copies the table from the 02 notes).

## Host / disk
/ had 7.9 GB free at worst; containerd grew 21→24 GB from another project's builds. Velox worktrees are ~0.6–2.5 GB each; package runs need ≥ 15 GB. Mac Studio (Apple Silicon, 2 TB, 128 GB) arrives ~2026-11-20 — see `docs/mac-studio-handover.md`.

## Rules learnt
Shared agent preamble (scratchpad `m6-preamble.txt` is tmpfs — rewrite from: worktree reset, own `infra/.env`, soak lock, kill only own PIDs, never delete outside own worktree/scratch, disk floor + pause rule, no root unless "apply", grep → /usr/bin/grep -a, commit trailer). Deleting remote branches is blocked by the permission classifier — leave merged `wip/*` branches on origin for the human.
