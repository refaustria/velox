# 07 — LatchRun mid-air stray correction after a repaired move

**Milestone:** M1 Movement (follow-up found in the M2 soak)
**Agent:** dev-hard
**Status:** todo
**Depends on:** M2 merged (e25d333)

## Goal
Find and fix the cause of a rare non-echo mid-air correction in LatchRun at 100 ms one-way / 2 % loss.

## Evidence
- `Saved/CI/20261029-141000-3335522/nettest/round-4/LatchRun-*`: 10.83 cm mid-air correction at ts 1.4918 after a repaired move at ts 1.3918 (`VeloxMoveRepaired`). 5 standalone reruns passed (`Saved/CI/20261029-144239-3378021`).
- 2026-10-29 04c run under load: LatchRun `C=2 max=26.39`, "expected >= 2 landing observations, got 1" (`Saved/CI/20261029-115612-3168251`).
- Related ADR 0002 amendments: A17 (derived state chained through replay), A28/A29 (event moves resent), A31 (merged move during speed change), A32 (OldMove selection / repaired moves), A34. Memory of earlier lessons: partition invariance, chained derived state.

## Scope
- `Source/Velox/Movement/*`, `Source/Velox/Tests/VeloxSavedMoveTest.cpp`/movement tests, `infra/ci/nettest.sh` only for diagnosis output.

## Acceptance criteria
- [ ] Root cause stated with log evidence (which state differs between the repaired server move and the client's move at 1.3918–1.4918: latch, jump, velocity, partition).
- [ ] Fix with a unit test reproducing the partition/repair case, or a documented proof that it is the A31 class with a measured rate and a decision proposal.
- [ ] `infra/ci/build.sh` green; `infra/ci/nettest.sh --scripts LatchRun --repeat 20 --keep-going` 20/20 and full default list `--repeat 3` 3/3 on an idle machine.
