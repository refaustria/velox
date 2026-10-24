# ADR 0002 A27/A28 — evidence excerpts (architect, 2026-10-24)

Client log lines are `LogVeloxMovement`/`LogVeloxNet` with the prefix stripped. `t` = client world time, `ts` = move time stamp,
`svel` = |NewVelocity| in the server's response, `cm` = client error at the acked move before the replay.

## A27 — KnockbackGroundRun, `Saved/CI/20261024-205419-2120808/nettest/round-4` (FAIL, 2.01 cm stray)

```
VeloxNetSoak event=knockback t=2.223 st=1.000 impulse=V(X=1250.00) override=0
VeloxCorrectionResponse t=2.489 ts=1.825151 grace=0.183321 latch=0 svel=1250.0      <- intended; expiry = 2.008472
VeloxCorrection         t=2.489 cm=20.85 st=1.275 air=0 grace=0.000 latch=0 speed2d=752.0 vz=0.0
VeloxCorrectionResponse t=2.514 ts=1.891822 grace=0.116651 latch=0 svel=1250.0      <- echo, expiry 2.008473
VeloxCorrection         t=2.514 cm=0.00
VeloxCorrectionResponse t=2.598 ts=1.941825 grace=0.066648 latch=0 svel=1250.0      <- echo, expiry 2.008473
VeloxCorrection         t=2.598 cm=0.00
VeloxCorrectionResponse t=2.648 ts=2.025164 grace=0.000000 latch=0 svel=1250.0      <- 16.7 ms after expiry, NO friction yet
VeloxCorrection         t=2.648 cm=2.01  st=1.433 air=0 grace=0.000 latch=0 speed2d=316.9 vz=0.0
VeloxCorrectionResponse t=2.748 ts=2.125170 grace=0.000000 latch=0 svel=679.3       <- 1250*(1-6*0.005556)^18 = 679.1: friction for exactly 0.100 s
VeloxCorrection         t=2.748 cm=0.00
VeloxCorrectionResponse t=2.856 ts=2.241845 grace=0.000000 latch=0 svel=333.1       <- 679.3*(1-6*0.005556)^21 = 333.0
```
Client's 12th move (grace 0 at start, friction): 1129.6/60 = 18.83 cm; server's: 1250/60 = 20.83 cm; difference 2.0 cm = the stray.
Only a merged server move >= 50 ms (two `GetSimulationTimeStep` iterations both using `MoveStartKnockbackGrace`) yields svel = 1250.0;
a single-iteration 1/30 merge would give 1152.

Control, `Saved/CI/20261024-204010-2100838/nettest/KnockbackGroundRun-client.log` (unchanged partition):
```
VeloxCorrectionResponse t=2.701 ts=1.825135 grace=0.183332 svel=1250.0              <- expiry 2.008467
VeloxCorrectionResponse t=2.859 ts=1.991813 grace=0.016654 svel=1250.0
VeloxCorrectionResponse t=2.892 ts=2.041817 grace=0.000000 svel=1019.9              <- 1250*(1-6*0.005556)^6 = 1019.8: friction from the first move after expiry
```

## A28 — PadRun, launch one move apart

`Saved/CI/20261024-211007-2142599/nettest/round-8` (pad 3, 10.23 cm):
```
client  VeloxJumpPad: pad=VeloxJumpPad_3 role=autonomous t=12.103 launch=V(X=1014.19, Z=1183.22)     <- live launch
client  VeloxCorrectionResponse t=12.320 ts=11.442511 grace=0.000000 latch=0 svel=800.0              <- server still walking at the end of the client's launch move
client  VeloxJumpPad: pad=VeloxJumpPad_3 role=autonomous t=12.320 ...                                <- A19 teleport overlap (AddKnockback ignored)
client  VeloxReplay t=12.328 start=11.450845 expected=11.450845 moves=25
client  VeloxCorrection t=12.320 cm=10.23 st=10.884 air=1 grace=0.000 latch=0 speed2d=1014.2 vz=749.9
client  VeloxCorrectionResponse t=12.428 ts=11.559184 grace=0.083327 latch=0 svel=1389.5             <- 0.2 - 14/120: server launch move started at 11.442511
client  VeloxJumpPad: pad=VeloxJumpPad_3 role=autonomous t=12.428 ...                                <- A19 teleport overlap again
client  VeloxCorrection t=12.428 cm=0.00
server  VeloxJumpPad: pad=VeloxJumpPad_3 role=authority t=22.129 launch=V(X=1014.19, Z=1183.22)
```
`Saved/CI/20261024-205419-2120808/nettest/round-1` (pad 3, 10.09 cm): `VeloxCorrectionResponse t=12.149 ts=11.235016 grace=0.000000 svel=800.0`, then `ts=11.376696 grace=0.058320 svel=1355.8`.
`Saved/CI/20261024-205419-2120808/nettest/round-5` (pad 4, 10.86 cm): `VeloxCorrectionResponse t=2.802 ts=1.858470 grace=0.183332 svel=1765.4` (= 0.2 - 2/120; |(1264.9, 1264.9 - g/60)|), `cm=10.86 = |(1264.9-800, 1264.9)|/120`.

