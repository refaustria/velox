# 01 — MaxRewindSeconds 0.30 → 0.35

**Milestone:** M4 follow-ups (human decision q19, 2026-10-30)
**Agent:** dev
**Status:** done
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
Done 2026-10-31. `MaxRewindSeconds` 0.30 -> 0.35 in the `UVeloxCombatSettings` default (`DA_CombatSettings_Default.uasset` does not serialise the field, so it follows the default; the `VeloxWeaponTests` row asserts 0.35 on the loaded asset), the `UVeloxLagCompensationSubsystem.h` default/comment, `create_bootstrap_content.py`, `docs/content-bootstrap.md` and an ADR 0003 B34 amendment. History: `HistoryWindowSeconds` 1.0 s, subsystem window = max(1.0, MaxRewind + 0.1) = 1.0 s >= 0.35 + 0.1 margin, unchanged. No other hard-coded 0.30 rewind assumptions (remaining 0.30 hits are bot ReactionSeconds). Results: build.sh CI OK 97 tests; fts.sh 7/7; nettest FireRun,FireRunControl,KnockbackRun --repeat 2 PASS 2/2 (FireRun rewind_med 133 ms, 11/11 hits, stamp 11).
