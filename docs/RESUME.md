# Resume after the pause of 2026-11-01

Paused by the human on 2026-11-01 ("we continue next week"). No agent, soak or UE process is running. All work is pushed.

## Branches
- `main` = M0–M5 (VM part). M5 acceptance waits for the human's Apple Silicon Mac (bake + checklist + decision 40).
- `m6-online` (head pushed) = ADR 0007 (accepted, answers 41–49) + M6 tasks 01, 01b, 03, 04, 12 merged. Last full build on the merged code: 125 tests green (04b worktree, before 01b); 01b's own build 124 tests + 10-min packaged server run.
- WIP branches (not merged, each with task-file notes saying what is open):
  - `wip/m6-02-listen-gate` — task 02 part A: listen-mode runner, `VeloxServerKind.h`, server-kind sites, **F1 fix** (`bNetworkAlwaysReplicateTransformUpdateTimestamp`, architect-approved, ADR 0003 B45). Open: the full gate (same-day dedicated default list baseline, listen default list, BotRun ×3 + BotDuel ×3 in listen) — runner `docs/adr/evidence/0007-listen-gate/gate.sh`; then part B with the packaged binary (01b is merged, so B is unblocked). Merge 02 into `m6-online` first (trial merge with 01b was clean) and run the gate on the merged head.
  - `wip/m6-07-hardening` — task 07: RPC gate, test-RPC cvar, password, names, discrepancy logging; Editor build + 127 tests green, RpcGateRun/PasswordRun PASS ×2. Open: Game Dev/Shipping builds, KnockbackRun 10/10, bot sets, `--repeat 3`. **Architect rulings needed:** (a) speed-hack result −20 % vs the "within 5 %" criterion (`MovementTimeDiscrepancyResolutionRate`), (b) the extra `ServerPause_Validate` gate, (c) password in logs → task 06 passes it via `VELOX_SERVER_PASSWORD`.
  - `wip/m6-04b-heartbeat` — heartbeat only with `-ServerId`; build 125 tests, FireRun clean. Open: the `-ServerId=test` stub check (command in the task file), then merge.

## Next steps, in order
1. Read `STATUS.md`, this file, `docs/tasks/m6-online/README.md`. `git worktree list` must show only the main checkout.
2. Finish 04b (stub check) → merge. Architect ruling on 07 (a)–(c) → 07 finishes its open checks → merge.
3. Merge 02 → full listen gate on an idle host → 02 part B (packaged binary). The gate verdict decides the server path (ADR 0007 §1).
4. Then: 01c (Shipping client headless join), 05 (server browser; reads `VeloxServerStatus.h`), 08 (spectator item-timer gating), 09 (anomaly counters, after 07), 06 files. 06 **apply** (user `velox`, systemd, nftables with 22/80/443/7777-udp only — 8765/3000 closed per q44; check what listens there and say it before applying), then 10, 13.
5. Small follow-ups: `infra/ci/README.md` smoke duration (now 120 s); `MaxSpectators` 5 in task 06 (host takes a slot); heartbeat finds the host via `IsLocalController()`; first release needs a ruling whether `L_DM1` (unbaked) goes into `MapsToCook` before the Mac bake.

## Rules learnt this session
- Agent briefs: shared preamble (scratchpad `m6-preamble.txt` is tmpfs — rewrite it from this list): worktree reset, own `infra/.env`, soak lock, kill only own PIDs, never delete outside own worktree/scratch (an agent deleted `~/.cache/daisychess`, `ms-playwright`, `pnpm` on 2026-11-01), ≥ 15 GB disk, no root actions unless the task says "apply".
- 14 stale self-matching `pgrep` wait loops from earlier agents are still running (harmless; killing them was blocked by the permission classifier; PIDs listed in the chat of 2026-11-01).

## Mac Studio
The Apple Silicon Mac Studio arrives about 2026-11-20 (compile, test, bake, playtest host). Human runbook: `docs/mac-studio-handover.md` (setup, build, M5 bake + checklist + decision 40, older playtests, task 11, what to send back).