In all three the server's move that ends at the client's launch-move end spans the client's entry move and launch move: the entry move's RPC was lost and merged.

## Verbose PadRun soak run for this analysis (`Saved/CI/20261024-214012-2173390`, `p.NetShowCorrections 1`, `LogNetPlayerMovement VeryVerbose`)

10 rounds, 2 FAIL (rounds 2 and 6), 8 PASS (max stray 2.77 cm). Server move-length histogram, round 1 (1643 `ServerMove Time` lines):
```
1136 x 0.0167  (combined 1/60 ground moves)      15 x 0.0333 (one lost RPC)        4 x 0.0500 (two consecutive lost RPCs)
 446 x 0.0083  (1/120 air moves)                   3 x 0.0417                        1 x 0.1333 (script start)
```
The dts are exact multiples (0.016667/0.016668): the client derives them with the server's own subtraction; there are no 25 ms moves.
Gap neighbourhoods (ts dt): `4.183622 0.016667 -> 4.233629 0.050007`, `13.427025 0.016667 -> 13.477030 0.050005` — three moves merged.

Round 2 (pad C = VeloxJumpPad_5, 15.34 cm), client then server:
```
VeloxJumpPad: pad=VeloxJumpPad_5 role=autonomous t=21.925 launch=V(Z=1649.24)
VeloxCorrectionResponse t=22.175 ts=21.279549 grace=0.000000 latch=0 svel=800.0
VeloxCorrection t=22.175 cm=15.34 st=22.650 air=1 grace=0.000 latch=0 speed2d=200.0 vz=1149.2
VeloxCorrectionResponse t=22.259 ts=21.362888 grace=0.116660 latch=0 svel=1484.1       <- 0.2 - 10/120: server launch move started at 21.279549

ServerMove Time 21.246214 Vel X=800 Position X=394.331 Z=72.150 DeltaTime 0.016668
ServerMove Time 21.279549 Vel X=800 Position X=420.999 Z=72.150 DeltaTime 0.033335      <- merged: entry move lost, sweep crosses the trigger, overlap at its end
ServerMove Time 21.287882 Vel X=6.667 Z=1632.576 Position X=421.027 Z=85.824 DeltaTime 0.008333   <- launch consumed one move after the client
```
Round 6 (pad A = VeloxJumpPad_3, 10.13 cm):
```
VeloxJumpPad: pad=VeloxJumpPad_3 role=autonomous t=12.127 launch=V(X=1014.19, Z=1183.22)
VeloxCorrectionResponse t=12.352 ts=11.484168 grace=0.191666 latch=0 svel=1545.8       <- 0.2 - 1/120: server launched in the move ending 11.484168
VeloxCorrection t=12.352 cm=10.13 st=10.901 air=1 grace=0.000 latch=0 speed2d=1014.2 vz=699.8

ServerMove Time 11.459167 Vel X=800 Position X=399.576 DeltaTime 0.050004               <- two lost RPCs while walking (harmless)
ServerMove Time 11.475834 Vel X=800 Position X=412.909 DeltaTime 0.016666               <- lost 1/120 entry move 11.467501 merged into the launch move; overlap at its end
ServerMove Time 11.484168 Vel X=1014.185 Z=1166.548 Position X=421.362 DeltaTime 0.008334   <- launch consumed one 1/120 move after the client
```

