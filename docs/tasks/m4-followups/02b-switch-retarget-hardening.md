# 02b — Switch retarget hardening (follow-ups from the B42 review)

**Milestone:** M4 follow-ups
**Agent:** dev-hard (items 1–2), dev (items 3–4)
**Status:** todo
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
- [ ] **1 (D4).** `OnRep_CurrentIndex`'s decision is a world-free function (e.g. `VeloxSwitchPrediction::ClassifyReplicatedIndex(Predicted, bLive, Auth, Stale) -> Confirm | EchoOfOlderStep | Correction(rep) | Correction(timeout)`), the OnRep only applies the result; `Velox.Combat.SwitchPrediction` tests: confirm; echo of an older step stands and trims the chain up to that step; a replicated value not in the chain while live → rep correction; expired → timeout; predicted == replicated while a chain is live → confirm, no log; chain bounded at 4 (oldest evicted). A report-only `nettest.sh --scripts FireRun --lag 125 --repeat 1` run is attached to the report (corrections reported, not asserted).
- [ ] **2 (D1).** A server-rejected retarget (`ClientRejectSwitch` or the rejected delegate for a retarget key) restores the previous pending target when the switch is still pending (drop timer and `SwitchEndTime` untouched); a rejected non-retarget switch still rolls back fully. Unit row: the decision is world-free or driven through a test seam; the `Inventory` D4 rows and `SwitchRetargetLocal/Remote` stay green.
- [ ] **3 (D2).** `UVeloxWeaponInventoryComponent::ResetLocalSwitchState()` clears pending index, drop timer, `SwitchEndTime`, predicted index + chain and `LocalSwitchKey`; called from `AVeloxCharacter::InitAbilityActorInfo` on the owning client's first init of a new pawn (next to `ResetShotSeq`). `VeloxWeaponSwitchCorrection` is logged at Verbose with `reason=dead` while the ASC has `State_Dead`. Unit row: a pending client switch + predicted index survive nothing across `ResetLocalSwitchState()`.
- [ ] **4 (D3).** `nettest.sh` counts server retargets with `mode != local` only; FireRun/FireRunControl detail line unchanged otherwise.
- [ ] `infra/ci/build.sh` CI OK, `infra/ci/fts.sh` 7/7, `infra/ci/nettest.sh --scripts FireRun,FireRunControl --repeat 2` PASS with `switch_corrections=0`, `--scripts BotDuel --repeat 1` PASS.

## Verification steps
1. `infra/ci/build.sh` (new tests listed in the output)
2. `infra/ci/fts.sh`
3. `infra/ci/nettest.sh --scripts FireRun,FireRunControl --repeat 2`; `infra/ci/nettest.sh --scripts BotDuel --repeat 1`
4. Report-only: `infra/ci/nettest.sh --scripts FireRun --lag 125 --repeat 1` (attach the retarget/correction detail line)

## Report back
Standard format. State for item 1 whether the `--lag 125` run actually reached the echo path (`VeloxWeaponSwitch rep … is an older step` lines on the client).

## Notes / outcome
