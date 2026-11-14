# 33 (H13) — Bots fall off narrow decks into the void + B5b Orbita re-soak

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done except the dwell invariant (see notes)
**Depends on:** H12 (merged), H11 (merged; B5 ran before it)
**Wave:** bot track (lead-added 2026-11-13 from B5)

## Goal
B5 measured 182 self void deaths in FFA ×10 at 4 bots on L_Orbita_Blockout (gate Σ ≤ 10) and 10/19/22 per BotDuel round (gate 2). 42 were at the Pons Fractus jump gap (Jump 5 ok vs 137 aborted — B5 ran before H11's JUMP fix; re-measure first). The rest fall off the narrow decks (Hortus, Horreum, Armamentarium: 27 each) while hopping or strafing; 94 had an item goal, 85 a fight goal; airborne recovery fires but does not save them. Fix the edge behaviour (H4 overlay, H8 hop guard, H5/H8 recovery) so bots stay on narrow decks; then B5b: the full Orbita soak sets with ADR 0008 §5.3 (e) + hazard gate.

## Scope
- `Source/Velox/Bots/*`, `Source/Velox/Tests/VeloxBot*`, `infra/ci/fts.sh` rows, `docs/adr/evidence/0008-orbita-blockout/`, the B5 task file notes, this file

## Acceptance criteria
- [x] Root cause per death class (gap / deck edge hop / deck edge strafe / knockback) with log evidence
- [ ] (partly: deaths, credited, coverage, links PASS; max_dwell_s 6 FAIL) Orbita FFA ×10 at 4 bots: Σ hazard_death_self ≤ 10, credited reported; all map-independent invariants PASS; coverage ≥ 36/37
- [x] BotDuel ×3 on Orbita: hazard_death_self ≤ 2 per round (behaviour; cost windows: A/B rule)
- [x] FtBotEdge/LavaAvoid/LavaEscape/KnockOff/GapJump/Targets unchanged green; DM1 BotRun ×3 behaviour PASS; build + automation + fts.sh green
- [ ] B5 (e) ticked if Orbita passes (not ticked: dwell)

## Notes / outcome
dev-hard, branch `wip/m7-33-h13-void-edges` on origin/m7-maps (0011823), engine 5.8.4. Raw logs /mnt/oldroot/velox-scratch/m7-h13/
(set1..set3 = iterations, top level = final set; classify.py / recstat.py there).

**Death classes, H12 logs (128 self, before; `void-deaths-h12-classified.txt`).** From the first airborne tick of each fatal flight
(VeloxBotAirRecovery `vel` Z) and the hits in the 3 s before: **deck-edge walk/strafe on the ground 97** (Vz = 0 at deck level, 73 within
100 cm of the recovery target = just over the eroded edge; 89 of them in a fight), **deck-edge hop 31** (19 own hops latched foreign at
take-off, 8 bunny-hop chains latched late, 4 never recovered), **gap 0** (Jump 79/79 after H11), **knockback 0** (no hit of any kind
within 3 s of any self death). Fight 103 / item 24. Mechanisms: (1) the H4 overlay's Drop keeps closing in, and the H7/H8 back-off fired
for lava only: an enemy across a gap pulls the bot over the deck edge at 800 cm/s, the airborne recovery cannot bring that back;
(2) item paths/corners at 800+ cm/s with no ground-level edge check outside fights; (3) bAutoHop re-jumps in the landing frame, so the
3.6.2 take-off guard ran only for the first hop of a chain; (4) own hops with a void landing were left alone (H7 steered lava only).
Found while soaking: (5) jump pads crossed off their link (P6 on Trabs, hub P1) - the recovery steered the pad arc from a landing
predicted at the launch height and the bot fell short (6 of 8 self deaths in iteration 2); (6) the tower drops 650 -> 330 overshot
End (H12 14/17 missed; Drop crossed n = 30 and failed); (7) SetGoal reset the fight stand-off window on every Fight -> Fight
re-evaluation (every 2-4 s), so the 3 s stand-off never fired (the 6 s Armamentarium fight dwell of H12).

**Fix** (`Bots/*`, hazard maps only unless noted; A/B cvar `velox.bots.EdgeGuard 0` turns off the H13 parts alone):
- Ground edge guard (`AVeloxBotController::UpdateEdgeGuard`, last word in ApplyHazardBehaviours): grounded, off a link, not within
  `EdgeGuardLinkExemptCm` 300 of the start of the link ahead, standing on nav: probe Feet + Vel x `EdgeGuardLookaheadSeconds` 0.15 +
  Wish x `EdgeGuardWishCm` 40; `VeloxBotCombatGlue::IsUnsafeGround` (off nav at 45 cm AND lava or no floor within
  `EdgeGuardDropDepthCm` 800) -> wish = unit inward (to the nearest nav point) + the wish's part along the edge (`VeloxBotRules::EdgeGuardWish`), no hop.
- Overlay Drop: the remaining wish is probed with IsUnsafeGround (void too); with an item path blended the path wish stays (the
  reversal undid the path: corner dwells), else the H8 reversal / no-wish.
- Airborne hop guard (`UVeloxBotPathFollowingComponent::HasAirHopLanding`): descending in an own hop with jump held, within
  `AirHopGuardSeconds` 0.25 of touchdown: the next hop's landing unsafe -> jump released.
- Own hop with no ground under its landing -> latched foreign, the airborne recovery steers (`OwnHopRecoveries`).
- Off-link pad flight: Vz > 1.3 x JumpVelocity off a link next to a JumpPad link start -> ballistic arrive wish to that link's end
  (log `VeloxBotOffLinkPad`).
- Fight across the void: `FightEdgeBlockedSeconds` (back-off / no-wish / guard) >= `FightEdgeYieldSeconds` 2 yields like a stand-off
  (`VeloxBotFightYield ... reason=edge`).
- Every map: Drop flights steer the ballistic arrive wish (as pads / gaps); SetGoal keeps the stand-off window Fight -> Fight
  (`VeloxBotRules::KeepsFightStandoff`).
- Diagnostics: `VeloxBotFall bot= cause=walk|hop|dodge|knock|pad_offlink|link fight= takeoff= vel= ...` on every void death; the 1 Hz
  line ends with `edge_guard=episodes/ticks air_hop_released= own_hop_recovery=`.

**Tests.** `Velox.Bots.HazardRules` gains EdgeGuardWish and KeepsFightStandoff cases. FT_BotEdge `dummy=across` (fts row
`FtBotEdgeAcross`): the dummy strafes on the main floor across the 1000 cm gap. Seeds 1-3 PASS (0 deaths); A/B `EdgeGuard 0`: seed 3
void death at 57.6 s (1, 2 pass); `EdgeAware 0`: death at 1.9 s. FtBotEdge (platform) unchanged PASS.

**B5b, final build (Orbita, 4 bots; evidence `ffa4-set-b5b.txt`, `botduel-set-b5b.txt`, `void-deaths-b5b.txt`).**
- FFA x10: **hazard_death_self 5** (gate 10; H12 128, B5 182), credited 21; causes walk 3 (fight), dodge 1, pad_offlink 1.
  Iterations: 3 (set1, before the dwell/drop/pad fixes), 8 (set2), 2 (set3), 5 (final). Invariants: left_arena / world_kill /
  no_nav_point / giveup / bursts 0, stuck 0, nav 0.90 s, Drop **100 % (31/31)** (H12 15/29), Jump 100 % (94/94), JumpPad 100 %
  (102/102), coverage **36/37** (Armor_Shard (-300,-1000,239.6) untaken; it varies per set: set2 37/37, set3 35/37 two hub ammo
  boxes, H12 36/37). **max_dwell_s 6 FAIL** (2 runs: fights at the hub's centre (36,32) and NW (-1298,1351), not at an edge; H12: 6,
  set1 6 at Armamentarium's edge, which the edge yield removed). Per-run FAILs otherwise budget>300 (host load; band none).
- BotDuel x3: behaviour PASS, hazard_death_self **1 / 0 / 0** (gate 2; B5 10/19/22), credited 2/1/1, cost not judged (host).
- fts.sh OK 19/19 (FtBotEdge, FtBotEdgeAcross, LavaAvoid, LavaEscape, KnockOff, GapJump, Targets, FtMapReachability-Orbita).
- DM1 BotRun x3 (set3 build; the final change only touches hazard-map overlay code): **set behaviour PASS**, stuck 1/1/0, dwell 3/4/4,
  cost round 1 PASS, rounds 2/3 slow throughout (host, D47 d). Build VeloxEditor + Velox green, automation 144/144 (CI OK).

**Open.** max_dwell_s 6 on Orbita: hub fight stand-offs at 1 Hz spread <= 150 cm that the 4 Hz stand-off rule (all samples within 150
cm of now over 3 s) does not catch; a map-independent fight-dwell question (D30 / task 06b), not an edge one. Not chased (budget).
B5 (e) stays unticked on that invariant alone.
