# 04 — Greybox item test map, FT_ItemPickup, ItemRun

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** done (merged; Quad/Haste rows in 02/03; notes/04-report.md)
**Depends on:** 01 compiles for the final run (script authoring in wave 1 against §1 names); ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 2 h; Wave 1 (script) / Wave 2 (finish)

## Goal
Implement ADR 0004 implementation-notes task **`04-item-test-map`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Greybox item test map, FT_ItemPickup, ItemRun. The ADR sections §8 (map), §10 (FT_ItemPickup, ItemRun) are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `Tools/editor/create_item_test_map.py`
- `Source/Velox/Tests/VeloxItemFunctionalTest.cpp`
- `Content/Data/MoveScripts/ItemRun.json`, `infra/ci/nettest.sh`
- `Config/DefaultEngine.ini`, `Config/DefaultGame.ini` - `ServerDefaultMap`, `EditorStartupMap`, `+MapsToCook` (`GameDefaultMap` -> `L_MainMenu` is task 10)
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (§8 (map), §10 (FT_ItemPickup, ItemRun); §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] Map `L_ItemTest` loads `-game -nullrhi`.
- [ ] `FT_ItemPickup` green: pawn at 100 health on Health.Medium -> refused and visible; health 50 -> taken, 75, hidden, `RespawnServerTime` set; advance 35 s -> available; pawn standing on it -> taken at once; weapon pickup grants the fire spec; Quad pickup -> tag and x3 damage on a dummy; Haste -> `GetMaxSpeed` x 1.3 (these last two need 02/03); `ResetLevel` restores every item.
- [ ] `ItemRun` 5/5 at 100 ms / 2 %: client runs over Health.Small/Shard/Ammo in the grid: predicted hide within one frame, confirmation within 150 ms, attribute step within 200 ms, 0 corrections.
- [ ] `infra/ci/build.sh` green (`[ci] CI OK`, all `Velox.*` tests).

## Standing rules
- The nettest port is assigned by the lead; pass it via `nettest.sh --port <N>`. Do not pick your own. Soak only on an idle machine.
- Unity-build safe names: no file-scope `using namespace`, no duplicate file-local helper names across `.cpp` files (use anonymous namespaces with distinctive names).
- Tunables live in data assets (`UVeloxItemDefinition`, `UVeloxGameModeDefinition`, `UVeloxCombatSettings`, `UVeloxMovementSettings`, `UVeloxUISettings`, `UVeloxGameUserSettings`) or `.ini`, never as magic numbers.
- Every change is covered by a test (automation test or functional test).
- No commit by the agent; the lead commits. Leave the project compiling; end with `infra/ci/build.sh` showing `[ci] CI OK` (and `nettest.sh` where named). State explicitly if Unreal was unavailable.
- Report ADR disagreements instead of deviating.

## Verification steps
1. `infra/ci/build.sh` -> `[ci] CI OK`.
2. The headless/automation/soak runs named in the acceptance criteria, with the lead-assigned nettest port.

## Report back
Standard format for the agent type (result, what changed, verification with commands + output tail, open issues). Note explicitly if Unreal was not available to compile.

## Notes
- Map per §8: 60 x 40 m arena, 4 m walls, central 2 m cover cross, every item once in a labelled grid on the south wall, Mega Health and Red Armor on 3 m platforms via `AVeloxJumpPad`, Yellow Armor on a ramp, 50 m marked corridor with Haste at its start, Quad near the centre, 8 `APlayerStart`s, one `ACameraActor` tagged `Intermission`, `DefaultGameMode = AVeloxGameMode_FFA`. Reuse `M_Greybox_*`.
- Engine fact: `AGameModeBase::ResetLevel` (resets every controller and every actor whose `ShouldReset` is true, calls `AActor::Reset`), `APlayerState::Reset` (score to 0), `APawn::Reset` (a pawn without a controller or with a player-state controller is destroyed), `AGameMode::PlayerCanRestart_Implementation` (false unless `MatchState == InProgress`) (for the `ResetLevel` assertion).
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
