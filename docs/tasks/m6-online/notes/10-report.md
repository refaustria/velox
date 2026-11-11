# Task 10 report: replication budget measurement

Setup: headless listen server (`L_DM1?game=FFA?Bots=12?BotDifficulty=Medium?MaxPlayers=16?listen?SpectatorOnly=1`, `t.MaxFPS 60`, `velox.bots.Debug 1`),
N idle player clients (`-game -nullrhi`, `t.MaxFPS 60`, `velox.bots.Watch 180`, no packet simulation, loopback), 180 s each, under the soak lock,
ports 7890. The engine prints no headless bandwidth line, so `VeloxNetStats` (Source/Velox/Net/VeloxNetStatsLog.cpp, `velox.net.StatsLog 1`,
non-shipping) was added. Frame time = game-thread CPU ms per frame (wall frame time is pinned at 16.7 ms by the cap). Windows: 10 s, conns == N,
first 30 s after full connection dropped (Countdown). Production (`velox-server@ffa`) used ~48 % of one core throughout (prod.log sidecar); all
windows of all runs were host-`clean` (rq_wait <= 1.4 %, steal 0, PSI <= 1.4 %).

Results: see docs/adr/evidence/0007-repl-budget/summary.txt (table, fit, extrapolation, verdict).
Control: N=2 with `velox.bots.Debug 0` (no bot debug logging): cpu mean 9.41 ms, so the ~8.4-9.0 ms fixed part is not logging.
Not usable headless: `stat unit` / `stat net` (no viewport draw), `net.DebugDraw`.
