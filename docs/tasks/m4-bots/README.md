# M4 Bots - task index

Spec: `docs/adr/0005-bots.md` (ADR 0005; implementation notes list the tasks). Template: `docs/tasks/TEMPLATE.md`.

| # | Task | Agent | Est. | Depends on | Wave | Status |
|---|---|---|---|---|---|---|
| 01 | [nav-foundation](01-nav-foundation.md) | dev-hard | 3 h | none | Wave 1 | done |
| 02 | [bot-pure-cores](02-bot-pure-cores.md) | dev-hard | 3 h | none | Wave 1 | done |
| 03 | [bot-content-and-scoreboard](03-bot-content-and-scoreboard.md) | dev | 2 h | none | Wave 1 | done |
| 04 | [bot-controller-navigation](04-bot-controller-navigation.md) | dev-hard | 3 h | 01, 02, 03 | Wave 2 | done |
| 04b | [bot-tunables-and-ci-fts](04b-bot-tunables-and-ci-fts.md) | dev | 2 h | 04, 05, 06 merged | Wave 3.5 (after the 05/06 merge, before 07) | done |
| 05 | [bot-combat](05-bot-combat.md) | dev-hard | 3 h | 04 | Wave 3 (parallel with 06) | done |
| 06 | [bot-item-routing](06-bot-item-routing.md) | dev-hard | 2-3 h | 04 | Wave 3 (parallel with 05) | done |
| 04c-a | [fight-balance](04c-a-fight-balance.md) | dev-hard | 2 h | 04b | Wave 3.6 (final review F1) | done (partial, see 04d) |
| 04c-b | [hop-headroom-and-progress-stuck](04c-b-hop-headroom-and-progress-stuck.md) | dev-hard | 2 h | 04b | Wave 3.6 | done (partial, open items) |
| 04c-c | [combat-events-ft-and-giveup](04c-c-combat-events-ft-and-giveup.md) | dev | 2 h | 04b | Wave 3.6 | done |
| 04d | [balance-followup](04d-balance-followup.md) | dev-hard | 2 h | 04c-a, ADR 0005 D20-D26 | Wave 3.7 (before 07) | done (D24 band via 04e) |
| 04e | [platform-nav-islands](04e-platform-nav-islands.md) | dev | 1-2 h | 04d | Wave 3.7 | done |
| 04f | [link-timeout-hop-gate-fight-stall](04f-link-timeout-hop-gate-fight-stall.md) | dev-hard | 2-3 h | 04c-b, 04d merged; ADR 0005 D27-D34 | Wave 3.7 (parallel with 04e, before 07) | done (its bands superseded by D36; open points ruled in D37) |
| 04g | [segment-blocked-repath](04g-segment-blocked-repath.md) | dev-hard | 2 h | 04e, 04f merged; ADR 0005 D35-D37 | Wave 3.8 (parallel with 07's tooling; before 07's acceptance rounds) | todo |
| 04j | [north-slot-seal](04j-north-slot-seal.md) | dev | 1-2 h | 04i, ADR 0005 D46 item 2 | Wave 3.8 (before the M4-07 rerun) | done |
| 07 | [bot-soak-and-playtest](07-bot-soak-and-playtest.md) | dev | 2 h | 04c-b, 04c-c, 04d, 04e, 04f; acceptance rounds on a head with 04g (D38) | Wave 4 | todo (may start now, D38) |
| 07b | [runner-host-classifier](07b-runner-host-classifier.md) | dev | 2 h | 07 tooling, ADR 0005 D46 | Wave 4 (before the acceptance rerun) | done |
| 08 | [bot-cost](08-bot-cost.md) | dev-hard | 3 h | 07b, the M4-07 rerun on an idle host (D46 1c cost FAIL) | Wave 4 | done (diagnosis; no bot fix possible, see outcome) |
| 07c | [process-slow-class](07c-process-slow-class.md) | dev | 1-2 h | 07b, 08, ADR 0005 D47 | Wave 4 (before the D47 cost rerun) | done |
| 07d | [transition-window-and-mega-band](07d-transition-window-and-mega-band.md) | dev | 1-2 h | 07c, the 046dabb acceptance logs (`a07d47`), ADR 0005 D48 | Wave 4 (closes M4 by `--reeval`; no UE run) | done |
| 07e | [botduel-round-failures](07e-botduel-round-failures.md) | dev-hard | 1 h | 07d, the 421548c live BotDuel x3 (`a07d48`) | Wave 4 (diagnosis) | done (two causes: open-top arena without KillZ -> 07f content; camp band -> D49) |
| 07g | [camp-set-band-and-left-arena](07g-camp-set-band-and-left-arena.md) | dev | 1-2 h | 07d, 07e, ADR 0005 D49; live acceptance also needs 07f (content: KillZ + clip walls) | Wave 4 (runner; verified by `--reeval`, no UE run; then the D49 (3) live acceptance closes M4) | todo |

## Wave plan (from ADR 0005)
- **Wave 1:** 01 || 02 || 03
- **Wave 2:** 04 (needs 01, 02, 03) -> `architect` review
- **Wave 3:** 05 || 06 (file ownership below) -> one `architect` review pass over 04/05/06
- **Wave 3.5:** 04b (review follow-up of 04: tunables into data, `infra/ci/fts.sh`; `docs/reviews/m4-01-02-04-review.md`)
- **Wave 3.6:** 04c-a/b/c (final review `docs/reviews/m4-05-06-04b-review.md`)
- **Wave 3.7:** 04d (ADR 0005 D20-D26); 04e || 04f (D27-D34)
- **Wave 3.8:** 04g (D35 segment line-of-walk check, D37 (d) single link budget) || 07's tooling, checklist and `BotWatch` mode (D38). The two soaks never overlap on the machine.
- **Wave 4:** 07's acceptance rounds on the head with 04g, ROADMAP M4 ticks, human playtest on the Apple Silicon Mac. Bands are judged on the repeat set (D36); the fix loop ends with 04g.

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
