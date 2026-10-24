# ADR 0002 A31/A32 — evidence excerpts (architect, 2026-10-24)

Prefixes stripped. `t` = client world time, `ts` = move time stamp (end of the move), `svel` = |NewVelocity| in the server's
response, `cm` = client error at the acked move before the replay, `P(x,z)` = position X/Z, `V(x,z)` = velocity X/Z.

## A31 — KnockbackGroundRun 1.97 cm, `Saved/CI/20261024-221246-2212613/nettest/round-5`

```
VeloxCorrectionResponse t=2.690 ts=1.808576 grace=0.183332 svel=1250.0     <- intended; expiry 1.991908
VeloxCorrection         t=2.690 cm=20.83
VeloxCorrectionResponse t=2.849 ts=1.958586 grace=0.033322 svel=1250.0     <- echo, expiry 1.991908
VeloxCorrectionResponse t=2.924 ts=2.025257 grace=0.000000 svel=1019.8     <- 1250*(1-6/180)^6 = 1019.9: friction from the expiry, both ends
VeloxReplay             t=2.932 start=2.033590 expected=2.033590 moves=16  <- the client's next move is a 1/120 move
VeloxCorrection         t=2.924 cm=0.00                                     <- positions equal at 2.025257
server: VeloxMoveGap ts=2.058592 dt=0.033335                                <- two client moves merged, the only gap in the run
VeloxCorrectionResponse t=3.049 ts=2.125263 grace=0.000000 svel=553.6      <- 1019.8*0.96^5*(1-6/180)^12 = 553.6: server velocity correct
VeloxCorrection         t=3.049 cm=1.97 speed2d=32.8
```
Server over [2.025257, 2.058592]: one iteration, N=5, H=6.667 ms, end 1019.8*0.96^5 = 831.5, displacement 831.5*0.033335 = 27.72 cm.
Client: 1/120 (N=2, factor 0.950625) -> 969.5, 1/120 -> 921.6, 1/60 (0.903296) -> 832.5; displacement 8.08+7.68+13.87 = 29.63 cm.
Difference 1.91 cm + 0.06 cm (1.0 cm/s end-speed difference over 0.0667 s) = 1.97-1.98 cm.

## A31 — KnockbackGroundRun 2.02 cm, `Saved/CI/20261024-224316-2265935/nettest/round-5`

```
VeloxCorrectionResponse t=2.487 ts=1.841804 grace=0.183332 svel=1250.0     <- expiry 2.025136
VeloxCorrectionResponse t=2.629 ts=1.975148 grace=0.049988 svel=1250.0     <- echo
server: VeloxMoveGap ts=2.041819 dt=0.066671                                <- four client moves merged: two 33.3 ms PhysWalking iterations
VeloxCorrectionResponse t=2.712 ts=2.058487 grace=0.000000 svel=1019.7     <- A27 holds: friction started at 2.025136 on both ends (client 1019.9)
VeloxCorrection         t=2.712 cm=2.02
```
Server iteration 2 [2.008483, 2.041819]: grace 16.65 ms then friction (substeps 6.667 ms: 3.349 + 2*6.667 ms), end 1128.9,
displacement 1128.9*0.033336 = 37.63 cm. Client: moves ending 2.025149 (v=1249.9, 20.83 cm) and 2.041816 (v=1129.0, 18.82 cm) = 39.65 cm.
Difference 2.02 cm. With the A31 trapezoid: server 40.645 cm vs client 40.641 cm (0.004 cm).

## A31 — error jumps at merged ground moves, verbose PadRun soak `Saved/CI/20261024-230737-2302792` (server dt > 0.020, Walking)

| round | merged ts | dt | client moves in the span (end speed) | client/server error before -> after | model (v1 - v2)/60 |
|---|---|---|---|---|---|
| 1 | 10.109095 | 0.0333 | 560.5, 506.3 | 0.00 -> 0.91 | 0.90 |
| 1 | 12.552040 | 0.0333 | 111.6, 86.6 | 0.97 -> 1.06 | 0.42 (partly opposite to the existing error) |
| 1 | 16.504032 | 0.0500 | 110.8, 85.8, 60.8 | 1.06 -> 0.78 | 1.25 opposite |
| 1 | 5.392099, 8.825683, 14.627181, ... | 0.0333-0.0500 | 800, 800 (cruise) or 0, 0 | unchanged | 0 |
| 2 | 1.225083 | 0.0333 | 759.6, 800 (accelerating) | 0.00 -> 0.67 | 0.67, server ahead |
| 2 | 9.192361 | 0.0333 | 304.5, 275.0 | 0.40 -> 0.80 | 0.49 |
| 3 | 3.508601 | 0.0333 | 411.4, 371.6 | 0.00 -> 0.67 | 0.66 |
| 3 | 5.100380 | 0.0333 | 554.9, 630.2 | 0.71 -> 1.46 | 1.25 |

