# 07 — LatchRun mid-air stray correction after a repaired move

**Milestone:** M1 Movement (follow-up found in the M2 soak)
**Agent:** dev-hard
**Status:** done (architect ratification of A35 pending)
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

## Outcome (2026-10-29)
- Cause: two-RPC loss burst around the take-off (A = ground move, dual B = ground pending 1.383429 + take-off 1.391763). RPC C carried the take-off as OldMove (A32 single slot); the server merged A+P+N into one 1/30 s move with N's jump flag → take-off at 1.358427 instead of 1.383429 (25 ms early) → 0.025 s × 433.3 cm/s = 10.83 cm, exactly the logged correction. Not the A31 class. Rate: over stored soak logs 2295 single and 914 double repairs (~26 % of losses are bursts, ~1.5 % lost RPCs).
- Fix: extra old-move slot in `FVeloxNetworkMoveDataContainer` (`ChooseExtraOldMove`, serialised after the engine fields, 1 bit when absent), simulated as an OldMove before Super; take-off jumps are event moves (`bJumpedThisMove` reset moved to `ControlledCharacterMove`/`MoveAutonomous`). Cost ~25–40 B/RPC (~2.4 KB/s per client).
- Tests: `Velox.Movement.SavedMove.ChooseExtraOldMove`, `.ExtraOldMoveSerialize`, `Velox.Functional.FT_TakeOffPartitionRepair` (pre-fix 10.83 cm reproduced, post-fix 0.0000 cm). build.sh 53 tests (`Saved/CI/20261029-150003-3396539`). LatchRun 20/20 (`Saved/CI/20261029-150212-3399666`), full list 3/3, 0 strays (`Saved/CI/20261029-151216-3413832`).
- Open: architect to ratify as ADR 0002 A35 (un-defers A32's second slot; updates A29/A32). Trade-off: while an event is unacked (~1 RTT) ordinary losses merge instead of repair (air moves, second order). Docs follow-up: `extra=` in `docs/movement-tooling.md` and `infra/ci/README.md`.
