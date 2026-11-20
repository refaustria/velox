# 34 (H14) — Take-off speed control on short gap hops + B4b Fornax re-soak

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done (B4b: FFA fails only max_dwell 6 and item coverage 28/30, see notes)
**Depends on:** H13 (merged; both touch Bots/*)
**Wave:** bot track (lead-added 2026-11-14 from B4, docs/tasks/m7-maps/20-b4-fornax-build.md notes)

## Goal
On L_Fornax_Blockout the lava hop `Isle_In` succeeds only 71/89 (79.8 %): bots take off at the ledge at ~950 cm/s, fly ~600 cm and overshoot the 300 cm RA island into lava. Lead decision (reversible): fix it bot-side, not by removing the link — H11's take-off logic already checks the along-link speed the flight needs; extend it so that on gap hops whose landing zone is short the bot sheds excess speed before take-off (or brakes in the air with the ballistic arrive wish), so it lands inside the link-end zone. General fix (also helps narrow-deck gaps on Orbita). Then B4b: re-run the Fornax sets.

## Scope
- `Source/Velox/Bots/*` (maneuver / path following / settings), `Source/Velox/Tests/VeloxBotGapFt.cpp` (+ a short-landing case, e.g. 300 cm island after a 250 cm gap, entered at full bhop speed), `Tools/editor/create_gap_test_map.py` + `Content/Maps/L_GapTest.umap` if the FT needs new geometry, `infra/ci/fts.sh` rows, B4 evidence/notes, this file

## Acceptance criteria
- [x] New FT case: short-landing hop entered at ≥ 900 cm/s lands inside the zone in ≥ 9/10 seeds
- [ ] Fornax FFA ×10 at 3 bots: every JUMP link ≥ 90 % (Isle_In listed), item coverage 30/30, all invariants PASS, hazard gate PASS; BotDuel ×3 PASS
- [x] FtBotGapJump 300/400 cm unchanged 10/10; other bot FTs green; DM1 BotRun ×3 behaviour PASS; build + automation + fts.sh green
- [ ] B4 (e) ticked if Fornax passes (BotRun's BotWatch world kills are fixed in H9; judge BotRun on bot behaviour if H9 is not merged yet)

## Notes / outcome
dev-hard, branch `wip/m7-34-h14-short-gap` on origin/m7-maps (07ceafb), engine 5.8.4. Raw logs /mnt/oldroot/velox-scratch/m7-h14/
(misses.py / pair.py there); evidence `docs/adr/evidence/0008-fornax-blockout/` (`*-b4b.txt`, `h14-*.txt`).

**Before (H13 code, FFA x10 at 3, `base-ffa3`).** Isle_In 47/55 (85.5 %; B4 71/89), Jump 91.3 %. The misses are not mainly
overshoots: 2 into lava from slow take-offs (along 457-483 < need 465, no run-up), 6 *on* the island but off its eroded nav and
> 150 cm from End (e.g. Rapidus take-off along 832 + ~360 across, landed at (-1656, 241), 9 cm from the island's north edge).
Mechanism: Medium bots bunny-hop at the corner **before** the take-off segment (H11's hop overrun guard only covers the segment
into the link) and land on the take-off with the hop's speed and diagonal direction; Isle_In has `run_up=0` (every take-off line),
so H11's back-up cannot straighten it, and the flight's arrive wish at End spends most of its ~800 cm/s^2 air braking on the
along speed, not the sideways drift.

**Fix** (`Bots/VeloxBotPathFollowingComponent.*`, `Bots/VeloxBotManeuver.*`, `Bots/VeloxBotSettings.h`; every map, only on
JUMP / JUMP_FAST links whose landing zone is short; A/B: `TakeoffLandProbeCm=0` turns it all off):
- landing zone probe: one nav raycast from End along the link (`TakeoffLandProbeCm` 400) gives `LandBeyondEndCm` (Isle_In 132;
  -1 = not short); cap = `GapTakeoffMaxSpeed` (land `TakeoffLandFarInsetCm` 40 short of the far nav edge; Isle_In ~690 cm/s),
  used only when cap > the H11 need;
- no hops within `TakeoffBrakeLeadCm` 250 of a short-gap start, also from the segment before the take-off segment
  (`HopIntoShortGap`); on the take-off segment above the cap: coast;
- ground correction in the last `TakeoffLeadCm` and in the approach: wish against (take-off velocity - velocity), take-off
  velocity = along the link at its speed clamped to [need, cap], nothing across (`ShortGapGroundWish`);
- air (take-off tick and flight): `ShortGapAirWish` - the full sideways arrive error, along only what leaves the zone
  [End - 60, End + beyond - 40];
- maneuver rule (world-free): above the cap in Approach -> WishScale 0 (`ShouldShedTakeoffSpeed`); `VeloxBotTakeoff` gains
  `max= beyond= brake=`.

**FT.** `FtBotGapJump` gains the island lane (`create_gap_test_map.py`, L_GapTest regenerated: 250 cm lava gap, 300 x 300
island, lava behind, one-way link -60 -> 350, a 250 cm take-off platform so there is no run-up room, as on Fornax) and mode
`isle` (10 trials, `gaps=0 / isle=0` options): the settled bot gets a ground velocity of 1000-1100 cm/s, 300-400 of it across,
15-90 cm before the take-off. Medium, A/B: H13 behaviour 9/10, H14 10/10 (`h14-gapft-isle.txt`). With the entry 0-25 cm before
the take-off (no ground tick, an artificial worst case once hops there are refused) both fail (2/10 vs 3/10): the air wish alone
cannot remove 340 cm/s across in a 0.67 s flight. fts row (Hard): gaps 10/10 x 4 unchanged, isle 10/10, min entry 1003.

**B4b, H14 build** (`ffa3-set-b4b.txt`, `botduel-set-b4b.txt`, `h14-isle-in.txt`):
- FFA x10 at 3: **Isle_In 57/61 (93.4 %)** (2 lava, 2 on-island misses), Isle_Out 45/50 (90.0 %; misses with along -800..0 =
  hit / stopped at the take-off, `beyond=-1`: H14 does not touch it; base 38/39), other JUMP links 7/7; link_success[Jump]
  92.4 %, Drop 100 %, JumpPad 100 %; hazard gate PASS (self 1 <= 6); stuck 0, world_kill 0, nav 0.83 s. **FAIL** on
  max_dwell_s 6 (one run, Fulgur at (2013,-682) goal=item, the east gallery, not a gap) and item_coverage 28/30
  (Ammo_Lightning (1750,1000), Health_Small (-1500,-1125); minors, different ones each set as in B4); per-run budget>300 (host).
- BotDuel x3: **set behaviour PASS, cost PASS**, Jump 35/38 (92.1 %), hazard self [0,0,0].
- DM1 BotRun x3: **set behaviour PASS, cost PASS** (stuck 0/0/0, kills 6/7/7, majors 4/4, world_kills 0).
- build + automation CI OK (144 tests incl. Velox.Bots.Maneuver with the new cap / coast cases), fts.sh OK 20/20.

**Open.** B4 (e) stays unticked: max_dwell 6 (1 run, map-independent fight/item dwell question as in H13) and minor-item
coverage 28/30 (lead's ruling pending from B4). Why Isle_In's run-up clamps to 0 (nav raycast from the start back along the
link stops within 140 cm) was not root-caused; H14 works without it.
