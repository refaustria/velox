# 12 — Playtest checklist, ROADMAP ticks, STATUS

**Milestone:** M3 Game loop
**Agent:** dev
**Status:** todo
**Depends on:** all others (last); ADR 0004 (and accepted ADRs 0001-0003)
**Estimate:** 1 h; Wave 5

## Goal
Implement ADR 0004 implementation-notes task **`12-playtest-checklist`** (`docs/adr/0004-game-loop-items-modes-ui.md`): Playtest checklist, ROADMAP ticks, STATUS. The ADR sections ADR 0004 task 12 are the specification; this file pins scope and acceptance.

## Scope — files/modules the agent may touch
- `docs/playtests/m3-gameloop-checklist.md`
- `ROADMAP.md` (M3 ticks), `STATUS.md`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0004-game-loop-items-modes-ui.md` (ADR 0004 task 12; §10 Tests; §11 Budgets)
- `docs/adr/0003-combat-gas-and-lag-compensation.md`, `docs/adr/0002-*.md`, `docs/adr/0001-*.md` as cited by ADR 0004

## Acceptance criteria
- [ ] Checklist for the Mac (dedicated server + 2 clients at 100 ms / 2 %) covers: pickups and sounds, Mega/RA timers as spectator, Quad/Haste/Regen/Suit feel, warmup -> countdown -> live -> end -> restart, scoreboard, feed, menu connect, settings persistence, keybind rebind.
- [ ] ROADMAP M3 ticks and STATUS updated.
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
- Docs only. Wave 5 also includes one `architect` review pass over 01, 02, 03, 05 together (lead dispatches).
- Engine facts in ADR 0004 are cited from memory: verify each against the UE 5.8 source before relying on it and report a mismatch instead of improvising.
