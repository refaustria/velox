# 08b — Task 08 review follow-ups: merge, ItemIndex base 1, log levels, pid= in evaluator lines

**Milestone:** M6 Online
**Agent:** dev
**Status:** done (2026-11-11)
**Depends on:** 08 (`wip/m6-08-spectator`), review `docs/adr/reviews/0007-08-review.md` (approve with follow-ups F1–F6)

## Goal
Task 08 merges into `m6-online` on a head that contains task 07 and release 0.6.0, with the review's low findings fixed and the listen-mode evaluator matching players by id instead of by name.

## Scope — files/modules the agent may touch
The files task 08 touched (Items registry, pickup, `VeloxPlayerState`, `VeloxPlayerController`, `VeloxGameMode*`, HUD timer reader, tests), `infra/ci/nettest.sh` (ItemGate evaluator), `docs/adr/0007-online.md` (append amendment **F3** from the review verbatim, after F2/F1.x), this file, notes/08-report.md.

## Work
1. **F1:** `git merge origin/m6-online` into `wip/m6-08-spectator`; expected conflicts in `VeloxGameModeMatch.cpp`, `VeloxPlayerController.h/.cpp`, `nettest.sh` `SCRIPTS_ARG` — keep both sides.
2. **F2:** `NextItemIndex = 1`, 0 = none; unit assertion in `Velox.Items.Replication` that the first index is 1; warn on wrap.
3. **F3:** `VeloxViewRefused` → Verbose (or once per spectate).
4. **F4:** the HUD calls `GetTimeUntilRespawn()` instead of duplicating its client branch; < 0 = unknown.
5. **F6 / 08c:** add `pid=<PlayerId>` to `VeloxItemTimerEntitlement`, `VeloxItemTimerPush` and the client `VeloxItemTimers` lines; the evaluator matches by pid (names with spaces break `kv()`); keep `local=`.

## Acceptance criteria
- [x] Editor build + all automation tests green on the merged head.
- [x] ItemGateRun ×1 dedicated + ×1 listen PASS, ItemRun PASS.
- [x] No Log-level `VeloxViewRefused` from a dead client's fire press.
- [x] ADR 0007 F3 appended.

## Constraints
Production is live on this host (ports 7777/8790, units `velox-server@ffa`, `veloxmaster`) — never touch them. Disk ≥ 12 GB free or pause.

## Notes / outcome
2026-11-11 (dev): F1 merge of origin/m6-online: one conflict, `nettest.sh` (SCRIPTS_ARG: took m6-online's list and appended
ItemGateRun; the `run_one` head: kept the ItemGateRun two-round block and then m6-online's RpcGateRun/PasswordRun case). Auto-merged
cleanly: VeloxGameModeMatch.cpp, VeloxPlayerController.h/.cpp, VeloxGameMode.*. F2: `NextItemIndex = 1`, wrap warns `VeloxItemIndexWrap`,
`FindPickupByIndex(0)` is null, assertions in `Velox.Items.Replication`. F3: `VeloxViewRefused` Verbose (ItemTimerGating row asserts
0 Log-level lines). F4: HUD uses `GetTimeUntilRespawn()` (local state/ServerNow duplication removed). F6: `pid=` on
VeloxItemTimerEntitlement, VeloxItemTimers (rep and tick) and `to=[name#pid]` in VeloxItemTimerPush; the evaluator matches by pid.
Evaluator fix found on the way: a push after the client's connection closed (client quits when Watch ends) has remote=0 and failed
the listen spectator round once; pushes after the first `UNetConnection::Close` following the client's entitlement are no longer judged.
Verification: build.sh --targets VeloxEditor green, 138/138 tests; ItemGateRun dedicated PASS, listen PASS (after the evaluator fix;
first listen attempt failed on that race), ItemRun PASS. ADR 0007 F3 appended.
