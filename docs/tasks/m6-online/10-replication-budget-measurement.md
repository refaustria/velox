# 10 — Replication budget measurement (classic replication stays unless these numbers say otherwise)

**Milestone:** M6 Online
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0007 §5.6; task 02 (listen-mode nettest, host sidecar); ideally task 01's packaged server

## Goal
Numbers, not opinions, for ADR 0001 §3's deferred "evaluate Replication Graph / Iris at M6 with real player counts": server frame-time percentiles and per-connection bandwidth of the headless listen server on `L_DM1` with several real connections and a full bot complement, extrapolated to 16 connections. Classic replication is kept unless the ADR 0007 §5.6 thresholds are crossed; either way the result is appended to the ADR.

## Scope — files/modules the agent may touch
- `infra/ci/replbudget.sh` (new): starts one server (listen mode, `L_DM1?game=FFA?Bots=12?BotDifficulty=Medium`, `t.MaxFPS 60`, `-ExecCmds="stat unit, stat net"` plus a 10 s `DumpStats`-style periodic dump — if `stat` dumps are awkward headless, use `-csvprofile`/`csv.*` or `FPlatformTime` logging via the existing `VeloxBotBudget` window line which already carries frame counts) and N headless `-game -nullrhi -nosound` clients at **`t.MaxFPS 60`** (ADR 0007 fact 6: never 120 on this VM) running the `BotWatch`-style idle script with `velox.bots.Watch`, each connected for 180 s; the host sidecar from `nettest.sh` (reuse `start_host_sampler`); collects `Saved/Logs` and writes a summary
- `Tools/ci/replbudget_summary.py` (new, stdlib only): frame-time mean/p50/p99 (from the periodic dumps or the `frames=`/`stalls=` windows), per-connection out/in bytes/s (from `stat net` dumps or `LogNetTraffic`-style lines — find what 5.8 prints headless; `net.DebugDraw` is useless here; `Net.PrintNetStats`? report what worked), the extrapolation table
- `Source/Velox/Net/VeloxNetStatsLog.cpp` (new, non-shipping, only if the engine gives no usable headless bandwidth line): a 10 s `VeloxNetStats conns= out_bps_total= out_bps_per_conn=[…] in_bps_total= frame_ms_mean= frame_ms_p99=` line from `UNetDriver::ClientConnections` (`OutBytesPerSecond`, `InBytesPerSecond`) and the game thread's frame times — ≤ 60 lines, server only
- `docs/adr/evidence/0007-repl-budget/` (summary, dumps, sidecars), `docs/tasks/m6-online/notes/10-report.md`, and a one-paragraph verdict appended to `docs/adr/0007-online.md` §"Amendments" by the lead from the report

## Relevant ADRs / references
- ADR 0007 §5.6 (thresholds: server frame mean > 8 ms or p99 > 16 ms at 16 connections; per-client downstream > 100 KB/s), fact 1 (`ReplicationGraph` plugin trimmed), fact 6 (q29)
- ADR 0001 §3 (rates: 60 Hz, `MaxInternetClientRate 100000`, GameState 10 Hz, pawns 60/30), §5
- ADR 0005 D46 (host sidecar, clean/contended/cost windows)

## Acceptance criteria
- [ ] `replbudget.sh --clients N --bots 12 --seconds 180` runs N ∈ {1, 2, 4} on this VM under the soak lock (sequential runs, idle host, load gate as in nettest); each run records: server frame mean/p50/p99 (ms), `frame_deficit`, per-connection out bytes/s (mean, max), total in bytes/s, host sidecar classes, server RSS; the clients' own frame rate is confirmed ≤ 60 (log).
- [ ] The summary extrapolates to 16 connections linearly in connection count for the replication part (frame time = fixed part (bots, physics) + per-connection part; fit from the three points) and states the per-connection downstream at 16 players + 8 bots (bot pawns replicate to every connection — scale by pawn count too; show the formula).
- [ ] A verdict against the §5.6 thresholds: **keep classic** / **revisit** — with the numbers; if the per-connection part is not measurable because the fixed part dominates, say so (that is itself a "keep classic").
- [ ] Nothing in the measurement changes game behaviour (non-shipping stats line only); build and tests green if source was touched.
- [ ] Evidence directory ≤ 10 MB.

## Verification steps
1. `flock -o <scratchpad>/soak.lock infra/ci/replbudget.sh --clients 1 --bots 12`, then `--clients 2`, `--clients 4`; print each summary.
2. `python3 -I Tools/ci/replbudget_summary.py <evidence dir>` → the extrapolation table.
3. `pgrep -f 'UnrealEditor|Binaries/Linux/Velox '` → nothing left.

## Report back
Standard format plus: the three-row measurement table, the fit, the 16-connection extrapolation, the host-sidecar classes per run (any cost windows = note for q29), and the one-paragraph verdict text for the ADR.

## Notes / outcome
(Filled by the agent or the lead after completion.)
