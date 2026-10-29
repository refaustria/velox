# 01b — Items ↔ match-state integration, ItemRun in the default soak list

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** done (merged 9810c71)
**Depends on:** 01, 04, 05 (all merged)

## Goal
Wire the hand-offs between M3-01, M3-04 and M3-05 (`notes/01-report.md`, `04-report.md`, `05-report.md`).

## Scope
- `Source/Velox/Items/VeloxPickup.*`, `VeloxItemRegistrySubsystem.*` (read mode rules from `AVeloxGameState::GetGameModeDefinition()` — null in the base mode → FFA defaults; call `SetModeRules` when the definition is known; use `EVeloxItemTimerVisibility` from `GameModes/VeloxMatchRules.h`, do not redefine).
- `AVeloxPickup::IsPawnEligible`: refuse while the pawn's ASC has `Velox.State.MatchFrozen` (tag from `GameModes/VeloxMatchEffects.h`).
- `Tools/editor/create_item_test_map.py`: regenerate `L_ItemTest` with `AVeloxGameMode_FFA` (the script already switches when the class exists); only `L_ItemTest.umap` may change.
- `infra/ci/nettest.sh` + `infra/ci/README.md`: add `ItemRun` to the default script list and document it (log lines, time rules incl. the RTT-based limits and the one-slow-event tolerance).
- Tests: extend `Velox.Items.Pickup` with a frozen-pawn refusal row and a mode-rules row.

## Acceptance criteria
- [ ] `infra/ci/build.sh` green (60 tests expected: 59 + `Velox.Items.ItemTestMap`).
- [ ] `velox.items.FtItemPickup quit` on `L_ItemTest -game` PASS with the FFA mode active (log `Game class is 'VeloxGameMode_FFA'`).
- [ ] `infra/ci/nettest.sh --scripts ItemRun --repeat 3` 3/3 at the assigned port.