Errors below 1.73 cm are never corrected: round 1 carries 1.06 cm from ts 12.55 to 15.89, round 2 carries 1.12 cm from 16.42 to the end.
The cruise entries show that the velocity update itself is partition-invariant (0.00 -> 0.00 after a 50 ms merge at 800 cm/s).

## A32 B1 — the entry move's resend arrives one RPC too late, verbose soak rounds 1-3 and 10 (12 of 12 entries)

Round 1, pad A (client then server):
```
ClientMove Time 2.025141 V(800,0) P(406.203,72.150) Mode Walking  DualMove? 0
ClientMove Time 2.033475 V(800,0) P(412.871,72.150) Mode Walking  DualMove? 0     <- entry move CD, its own single-move RPC
ClientMove Time 2.041809 V(1264.9,1248.2) P(423.413,82.623) Mode Falling DualMove? 0  <- launch move E, flushed alone next frame (bForceNoCombine)
ClientMove Time 2.058477 ... Mode Falling DualMove? 1

ServerMove Time 2.033475 ... DeltaTime 0.008334 Mode Walking
ServerMove Time 2.041809 V(1264.9,1248.2) ... DeltaTime 0.008334 Mode Falling        <- E processed; its RPC carried no OldMove
ServerMove: TimeStamp expired: 2.033475, CurrentTimeStamp: 2.041809                  <- first duplicate of CD: in the RPC after E's
ServerMove Time 2.050143 ... / ServerMove Time 2.058477 ...
ServerMove: TimeStamp expired: 2.033475, CurrentTimeStamp: 2.058477 ... (repeated every RPC until acked, 12-15 lines, one RTT)
```
Same sequence at 11.443633/11.451966 and 21.275452/21.283785 (round 1), 2.016796/2.025131, 11.425866/11.434200,
21.251366/21.259699 (round 2), 2.041814/2.050147, 11.450948/11.459281, 21.284864/21.293197 (round 3), 21.276501/21.284834 (round 10).
Engine: `ReplicateMoveToServer` l.8941 `for (i = 0; i < NumSavedMoves-1; i++)` excludes the newest saved move from the OldMove
search; `CanDelaySendingMove` l.13024 returns false for `bForceNoCombine`, so the launch move is sent in the next frame while the
entry move is still the newest saved move.

## A32 B2 — a 1.43 cm sub-threshold offset flips the trigger crossing, verbose soak round 10 (FAIL, 14.64 cm)

```
client  ClientMove Time 21.259830 V(800,0) P(399.818,72.150)
client  VeloxJumpPad: pad=VeloxJumpPad_2 role=autonomous t=21.975 ts=21.276501 src=live applied=1 launch=V(Z=1649.24)
client  ClientMove Time 21.276501 V(800,0) P(413.154,72.150)                 <- client enters the trigger at the end of this move
client  ClientMove Time 21.284834 V(6.667,1632.576) P(413.182,85.824)        <- client launch move

server  ServerMove Time 21.259830 V(800,0) P(398.388,72.150) DeltaTime 0.016666
server  ServerMove Time 21.276501 V(800,0) P(411.724,72.150) DeltaTime 0.016670   <- NOT merged; server is 1.43 cm behind, not yet in the trigger
server  VeloxJumpPad: pad=VeloxJumpPad_2 role=authority ts=21.284834 src=server applied=1
server  ServerMove Time 21.284834 V(800,0) P(418.391,72.150) DeltaTime 0.008333   <- server enters one move later
server  ServerMove: TimeStamp expired: 21.276501, CurrentTimeStamp: 21.284834     <- the resend, as in B1, too late and not needed here
server  ServerMove Time 21.293167 V(6.667,1632.576) P(418.418,85.824)            <- launch consumed one 1/120 move after the client

client  VeloxCorrectionResponse t=22.209 ts=21.284834 grace=0.000000 svel=800.0
client  VeloxCorrection t=22.209 cm=14.64 air=1 vz=1182.5
```
`Saved/CI/20261024-222646-2236681/nettest/round-17` (15.20 cm, the brief's case) has the same signature: `svel=800.0` at the client's
launch stamp 21.037687, no gap at the entry, and two merges (`VeloxMoveGap ts=19.891003`, `ts=20.760731`) during the preceding leg
with speed changes (correction at 19.039721 with `svel=555.2`, `post_correction_sample t=20.381 speed2d=413.1`).
