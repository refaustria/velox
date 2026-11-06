# ADR 0007 section 5.3: movement time-discrepancy detection (M6 task 07), 2026-11-01

Host: the shared Debian VM, editor binary as the dedicated server (`nettest.sh`), 100 ms one-way / 2 % loss both ways, under the
soak lock. Detection was **already on since M1** (`Config/DefaultGame.ini`); task 07 added `MovementTimeDiscrepancyDriftAllowance=0.0`
and the `VeloxTimeDiscrepancy` line. "Off" = `VELOX_NETTEST_SERVER_ARGS=-ini:Game:[/Script/Engine.GameNetworkManager]:bMovementTimeDiscrepancyDetection=False`.

## StrafeRun + PadRun x10

| detection | rounds passed | non-echo corrections (sum) | strays <= 2.5 cm (sum) | max correction | VeloxTimeDiscrepancy |
|---|---|---|---|---|---|
| off (baseline, run first) | 10/10 | 2 (one per script) | 0 | 2.67 cm | n/a |
| on (committed config) | 10/10 | 0 | 0 | 0.00 cm | 0 |

Logs: `Saved/CI/20261101-115112-1524067` (off), `Saved/CI/20261101-121307-1565812` (on). Neither set got worse with detection on.

## Full default list, detection on (`--keep-going`, one round)

16/17 PASS: StrafeRun, PadRun, DM1Run, KnockbackRun (intended 23.0 cm), KnockbackGroundRun, LatchRun, CrouchRun, FireRun,
FireRunControl, RocketJumpRun (0 non-echo corrections), ItemRun, HasteRun, BotDuel, DeathSpectateRun, RpcGateRun, PasswordRun.
**BotRun FAIL on cost only**: windows 9, 11, 12, 14 were slow with no host cause (D46 1c/D47). Its behaviour was in band (kills 11,
pickups 47, stuck 0, dwell 3 s). This is a single round, not a `--repeat 3` set, and nothing in this task touches the bot path. It
was not re-run because the project paused (wind-down). `VeloxTimeDiscrepancy` lines across the whole set: 0. Logs:
`Saved/CI/20261101-130256-1653258`.

