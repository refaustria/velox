# 02b — Switch retarget hardening (follow-ups from the B42 review)

**Milestone:** M4 follow-ups
**Agent:** dev-hard (items 1–2), dev (items 3–4)
**Status:** done (dev-hard, 2026-10-31; items 1–4)
**Depends on:** task 02 merged (dc3115f), ADR 0003 B42, `docs/adr/reviews/0003-B42-review.md`

## Goal
Close the four defects of the B42 review without changing the B42 rule: the client's echo/correction decision becomes a world-free, unit-tested function; a rejected retarget restores the previous target instead of dropping the whole switch; the owning client resets its switch state at a new life; the nettest server-side retarget count ignores local (bot / listen-host) retargets.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxWeaponInventoryComponent.{h,cpp}` — `OnRep_CurrentIndex`, `CancelLocalSwitch`, new `ResetLocalSwitchState()`, pre-retarget bookkeeping
- `Source/Velox/Weapons/VeloxFireTiming.h` (or a new `VeloxSwitchPrediction.h` beside it) — the world-free decision
- `Source/Velox/Weapons/VeloxGameplayAbility_WeaponSwitch.cpp` — only `BindLocalRejection` / `ApplyRetarget` bookkeeping for item 2
- `Source/Velox/Character/VeloxCharacter.cpp` — one call in `InitAbilityActorInfo` next to `ResetShotSeq()`
- `Source/Velox/Tests/VeloxWeaponTests.cpp` — new rows
- `infra/ci/nettest.sh` — item 4
- `docs/adr/0003-combat-gas-and-lag-compensation.md` — a B43 amendment of ≤ 10 lines ratifying the changes
(Anything else: report back.)

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` B18, B33, B37, B42
- `docs/adr/reviews/0003-B42-review.md` D1–D4

## Acceptance criteria
- [x] **1 (D4).** `OnRep_CurrentIndex`'s decision is a world-free function (e.g. `VeloxSwitchPrediction::ClassifyReplicatedIndex(Predicted, bLive, Auth, Stale) -> Confirm | EchoOfOlderStep | Correction(rep) | Correction(timeout)`), the OnRep only applies the result; `Velox.Combat.SwitchPrediction` tests: confirm; echo of an older step stands and trims the chain up to that step; a replicated value not in the chain while live → rep correction; expired → timeout; predicted == replicated while a chain is live → confirm, no log; chain bounded at 4 (oldest evicted). A report-only `nettest.sh --scripts FireRun --lag 125 --repeat 1` run is attached to the report (corrections reported, not asserted).
- [x] **2 (D1).** A server-rejected retarget (`ClientRejectSwitch` or the rejected delegate for a retarget key) restores the previous pending target when the switch is still pending (drop timer and `SwitchEndTime` untouched); a rejected non-retarget switch still rolls back fully. Unit row: the decision is world-free or driven through a test seam; the `Inventory` D4 rows and `SwitchRetargetLocal/Remote` stay green.
- [x] **3 (D2).** `UVeloxWeaponInventoryComponent::ResetLocalSwitchState()` clears pending index, drop timer, `SwitchEndTime`, predicted index + chain and `LocalSwitchKey`; called from `AVeloxCharacter::InitAbilityActorInfo` on the owning client's first init of a new pawn (next to `ResetShotSeq`). `VeloxWeaponSwitchCorrection` is logged at Verbose with `reason=dead` while the ASC has `State_Dead`. Unit row: a pending client switch + predicted index survive nothing across `ResetLocalSwitchState()`.
- [x] **4 (D3).** `nettest.sh` counts server retargets with `mode != local` only; FireRun/FireRunControl detail line unchanged otherwise.
- [x] `infra/ci/build.sh` CI OK, `infra/ci/fts.sh` 7/7, `infra/ci/nettest.sh --scripts FireRun,FireRunControl --repeat 2` PASS with `switch_corrections=0`, `--scripts BotDuel --repeat 1` PASS.

## Verification steps
1. `infra/ci/build.sh` (new tests listed in the output)
2. `infra/ci/fts.sh`
3. `infra/ci/nettest.sh --scripts FireRun,FireRunControl --repeat 2`; `infra/ci/nettest.sh --scripts BotDuel --repeat 1`
4. Report-only: `infra/ci/nettest.sh --scripts FireRun --lag 125 --repeat 1` (attach the retarget/correction detail line)

## Report back
Standard format. State for item 1 whether the `--lag 125` run actually reached the echo path (`VeloxWeaponSwitch rep … is an older step` lines on the client).

## Notes / outcome
Done 2026-10-31 by dev-hard, all four items (amendment numbered **B44**: B43 was already taken by the death-camera note).
- 1: `Weapons/VeloxSwitchPrediction.h` (`ClassifyReplicatedIndex`, `AdvanceChain`, `ClassifyLocalRejection`); `OnRep_CurrentIndex` / `PredictCurrentIndex` apply them; `OldEffective` no longer uses an expired prediction (review nit).
- 2: the inventory records `{key, target, replaced target}` (RetargetPendingIndex -> NoteLocalSwitch; no ability change needed); a refused retarget restores the replaced target (pending: `RetargetPendingIndex`, landed: predicted); rejection lines carry `applied=0/1 action=...` (review nit).
- 3: `ResetLocalSwitchState()` from `AVeloxCharacter::InitAbilityActorInfo` on every client at a new pawn's first init; corrections while `State_Dead` are Verbose `reason=dead` and not counted by nettest.
- 4: nettest server retargets filtered `mode != local`.
- Verification: build.sh CI OK 101/101 (new `Velox.Combat.SwitchPrediction`), fts.sh 7/7, nettest FireRun,FireRunControl x2 PASS (`retarget client=4 server=4 modes=pending:4 switch_corrections=0` in all four), BotDuel x1 PASS. Report-only `FireRun --lag 125` (LogVeloxCombat Verbose): `retarget client=4 server=4 modes=pending:4 switch_corrections=0`, `raise_switches=2`, **0 "is an older step" lines**: FireRun.json does not reach the echo path at 250 ms RTT. The round FAILs only on the rail `rewind_ms` band, which is written for 100 ms one way.
