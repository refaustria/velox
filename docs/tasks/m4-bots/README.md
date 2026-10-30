# M4 Bots - task index

Spec: `docs/adr/0005-bots.md` (ADR 0005; implementation notes list the tasks). Template: `docs/tasks/TEMPLATE.md`.

| # | Task | Agent | Est. | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| 01 | [nav-foundation](01-nav-foundation.md) | dev-hard | 3 h | none | Wave 1 | todo |
| 02 | [bot-pure-cores](02-bot-pure-cores.md) | dev-hard | 3 h | none | Wave 1 | todo |
| 03 | [bot-content-and-scoreboard](03-bot-content-and-scoreboard.md) | dev | 2 h | none | Wave 1 | todo |
| 04 | [bot-controller-navigation](04-bot-controller-navigation.md) | dev-hard | 3 h | 01, 02, 03 | Wave 2 | todo |
| 05 | [bot-combat](05-bot-combat.md) | dev-hard | 3 h | 04 | Wave 3 (parallel with 06) | todo |
| 06 | [bot-item-routing](06-bot-item-routing.md) | dev-hard | 2-3 h | 04 | Wave 3 (parallel with 05) | todo |
| 07 | [bot-soak-and-playtest](07-bot-soak-and-playtest.md) | dev | 2 h | 05, 06 | Wave 4 | todo |

## Wave plan (from ADR 0005)
- **Wave 1:** 01 || 02 || 03
- **Wave 2:** 04 (needs 01, 02, 03) -> `architect` review
- **Wave 3:** 05 || 06 (file ownership below) -> one `architect` review pass over 04/05/06
- **Wave 4:** 07, ROADMAP M4 ticks, human playtest on the Mac

File ownership in wave 3: 05 owns `Bots/VeloxBotCombat.*` and the controller's `UpdateCombat`/`Act` phases; 06 owns `Bots/VeloxBotItemRouting.*` and `UpdateGoal`; both append rows to `Tests/VeloxBotTests.cpp` in their own section. Neither edits the other's files; shared needs go to the lead.

## Standing rules (every task)
- The nettest port is assigned by the lead; pass it via `nettest.sh --port <N>`. Soak only on an idle machine.
- Bots touch the pawn only through the ADR 0005 section 1 entry points (`AddMovementInput`, `SetLocalMoveInput`, `SetJumpInput`, `Crouch/UnCrouch`, ability input tags, `SetControlRotation`). Never velocity, never `Launch`, never teleports.
- Every decision rule is a world-free function with a unit test before it is wired. Randomness only through an injected `FRandomStream`.
- Tunables live in `UVeloxBotDifficulty`, `UVeloxBotSettings`, link properties or `.ini`; never literals in code.
- Unity-build-safe names: no file-scope `using namespace`, anonymous namespaces with distinctive names.
- Engine facts: the ones listed as verified at the top of ADR 0005 may be relied on; verify anything else against `/opt/UnrealEngine-5.8/Engine/Source` before use and report mismatches.
- No commit by the agent; the lead commits. Leave the project compiling; end with `infra/ci/build.sh` showing `[ci] CI OK` (and `nettest.sh` where named). State explicitly if Unreal was unavailable.
- Report ADR disagreements instead of deviating.
