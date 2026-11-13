# 34 (H14) — Take-off speed control on short gap hops + B4b Fornax re-soak

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** H13 (merged; both touch Bots/*)
**Wave:** bot track (lead-added 2026-11-14 from B4, docs/tasks/m7-maps/20-b4-fornax-build.md notes)

## Goal
On L_Fornax_Blockout the lava hop `Isle_In` succeeds only 71/89 (79.8 %): bots take off at the ledge at ~950 cm/s, fly ~600 cm and overshoot the 300 cm RA island into lava. Lead decision (reversible): fix it bot-side, not by removing the link — H11's take-off logic already checks the along-link speed the flight needs; extend it so that on gap hops whose landing zone is short the bot sheds excess speed before take-off (or brakes in the air with the ballistic arrive wish), so it lands inside the link-end zone. General fix (also helps narrow-deck gaps on Orbita). Then B4b: re-run the Fornax sets.

## Scope
- `Source/Velox/Bots/*` (maneuver / path following / settings), `Source/Velox/Tests/VeloxBotGapFt.cpp` (+ a short-landing case, e.g. 300 cm island after a 250 cm gap, entered at full bhop speed), `Tools/editor/create_gap_test_map.py` + `Content/Maps/L_GapTest.umap` if the FT needs new geometry, `infra/ci/fts.sh` rows, B4 evidence/notes, this file

## Acceptance criteria
- [ ] New FT case: short-landing hop entered at ≥ 900 cm/s lands inside the zone in ≥ 9/10 seeds
- [ ] Fornax FFA ×10 at 3 bots: every JUMP link ≥ 90 % (Isle_In listed), item coverage 30/30, all invariants PASS, hazard gate PASS; BotDuel ×3 PASS
- [ ] FtBotGapJump 300/400 cm unchanged 10/10; other bot FTs green; DM1 BotRun ×3 behaviour PASS; build + automation + fts.sh green
- [ ] B4 (e) ticked if Fornax passes (BotRun's BotWatch world kills are fixed in H9; judge BotRun on bot behaviour if H9 is not merged yet)

## Notes / outcome
(Filled by the agent.)
