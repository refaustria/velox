# 10 (H5) — Airborne recovery, pad discipline, hazard escape

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done (dev-hard, 2026-11-13)
**Depends on:** H4
**Wave:** hazard track

## Goal
In-flight steering toward the nearest nav point at or below the bot's height, pad discipline on `JUMP_PAD` links, and the lava escape that runs ahead of `UpdateOffMeshRecovery`; gated by `HasHazards()`.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotController.*`
- `Source/Velox/Bots/VeloxBotManeuver.*`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §3.6 items 3-4, ADR 0005 (D17)
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `FtBotLavaEscape` green
- [ ] A bot knocked off the test platform edge with 300 cm/s lands back in >= 8 of 10 seeded trials
- [ ] DM1 bands unaffected (BotRun x 3 spot check)
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
dev-hard, 2026-11-13, branch `wip/m7-10-h5-airborne-recovery` (base origin/m7-maps), engine 5.8.4.

- Gate: `VeloxBotCombatGlue::IsEdgeAware(World)` (H4: `HasHazards()` and cvar `velox.bots.EdgeAware`, 0 = A/B off). Off hazard
  maps both new entry points return on the first line, so DM1 / test-map behaviour is the base's.
- Hazard escape (3.6.4, `AVeloxBotController::UpdateHazardEscape`, called in `EnsureMove` ahead of `UpdateOffMeshRecovery`):
  feet (+1 cm) in a Lava volume start it; it owns the move (StopMove, no path query) until the bot stands on the ground outside
  the lava (or `HazardEscapeMaxSeconds` 3). Target = nearest nav point outside every hazard volume within `HazardEscapeExtentCm`
  (400): one projection of the feet plus 4 rings x 16 small projections, each rejected when `point + HazardProbeUpCm` is in a
  volume, window feet -100 / +150 (the rim is above the bed); re-searched every 0.25 s; fallback the nearest side of the
  volume's box (`VeloxBotRules::HazardBoxExit`). Input (in `ApplyHazardBehaviours`, after Act): view yaw at the target,
  forward, jump pressed on the ground and released in the air (a held key auto-hops and the escape never sees the rim).
  Log `VeloxBotHazardEscape ... result=start|out|timeout`.
- Pad discipline (3.6.3): `TickAirborne` today wishes End at scale 1 without jump for a pad (unit-tested, kept for the base
  maps); `FVeloxBotManeuver::IsPadFlight()` (JumpPad + Airborne) and the controller zeroes the move input on hazard maps; aim,
  weapon and fire untouched.
- Airborne recovery (3.6.3, `ApplyHazardBehaviours`): only for a foreign flight, not on a link. Deviation from "IsFlight": a
  flight latched per airborne stretch by `VeloxBotRules::IsForeignFlight` = no jump key at takeoff (walked/knocked off), or a
  knockback grace seen, or IsFlight. With IsFlight alone (0.83 s) the bot would steer only after it is 300+ cm below the ledge;
  the own hop stays excluded. Each tick: landing = feet + v2D x `FlightSecondsToLevel` (to the last target's level, else the
  feet's), projected with `AirRecoveryExtentCm` 600 in the window [feet - 800, feet + 20] (D44 `ProjectReachable`); landing
  within `AirRecoveryOnCourseCm` (20) of the point = no steering, else wish = unit(point - landing), scale 1, jump released
  (VQ3 air accel is ~800 cm/s^2, so the correction direction matters more than feet -> point). Log `VeloxBotAirRecovery`.
- Tunables: `AVeloxBotController` Config (category Velox|Bot|Hazard, next to the D17 off-mesh ones): AirRecoveryExtentCm,
  AirRecoveryDownCm, AirRecoveryUpToleranceCm, AirRecoveryOnCourseCm, HazardEscapeExtentCm, HazardEscapeDirections,
  HazardEscapeUpCm, HazardEscapeDownCm, HazardProbeUpCm, HazardEscapeSearchSeconds, HazardEscapeMaxSeconds. Bot state:
  HazardEscapes/Out, AirRecoveryFlights/Ticks, PadDisciplineTicks.
- Tests (`Tests/VeloxBotRecoveryFt.cpp`, fts.sh rows `FtBotLavaEscape|L_HazardTest`, `FtBotKnockOff|L_HazardTest`):
  `Velox.Bots.HazardRules` (automation, world-free); FT_BotLavaEscape: bot put into the channel at 3 spots, out (grounded
  outside the volume) 0.62 / 0.62 / 0.62 s, limit 1 s; A/B EdgeAware 0: dies in the lava in the first trial.
  FT_BotKnockOff: 10 seeded trials (Crc32-hashed seed: side, place, inset 45..70, lift 300..675, +-20 deg), 300 cm/s
  horizontal + lift via AddKnockback(Server, override): landed 10/10 (two runs, both seedings); A/B EdgeAware 0: 0/10.
- Verification (soak lock): `build.sh --targets VeloxEditor,Velox` green, automation 144/144; `fts.sh` 12/12 (see below for
  the pad row); FtBotEdge seed 1 unchanged vs H4 (probes 601, mirror 110, drop 14, hops refused 152, 0 deaths);
  `create_map.py --map dm1 --golden check` OK. BotRun x 3 on L_DM1_Blockout: set behaviour PASS (stuck 1/0/0, left_arena
  none, world_kills 0, majors 4/4), cost FAIL on windows 1-3 of each round at frame_other_ref 2171-2251 us (host load 4-5 from
  other projects). A/B base sources (origin/m7-maps) on the same host, BotRun x 2: r1 cost FAIL windows 1,2,3 at
  frame_other_ref 2267 us (same pattern), r2 PASS at 1148 us when the host went quiet: the cost windows are the host, as in H4.
- Finding (out of scope, report-only row): on L_HazardTest the path from the main floor to the platform does not use
  `Pad_Gap`'s JUMP_PAD link (no `VeloxBotLink` line in base or H5); the bot runs over the pad off-link and overshoots the
  platform into the void (x ~2700). FT_BotKnockOff logs `report=pad` but does not judge it. Pad discipline is therefore proven by
  the unit test and the code path only; the fixture/link (H2 map or nav link) needs a look before B4 Fornax.
- Not done (out of scope): lava avoidance. H4's respawned bot walking into the channel is now escaped within ~0.6 s (about 75
  damage), but nothing keeps a path or the edge overlay out of the channel: the overlay's nav probe accepts a lava bed with mesh,
  and lava has no nav area cost. Needs `VeloxBotCombat` (probe rejects hazard points) and/or a nav area for hazard boxes.