KnockbackRun 10/10 (the acceptance's own set) was **not** run.

## Speed-hack proxy (`SpeedHackRun`, client `velox.move.DebugTimeScale`), x travelled in 10 s of real time

| client scale | detection | dx in 10 s | rate | vs honest | server `VeloxTimeDiscrepancy` lines |
|---|---|---|---|---|---|
| 1.0 (honest) | on | 2006.9 cm | 199.8 cm/s | - | 0 |
| 1.3 | off | 2596.6 cm | 259.2 cm/s | **+29.7 %** | 0 |
| 1.3 | on | 1595.1 cm | 159.2 cm/s | **-20.3 %** | 14 (about one per 0.8 s) |

With detection off, the hack gains the full 30 %, which meets the >= 20 % criterion and shows that the `-ini:` override works. With
detection on, the hacker gains nothing, but ends up 20 % **slower** than an honest client. The acceptance asked for "within 5 %".
The engine resolves at `MovementTimeDiscrepancyResolutionRate=1.0`: once the +0.25 s margin is crossed, it pays back the whole
excess, the client is corrected (62 non-echo corrections, 640 gaps in that run), and the time it spends being corrected costs it
more ground. Getting to "within 5 %" would need a lower resolution rate (for example 0.5). That is a new ADR knob, so the lead or
architect should rule on it.

Line format (server, LogVeloxNet Warning, once per resolution start):
`VeloxTimeDiscrepancy controller=VeloxPlayerController_0 player=Player t=11.814 payback_s=0.238 lifetime_raw_s=0.259 lifetime_s=0.9 count=1`

Logs: `Saved/CI/20261101-123519-1606894` (honest), `20261101-124024-1617083` (1.3 on), `20261101-125713-1643142` (1.3 off).

## 2026-11-06 re-run with ADR 0007 F1.1 (`DriftAllowance` 0.005, server-side measurement)

Same host and settings (100 ms one way / 2 % loss, editor binary as dedicated server, soak lock). Logs under
`Saved/CI/` of the task-07 worktree.

### StrafeRun + PadRun x10, detection on, `DriftAllowance=0.005`

| rounds passed | non-echo corrections (sum, 20 rows) | strays <= 2.5 cm (sum) | max correction | VeloxTimeDiscrepancy |
|---|---|---|---|---|
| 10/10 | 1 (PadRun, round 9) | 0 | 2.62 cm | 0 |

Against the 2026-11-01 "on" set (0 / 0 / 0.00 cm): one 2.62 cm correction, the size of the "off" baseline's (2.67 cm); Σ strays
unchanged at 0. Logs: `20261106-094548-2455546`. KnockbackRun x10: 10/10 PASS (intended 23.0 cm, one round 25.4 cm), logs
`20261106-101501-2586206`. RpcGateRun,PasswordRun x3: 3/3 each (`20261106-104233-2718634`).

### Speed-hack proxy, server-side displacement (`VeloxTimeScale role=server`), 10 s of real time from the first sample in motion

| client scale | detection | dx_server | vs honest (server) | dx_client | VeloxTimeDiscrepancy | logs |
|---|---|---|---|---|---|---|
| 1.0 (honest) | on | 1998.3 cm | - | 1974.6 cm | 0 | `20261106-110544-2791199` |
| 1.3 | on | 1588.8 cm | **-20.5 %** | 1513.0 cm | 15 | `20261106-110837-2802155` |
| 1.3 | off | 2591.7 cm | **+29.7 %** | 2599.2 cm | 0 | `20261106-111123-2811636` |

The rows printed by nettest in these runs used the first `speed2d > 1` sample as the window start, which gave `dx_server=n/a`
for the "on" run (the server reports `speed2d=0.0` whenever its 1 Hz sample falls into a payback window). The committed rule
(first sample whose position moved by 1-600 cm since the previous one; the 600 cm cap skips the script's teleport) was applied to
the same logs offline; the numbers above are from that rule.

**Result against F1.1:** off +29.7 % (>= 20 %: met); on: >= 5 resolutions (15: met), client-side <= honest (1513 <= 1975: met),
**server-side within +-5 % of honest: not met (-20.5 %)**. The engine's time accounting matches F1.1's model (15 resolutions
1.0-1.1 s apart, `payback_s` 0.23-0.25, `lifetime_raw_s` +0.30 s per cycle), but the server-side pawn advances ~157 cm/s, the same
deficit as the client view. So the server does not simulate "~1.083 s of movement per cycle"; the loss is on the server as well.
The hack still gains nothing; it costs the cheater ~20 %. Root cause not pinned (see the task-07 report, open item 1).

### Diagnostic: 1.3 on with the engine's per-move log (`p.DebugTimeDiscrepancy 2`, `20261106-114444-2964374`)

Report-only (the round FAILs by design: nettest fails on the engine's discrepancy warnings). With the committed window rule
nettest itself printed `dx_server=1493.2 dx_client=1497.6 time_discrepancies=15`. Server-side sums over the run
(script `tdcheck.py` in the task scratch dir, not committed):

| quantity | value |
|---|---|
| moves (detection lines) / of them while resolving | 911 / 246 |
| Σ ClientDelta / Σ ServerDelta | 20.00 s / 15.41 s (1.298x) |
| Σ ClientDelta of the resolving moves | 5.73 s |
| Σ DeltaOverride (time simulated while resolving) / Σ TimeToPayBack | 0.21 s / 3.46 s |
| simulated time (non-resolving ClientDelta + DeltaOverride) / server time | 14.26 s / 15.41 s = **0.94** |

Two server-side losses, both outside F1.1's model:
1. **Accounting (about -6 %).** F1.1 assumes the time simulated per cycle equals the real time. Measured: 0.94. The client time sent
   during each payback (about 0.3 s per cycle) is neither simulated nor counted, and the payback (Σ 3.46 s) is larger than the
   excess accumulated outside resolution.
2. **Standstill after each payback (the rest, about -14 %; likely, not proven).** Nearly every payback move runs with `DeltaOverride`
   = `MIN_TICK_TIME` (1e-6 s) (Σ DeltaOverride is 0.21 s over 246 moves). That is a 2e-4 cm move, below `MoveComponent`'s minimum sweep distance. `PhysWalking`'s "make
   velocity reflect actual move" then sets `Velocity = 0`. The server samples that fall inside a payback read `speed2d=0.0`
   while the position keeps advancing between samples. So the server pawn starts from standstill again after each of the
   15 resolutions.

Neither loss reaches honest players: they never enter resolution (0 lines over every honest set).
