# 08 — Repair-slot attribution in the net soak (A35 instrumentation)

**Milestone:** M1 Movement (follow-up of M1-07)
**Agent:** dev
**Status:** done (merged into m2-combat; repaired_extra 1–15 per script)
**Depends on:** M1-07 (1287b23), ADR 0002 A35; review `docs/reviews/m2-08b-m1-07-review.md` ruling 9, defect 5

## Goal
The soak can tell which old-move slot repaired a loss, so a regression of the second slot (or of `ChooseExtraOldMove`) is visible as `repaired_extra=0` over a long run instead of as a rare stray. The comment that describes duplicate rejection names the engine line that actually fires.

## Scope — files/modules the agent may touch
- `Source/Velox/Movement/VeloxMovementComponent.cpp` — `MoveAutonomous` override (l.≈470–480): `VeloxMoveRepaired ts= dt= slot=old|extra` — `extra` when `GetCurrentNetworkMoveData() == VeloxNetworkMoveDataContainer.GetExtraOldMoveData()`, else `old`; comment at l.≈962: replace "A duplicate is rejected by VerifyClientTimeStamp (Log-level line)" with the engine reference (`ServerMove_PerformMovement`, `LogNetPlayerMovement` Log `ServerMove: TimeStamp expired`, Warning only beyond `net.NetServerMoveTimestampExpiredWarningThreshold`; visible with `log LogNetPlayerMovement Log`).
- `infra/ci/nettest.sh` — parse `slot=`, print `repaired_extra=N` after `repaired=N` and `resent_extra=N` (client `VeloxResend … extra=` lines with a stamp ≠ −1, or `GetNumExtraResentMoves()` in the `VeloxNetSoak` summary if the summary already carries it); the A34(ii) "loss simulation inactive" rule counts `repaired` (both slots).
- `docs/movement-tooling.md` (§ Net soak), `infra/ci/README.md` — the new columns; keep "ADR 0002 A32/A35".

## Relevant ADRs / references
- `docs/adr/0002-movement-component-design.md` A32, A34, A35
- UE 5.8 `CharacterMovementComponent.cpp` l.9967–10007

## Acceptance criteria
- [ ] Server log shows `VeloxMoveRepaired ts=… dt=… slot=extra` at least once in a 5-round full-list soak at 2 % loss (bursts are ≈ 26 % of losses — expect a handful per round).
- [ ] Every summary row prints `repaired=N repaired_extra=M resent_extra=K` with `M ≤ N`, `K ≥ 0.9 × resent` once past the first RPC after each ack reset.
- [ ] No new Log-level line per RPC; `grep -c "TimeStamp expired"` on a soak server log at default verbosity is 0.
- [ ] `infra/ci/build.sh` green; `infra/ci/nettest.sh --repeat 3` 3/3.
- [ ] No commit.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`.
2. `infra/ci/nettest.sh --repeat 3 --keep-going` → paste the rows; `grep -c "slot=extra"` on one server log.

## Report back
Standard format; quote one `slot=extra` line with the surrounding `VeloxMoveGap`/`ServerMove` context.

## Notes / outcome
