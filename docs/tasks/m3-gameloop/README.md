# M3 Game loop - task index

Spec: `docs/adr/0004-game-loop-items-modes-ui.md` (ADR 0004; implementation notes list the tasks). Template: `docs/tasks/TEMPLATE.md`.

| # | Task | Agent | Est. | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| 01 | [items-foundation](01-items-foundation.md) | dev-hard | 3 h | none | Wave 1 | todo |
| 02 | [powerups-damage](02-powerups-damage.md) | dev-hard | 2-3 h | 01 | Wave 2 (parallel with 03) | todo |
| 03 | [haste-movement](03-haste-movement.md) | dev-hard | 2-3 h | 01 (parallel with 02) | Wave 2 | todo |
| 04 | [item-test-map](04-item-test-map.md) | dev | 2 h | 01 compiles for the final run (script authoring in wave 1 against §1 names) | Wave 1 (script) / Wave 2 (finish) | todo |
| 05 | [match-state](05-match-state.md) | dev-hard | 3 h | none (uses GE_MatchFrozen from 02 if merged) | Wave 1 | todo |
| 06 | [scoreboard-data-feed](06-scoreboard-data-feed.md) | dev | 2 h | 05 | Wave 3 | todo |
| 07 | [hud](07-hud.md) | dev | 3 h | 01, 02, 06 | Wave 3 | todo |
| 08 | [scoreboard-intermission-ui](08-scoreboard-intermission-ui.md) | dev | 2 h | 06, 07 | Wave 4 (parallel with 10) | todo |
| 09 | [settings-backend](09-settings-backend.md) | dev | 2 h | none (parallel with 07, wave 1 capable) | Wave 1 / finishing in Wave 3 | todo |
| 10 | [menus](10-menus.md) | dev | 2-3 h | 07, 09 | Wave 4 (parallel with 08) | todo |
| 11 | [gauntlet](11-gauntlet.md) | dev | 1 h | none (optional, parallel) | Wave 1 | todo |
| 12 | [playtest-checklist](12-playtest-checklist.md) | dev | 1 h | all others (last) | Wave 5 | todo |
| 13 | [gameplay-review-fixes](13-gameplay-review-fixes.md) | dev | 2 h | 01, 02, 03, 05 merged; `docs/reviews/m3-gameplay-review.md` | Wave 5 (before 12) | todo |

## Wave plan (from ADR 0004)
- **Wave 1:** 01 || 05 || 04 (script authoring) || 09 || 11
- **Wave 2 (after 01):** 02 || 03; 04 finishes
- **Wave 3 (after 05):** 06, then 07 (needs 01, 02, 06) || 09 finishing
- **Wave 4:** 08 || 10
- **Wave 5:** 12 and one `architect` review pass over 01, 02, 03, 05 together (GAS, replication, movement)

Separate `nettest.sh --port` per parallel agent (assigned by the lead); soak only on an idle machine.

## Standing rules (every task)
- The nettest port is assigned by the lead; pass it via `nettest.sh --port <N>`. Do not pick your own. Soak only on an idle machine.
- Unity-build safe names: no file-scope `using namespace`, no duplicate file-local helper names across `.cpp` files (use anonymous namespaces with distinctive names).
- Tunables live in data assets (`UVeloxItemDefinition`, `UVeloxGameModeDefinition`, `UVeloxCombatSettings`, `UVeloxMovementSettings`, `UVeloxUISettings`, `UVeloxGameUserSettings`) or `.ini`, never as magic numbers.
- Every change is covered by a test (automation test or functional test).
- No commit by the agent; the lead commits. Leave the project compiling; end with `infra/ci/build.sh` showing `[ci] CI OK` (and `nettest.sh` where named). State explicitly if Unreal was unavailable.
- Report ADR disagreements instead of deviating.

## Lead decisions on ambiguities (2026-10-29)
- **04 item test map** completes with the item basics (refuse/take/respawn, ItemRun). The Quad ×3 and Haste ×1.3 assertions of `FT_ItemPickup` move into the acceptance of **02** (Quad) and **03** (Haste); each adds its row to `FT_ItemPickup` when it merges.
- **`UVeloxGE_MatchFrozen`** is owned by **05** (wave 1). 02 uses it, does not define it.
- **09 settings backend** runs in wave 1. It owns `bAutoSwitchOnPickup` in `UVeloxGameUserSettings`; 01 reads it through a getter on `AVeloxPlayerController` and defaults to on until 09 merges. Expect small merges in `VeloxPlayerController` (05, 06, 08, 09) — the lead resolves them.
- **11 Gauntlet** is deferred until after wave 3 (optional, not on the M3 critical path).
- **`PickupPredictionTimeout`** lives in `UVeloxCombatSettings`; 01 may add it (02 also edits the file; lead merges).
- **Ports**: every task uses the nettest port given at dispatch; task 10's "7777" means "the assigned port".
- **nettest.sh** edits by 01/03/04: each adds its script block only; the lead merges.
- ADR 0004 defaults are used for the human's open questions (STATUS question 21) until answered.
