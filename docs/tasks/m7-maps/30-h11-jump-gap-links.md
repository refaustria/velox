# 30 (H11) — Bots fail JUMP links across gaps

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H8 (merged). Runs in parallel with H10 (different code path: link traversal, not combat); merge carefully.
**Wave:** bot track (lead-added 2026-11-13 from B2, docs/tasks/m7-maps/18-b2-nemus-build.md notes). **Blocks B4 (Fornax lava hops are JUMP links) and B2b.**

## Goal
B2 found that bots fail the two JUMP links across the 400 cm log breaks on L_Nemus_Blockout almost always (0/33 in FFA ×10 at 3 bots, 2/47 at 6 bots, 0/11 BotRun); all `link_timeout` stuck strikes sit on them. A probe with 300 cm gaps still gave 0/29, so the cause is bot-side, not map geometry. Root-cause and fix JUMP link execution generally (run-up, take-off point, jump timing, air control toward the link end, link-end acceptance), so gap jumps reachable by Velox movement succeed. Check that `JUMP_FAST` links (Culmen, Nemus) work the same way.

Evidence: docs/adr/evidence/0008-nemus-blockout/, raw logs under /mnt/oldroot/velox-scratch/m7-nemus/.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/*` (path following, link traversal, maneuver), `Source/Velox/Tests/VeloxBot*` (+ a new FT with a 300 and a 400 cm gap, e.g. on a generated test map or a new small one via `Tools/editor/create_*_test_map.py`), `infra/ci/fts.sh` rows
- this file

## Acceptance criteria
- [ ] Root cause stated with log evidence
- [ ] New FT: bot crosses a 300 cm and a 400 cm gap JUMP link in ≥ 9/10 seeded tries each
- [ ] Nemus FFA ×10 at 3 bots: JUMP link success ≥ 90 %, stuck within limit (re-run only that set; full B2b re-soak is a separate task)
- [ ] DM1 BotRun ×3 behaviour PASS; FtBotEdge/LavaAvoid/LavaEscape/KnockOff unchanged green
- [ ] build + full automation + fts.sh green; DM1 golden OK

## Notes / outcome
(Filled by the agent.)

**Root cause** (three bot-side defects, all in the JUMP / JUMP_FAST traversal; the hop landing guard and the edge overlay are not involved: both are skipped while a link traversal runs, and Nemus has no hazard volume):
1. *The flight held jump.* `TickAirborne` kept `bJumpHeld` for gap jumps; with `bAutoHop` the movement component re-jumps inside the landing frame (`ProcessLanded`), so the pawn is never on the ground at a bot tick and the landing is never seen. The bot hopped on through End (or in the pit) until the 6 s D28 timeout. Evidence: B2 log `s1/ffa6/ffa-1.log` 07:59:50–55, Rapidus `link=Jump:VeloxNavLink_42 ground=0` for 5 s, then `reason=timeout dt=6.02` at Z 500 (in the gap); every B2 stuck strike is `link_timeout` on the two JUMP links. Baseline FT_BotGapJump (old code): 0/40, every crossing `link=none fell=1`, the bot overshot the far platform hopping (e.g. trial 0 ended at x 1504 on a lane that ends at 1700, then fell off).
2. *Early take-off.* The jump fired at the radial `TakeoffLeadCm` (100 cm) from Start, i.e. 137–172 cm before the edge (baseline FT `takeoff_before_edge`); a 400 cm gap at 800 cm/s (0.675 s flight, 540 cm) needs the take-off within about 90 cm of it.
3. *Side approaches.* On Nemus, bots reach both take-offs from the side (Log_Land meets the log next to Jump_LogBreak's start; the Templum podium from any angle). With 1+2 fixed, the first Nemus runs still missed: `VeloxBotTakeoff ... along=775-789 speed=862-892` (350–470 cm/s across the link) drifted off the 400 cm log, `along=280 speed=878` cannot reach.

**Fix** (`Bots/VeloxBotManeuver.*`, `Bots/VeloxBotPathFollowingComponent.*`, `Bots/VeloxBotSettings.h`):
- the gap flight releases jump (the landing is seen) and steers with the ballistic arrive wish to End (as the pad / rocket-jump flights), which brakes sideways drift;
- take-off along the link axis: from `TakeoffLeadCm` before Start a down-trace ledge probe (`TakeoffLedgeProbeCm` 25 + this tick's step ahead, `TakeoffLedgeDepthCm` 60) fires the jump at the edge, at the latest `TakeoffMaxPastStartCm` (40) past Start;
- take-off check on the along-link speed the flight needs (`VeloxBotManeuverRules::GapTakeoffSpeed`, landing `TakeoffLandInsetCm` 60 short of End, air time to the end's level) and on the speed across the link (`TakeoffMaxLateralCmS` 250); too slow or too much across → back up along the link axis (run-up clamped to the nav mesh behind the start by a nav raycast; < 100 cm: none) and retry once; a JUMP out of retries jumps anyway, JUMP_FAST stays `tooslow`. Near Start an approach mostly across the link backs up at once;
- on a plain segment into a JUMP / JUMP_FAST link a hop whose landing passes `TakeoffMaxPastStartCm` (and does not clear the whole link) is refused (every map);
- one `VeloxBotTakeoff` log line per take-off / back-up decision (soak diagnostics).
JUMP_FAST shares all of it (same code path); its required-speed check is unchanged. No JUMP_FAST attempt occurred on Nemus (as in B2); Culmen is not built yet.

**New FT** `velox.bots.FtBotGapJump` on the new `L_GapTest` (`Tools/editor/create_gap_test_map.py`): lanes with a 300 and a 400 cm gap (1200 cm wide platforms, lava bed 300 cm down, both-ways JUMP link 60 cm before the edge -> 100 cm onto the landing, the Fornax / Nemus authoring rule; the way back takes off 100 cm before the edge), seeded starts 150–1100 cm before the edge and ±450 cm off the axis (side approaches), both directions, 10 per gap with the hazard behaviours on and again with `velox.bots.EdgeAware 0` (Nemus case). fts.sh row `FtBotGapJump`.

**Verification** (Engine 5.8.4, raw logs /mnt/oldroot/velox-scratch/m7-h11/):
- FT_BotGapJump: baseline (old bot code) 0/40 (`ft-base.log`); with the fix 10/10 per gap (300, 400) and mode (hazard behaviours on / `EdgeAware 0`) = 40/40, take-off 16 cm past to 46 cm before the edge, 13 of 40 crossings backed up for a straight run-up (`ft-fix4.log`, `ft-final.log`).
- Nemus FFA ×10 at 3 (`nemus-ffa3-set10.txt`; run 1 re-run as `nemus-ffa3-r1`, the first attempt segfaulted at start-up before writing a log): link_success[Jump] **100 % (40/40)** (B2 0/33), aborted 19 (below_start 8+, relaunched by machine-gun hits in the flight, replaced; not attempts); sum_stuck 3 (all `fight_stall`, none `link_timeout`; B2 33); JumpPad 100 % (159/159), Drop 97 %. Still failing, not H11: nav_build_s 3.08 s in one run (1.4–1.8 s in the others, host load 10–33) and item_coverage 30/41 (B2 28/41), per-run budget > 300 µs under host load; B2b re-soak.
- build + full automation: CI OK (`ci-full2.log`, incl. Velox.Bots.Maneuver with the new take-off / run-up / lateral cases); DM1, Nemus, Sacrarium golden byte-identical.
- fts.sh: OK 17/17 (`fts-all.out`; FtBotEdge, FtBotLavaEscape, FtBotKnockOff, FtBotLavaAvoid unchanged green, FtBotGapJump new).
