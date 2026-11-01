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
