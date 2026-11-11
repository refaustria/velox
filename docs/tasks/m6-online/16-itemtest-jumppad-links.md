# 16 — L_ItemTest JumpPad links 0/4: explain and fix the miss rate

**Milestone:** M6 Online (bot/map follow-up from ADR 0007 F2)
**Agent:** dev
**Status:** done
**Depends on:** ADR 0007 F2 ruling 1, ADR 0006 E0.3 (b) ("a pad has no reason to miss"), ADR 0005 (link outcomes)

## Goal
The bots' JumpPad link rate on `L_ItemTest` is ≥ 95 % because every miss on `VeloxNavLink_0` / `VeloxNavLink_4` is explained from the landing offset and the pads or link endpoints are corrected — not the 90 % threshold or the runner.

## Context
Gate run 2026-11-06: all JumpPad misses are `reason=missed` on those two links in dedicated (23/25), listen (35/39) and packaged (22/25); pooled 186/200 = 93.0 %. Evidence: `docs/adr/evidence/0007-listen-gate/gate-20261106/`.

## Scope — files/modules the agent may touch
- `L_ItemTest` generation (the map library / generator script that places the pads and nav links), its nav-link endpoints, pad launch data if per-pad
- `infra/ci/` only for a diagnostic print (landing point vs link end) if missing
- Never `Source/Velox/Movement/` or the lag-compensation path; no threshold changes

## Acceptance criteria
- [x] A table per miss: link, launch point, predicted vs actual landing, offset to the link end, cause.
- [x] Fix applied; navlint and the map's FT still pass.
- [x] BotDuel ×3 + BotRun ×3 on L_ItemTest (dedicated, idle host): JumpPad ≥ 95 % observed, every set PASS.
- [x] Editor build + all automation tests green.

## Notes / outcome

### Outcome (2026-11-11)
Method: a temporary (not committed) log line at the end of every traversal gave feet, velocity and distance to the link end. Baseline, 6 rounds on L_ItemTest (dedicated): JumpPad 24/25 and 30/32, all landings of the 150 cm radius test passed with 55-112 cm except these misses:

| Link | Launch | Predicted landing | Actual landing | Offset to link end | Cause |
|---|---|---|---|---|---|
| _0 (Mega) | pad fired with the bot's feet well above the floor (bunny-hop through the 140 cm trigger) | x -1519 (19 cm short of the end) | x -1320, on the platform, 12 cm from the far edge (off the eroded nav, so the end-surface rule did not apply) | 180 | launch origin is fixed, the pawn is not: a feet height h adds sqrt((30+h)/1000)-0.173 s of flight at 1044 cm/s (about +195 cm for h=100) |
| _4 (RA) | same class, or a pad fired in a duel by an extra impulse | x 2081 | x 2565, on the floor past the platform | 465 | overshoot beyond the platform (higher/longer flight) |
| _4 (RA) | none: bot was mid bunny-hop 150 cm before the pad when the traversal began, landed, dt 0.02 | - | x 1147 on the floor | 1005 | bot approach (state machine counts a landing after a hop as a miss); not a pad fault (bot code, out of scope; did not recur in the 57 attempts after the fix) |

The gate-evidence misses (dt 0.6-1.6 s on _0 / _4) are the same flight-offset class (feet-height and entry offset); the pad launch velocity (1044, 1149) and predicted landing (19 cm short of the centre) were already right.

Fix (generator only): the L_ItemTest pads use a 80 x 80 x 40 cm trigger resting on the floor (`PAD_TRIGGER_EXTENT` in `create_item_test_map.py`, new optional `trigger_extent` of `veloxmap.jump_pad`; other maps unchanged). The launch origin is the trigger centre, so the target point drops by the same 50 cm: launch velocity and landing point are bit-identical (compute_landing_point -1519.29 / 2080.71). A pawn can no longer fire the pad with its feet above 40 cm, entry offset is at most 75 cm instead of 85. Measured after: landings dx -84..+74, max distance to the link end 101 cm (was 180+). Link actors are renumbered by the regeneration (the pad links are now _15/_16..), nothing keys on the old names.

Verification: BotRun x3 and BotDuel x3 dedicated: set PASS both, JumpPad 26/26 and 31/31 (100 %, 57/57). Editor build + all automation tests green; fts.sh all 8 PASS; navlint L_ItemTest result=FAIL with clusters=11 is identical on the unmodified map (pre-existing, not touched by this task). `audit LIGHTING FAIL` is L_DM1 not baked in this worktree.
