# 07f — KillZ and sealed arenas for the test maps

**Milestone:** M4 Bots
**Agent:** dev
**Status:** done (verified)
**Depends on:** 07e

## Goal
A rocket knockback must not carry a pawn out of a test arena and let it fall ~105 s until the engine default KillZ (07e,
round 3). Out-of-map pawns die at once; the open-top arenas are sealed.

## Scope — files/modules the agent may touch
- `Tools/editor/create_{item,combat,movement}_test_map.py`, `Content/Maps/L_ItemTest|L_CombatTest|L_MovementTest` (regenerated), this file.

## Acceptance criteria
- [x] `WorldSettings.KillZ = -1000` on all three maps (lowest walkable floor: Z=0 on L_ItemTest/L_CombatTest, Z=-100 gap pit top on L_MovementTest).
- [x] L_ItemTest and L_CombatTest: invisible `BlockAll` clip walls (coinciding with the 400 cm perimeter walls, 0..1800 cm) and a clip ceiling at 1800..1900 cm
      (`Clip_*` actors, folder `Clip`, hidden in game, `CanEverAffectNavigation=false`; the navmesh is unchanged).
- [x] L_MovementTest: KillZ only (decision below).
- [x] build.sh 97 tests, fts.sh 7/7, nettest.sh default list (see outcome).

## Notes / outcome
- 1800 cm clears the highest knockback apex seen (1875 cm/s at 2000 cm/s^2 gravity = about 880 cm) with a wide margin.
- L_MovementTest has no perimeter walls at all (open slab 6600 x 11200 cm, gap runways to x -4910, walled corridor); a launch there leaves via
  the open sides, not over a 400 cm wall, and sealing it would need a different, much larger box that must also clear the highest ramp tops (about 1230 cm)
  and the pad arcs (pad A apex 420 cm). Not sealed; KillZ -1000 (800 cm below the pit floor at -200) kills a pawn that leaves the slab within about 1 s.
  If a ceiling is wanted later it must be above 1230 cm + knockback apex.
- Generators log `WorldSettings.KillZ = -1000.0`; the item/combat counts gain `clip=5`.
- Verification: `build.sh` CI OK, 97 tests; `fts.sh` 7/7; `nettest.sh` default list (rounds=1): 12/13 PASS in the full run, `BotRun` failed its
  *cost* band only (behaviour fine: kills 12, pickups 56, stuck 1; mean_us 211, 12/14 clean windows over 2000 us, load1 1.91 at start, right after the
  movement scripts); standalone rerun `BotRun` PASS (kills 14, mean_us 64, 0/6 clean windows over 2000 us). StrafeRun, PadRun and RocketJumpRun PASS on the
  regenerated L_MovementTest. Cost-band noise, not map related (the clip boxes add no navigation and the BotDuel/BotRun path costs are unchanged).
