# 03 — Nav-coverage lint (`velox.bots.NavCoverage`, `infra/ci/navlint.sh`)

**Milestone:** M5 Map + characters
**Agent:** dev-hard
**Status:** todo
**Depends on:** ADR 0006 §6; ADR 0005 D17, D32, D41, D44/D45 (projection semantics)

## Goal
The lint owed since M4: a server console command that finds walkable floor without nav mesh (D41's class) and nav under too little clearance (D32's class), reported as clusters with the layout tag, plus a CI wrapper that fails on any cluster. It gates every DM1 map from task 05 onwards.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxNavCoverage.h/.cpp` (new; the world-free clustering helper in `VeloxNavCoverageMath.h`), `Source/Velox/Bots/VeloxBotCheats.cpp` or wherever `velox.bots.*` console commands are registered, `Source/Velox/Tests/VeloxBotTests.cpp` (new case `Velox.Bots.NavCoverageClusters`)
- `infra/ci/navlint.sh` (new), `infra/ci/lib.sh` (only if a helper is needed)
- `docs/tasks/m5-map-characters/notes/03-report.md` (the report-only output of the three test maps)

## Relevant ADRs / references
- ADR 0006 §6 (algorithm, output format, exemptions, gate)
- ADR 0005 §3 (agent geometry), D17 (`AgentRadius 45` erosion), D41 (`OnMeshHalfHeightCm`, `AirborneProjectRadiusCm` in `UVeloxBotSettings`), D44/D45 (projection pitfalls: nearest point behind a wall, points above the feet)
- `Tools/editor/README.md` "Bot navigation in the map scripts" (what exports to nav)

## Acceptance criteria
- [ ] `velox.bots.NavCoverage [Grid=50] [MinClusterM2=1]` runs on a server/standalone world after `VeloxNav built`, logs `VeloxNavCoverage walkable_m2=<x> covered_pct=<y> clusters=<n>` then one `VeloxNavCoverage cluster i=<k> m2=<a> center=(x,y,z) extent=(dx,dy) actor=<name> layout=<tag>` line per cluster, and `VeloxNavCoverage lowclear polys=<n>` for the D32 pass. Exemption: actors tagged `NavLintIgnore`.
- [ ] Samples use the D41 vertical extent (`UVeloxBotSettings::OnMeshHalfHeightCm`, `AirborneProjectRadiusCm`) and count a point as covered only when the projected point lies within `AgentRadius` horizontally and within step height vertically of the sample (so a ramp poly above a void does not count — D41's exact failure).
- [ ] Unit test `Velox.Bots.NavCoverageClusters`: grid points → clusters (4-connectivity), area in m², centre/extent; includes the "two clusters separated by one covered row" and "cluster below MinClusterM2 dropped" cases.
- [ ] `infra/ci/navlint.sh <Map> [--report-only]` runs `UnrealEditor-Cmd "$VELOX_PROJECT" <Map> -server -nullrhi -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -ExecCmds="velox.bots.NavCoverage"` with `timeout 120`, parses the lines, prints a summary and exits non-zero when `clusters > 0` or `lowclear polys > 0` (unless `--report-only`); kills its own child on timeout.
- [ ] Report-only run on `L_MovementTest`, `L_CombatTest`, `L_ItemTest` saved to `notes/03-report.md` with an interpretation per cluster (known D17 bed slot north of the YA ramp = expected; anything else = a finding for the lead, not fixed in this task).
- [ ] Build green (all tests), `fts.sh` 7/7.

## Verification steps
1. `infra/ci/build.sh` → new test listed and passing.
2. `infra/ci/navlint.sh /Game/Maps/L_ItemTest --report-only` → summary printed, exit 0; the same without the flag → exit code reflects the clusters.
3. Synthetic positive: temporarily spawn a 300 × 300 cm box at Z 170 over open floor in `L_CombatTest` through the generator (do not commit) → one cluster reported with its tag; remove it.
4. `infra/ci/fts.sh` → `OK 7/7`.

## Report back
Standard format plus the three summaries and the synthetic-positive line.

## Notes / outcome
