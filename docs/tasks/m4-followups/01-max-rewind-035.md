# 01 — MaxRewindSeconds 0.30 → 0.35

**Milestone:** M4 follow-ups (human decision q19, 2026-10-30)
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0003 (B34), STATUS question 19

## Goal
Raise the lag-compensation rewind cap from 0.30 s to 0.35 s as the human decided, so players with up to ~350 ms of combined latency still get rewound hits. Everything that is sized from the cap (history buffer length, timeouts, tests) must follow.

## Scope — files/modules the agent may touch
- `Source/Velox/Combat/VeloxCombatSettings.h` and the matching `Config/*.ini` entry — the value
- Lag-comp history sizing in `Source/Velox/Combat/` or `Source/Velox/Net/` only if it is derived from or must cover the cap
- `Source/Velox/Tests/VeloxWeaponTests.cpp` (B34 row) and other tests that hard-code 0.30
- `docs/adr/0003-*.md` — a one-line amendment note under B34
(Anything else: report back.)

## Acceptance criteria
- [ ] `MaxRewindSeconds` is 0.35 in code default and config; no remaining hard-coded 0.30 rewind assumptions (grep for 0.30/0.3f near rewind/history).
- [ ] The lag-comp history keeps at least 0.35 s plus its existing margin at the server tick rate; state the numbers.
- [ ] `infra/ci/build.sh` CI OK (97+ tests), `infra/ci/fts.sh` 7/7, `infra/ci/nettest.sh --scripts FireRun,FireRunControl,KnockbackRun --repeat 2` all PASS.

## Verification steps
1. `infra/ci/build.sh`
2. `infra/ci/fts.sh`
3. `infra/ci/nettest.sh --scripts FireRun,FireRunControl,KnockbackRun --repeat 2`

## Report back
Standard format.

## Notes / outcome
