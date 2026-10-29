# 08h — Remote-shooter switch timeline: no receive-time carry, one resend tolerance, inventory-owned drop end (B37; absorbs 08e)

**Milestone:** M2 Combat (follow-up)
**Agent:** dev-hard
**Status:** done (merged 2cf7270; 75 tests; FireRun+HasteRun 2/2) (ruling in ADR 0003 B37; supersedes `08e-switch-timing-remote-shooter.md` on `m2-combat`, which is to be marked "folded into 08h")
**Depends on:** ADR 0003 B37, review `docs/reviews/m2-late-rpc-review.md` (rulings 6–9, defects 3–6); 08c merged. Independent of 08f/08g.

## Goal
An honest player at the 250 ms RTT limit never has a shot rejected or a switch orphaned because reliable RPCs arrived bunched. Three changes from 08c's `--lag 125` findings: the server stops carrying `NextFireTime` on receive time for remote shooters (refire wait from the last accepted receive time), the switch gate uses the one resend tolerance (`FireRateJitterToleranceSeconds`, 0.5 s; `SwitchTimingToleranceSeconds` deleted), and the inventory owns the server-side drop completion so a rejected supersede cannot orphan the pending switch. Plus the stale-comment sweep. `MaxRewindSeconds` is **not** touched (human question 19).

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxGameplayAbility_Fire.h/.cpp` — `LastAcceptedReceiveTime` (server, per instance; set in both abilities' accepted remote path where `AdvanceNextFireTime` is called today: `VeloxGameplayAbility_FireProjectile.cpp:262`, hitscan equivalent), `AdvanceNextFireTime` no longer called on the remote path; `CanActivateAbility` l.129–134 uses `GetFireRateJitterToleranceSeconds()`; header comments l.5–11, 65, 82.
- `Source/Velox/Weapons/VeloxGameplayAbility_WeaponSwitch.h/.cpp` — `RefireWait` (l.60–71): on authority for a remote shooter `max(0, LastAcceptedReceiveTime + Interval − Now)` of the current weapon's fire instance, local branch unchanged; supersede limit (l.89–91) uses the jitter tolerance; `SetPendingIndex(Target, DropEnd)` (l.115–118); `OnDropFinished` (l.146–163): authority branch no longer sets the index (the timer does), still ends the ability.
- `Source/Velox/Weapons/VeloxWeaponInventoryComponent.h/.cpp` — `SetPendingIndex(int32 Index, float DropEndSeconds)` arms `PendingDropTimer` → `CompletePendingDrop()`; cleared in `SetCurrentIndex` (l.357), `CompletePendingSwitch` (l.377), `CompletePendingSwitchIndexOnly` (l.399), `ResetToLoadout` (l.257), `EndPlay`.
- `Source/Velox/Weapons/VeloxFireTiming.h` — `IsSwitchFireAllowed` comment (the tolerance is the resend tolerance); optional world-free `RemoteRefireWait(LastAccepted, Interval, Now)`.
- `Source/Velox/Combat/VeloxCombatSettings.h/.cpp` — delete `SwitchTimingToleranceSeconds`, its getter and cvar (`.cpp:19, 85–90`); comment on `FireRateJitterToleranceSeconds` names both uses.
- `Tools/editor/create_bootstrap_content.py:384`, `docs/content-bootstrap.md:26` — field removed (asset regenerated, byte-identical otherwise; note the asset in the report).
- `Source/Velox/Weapons/VeloxGameplayAbility_FireHitscan.h:9`, `.cpp:247`, `VeloxGameplayAbility_FireProjectile.cpp:244`, `Net/VeloxLagCompensationSubsystem.h` (member default 0.30) — stale comments (review defect 6; the lag-comp `.cpp:650–653` comment belongs to 08g).
- `Source/Velox/Tests/VeloxWeaponTests.cpp` — rows below.
- `infra/ci/README.md` — the `--lag 125` row is now asserted for FireRun rejections (document).
- `docs/tasks/m2-combat/notes/08h-report.md`.
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` B37 (the spec), B18, B19, B32, B33, B34.
- `docs/reviews/m2-late-rpc-review.md` §2 rulings 6–9, §3 defects 3–6; `docs/tasks/m2-combat/notes/08c-report.md` (the `wait=0.090` vs `wait=0.373` evidence).

## Acceptance criteria
- [ ] `FireTiming`/`Inventory` rows: six MG shots accepted in one server frame (bunched resend) → the server's refire wait for a switch in the next frame ≤ `Interval` (0.1 s), not 0.6 s; a rail accepted at `T` → wait at `T + 0.4` = 1.1 s; no shot → 0.
- [ ] Switch gate row: server switch (MG→RG) activated 0.33 s "late" relative to the fire (remaining 0.33 + 0.02 jitter) → the rail is accepted; remaining 0.55 → rejected (B18 with the 0.5 tolerance).
- [ ] B33 rows (08c (a)/(b)) still pass; new row (c): supersede rejected (remaining > Raise + 0.5) → after `DropEnd` elapses with no further RPC, `CurrentIndex == Pending`, `PendingIndex == NONE`, GE still active through the raise; (d) an accepted shot of the pending weapon before the drop end completes it and clears the timer (no double `SetCurrentIndex`).
- [ ] `grep -rn SwitchTimingTolerance Source Tools docs/content-bootstrap.md` → nothing but ADR history; `grep -rn "minimum spacing\|bucket + spacing" Source/Velox` → nothing.
- [ ] FireRun `--lag 125` ×5: 0 `VeloxShotRejected`, 0 `ClientActivateAbilityFailed`, hits ≥ 9/10 per run; `hint=stamp` count reported (asserted only if the human chooses 0.35 for question 19).
- [ ] Default soak list `--repeat 3` 3/3 at the default lags; `infra/ci/build.sh` green; no commit.

## Verification steps
1. `infra/ci/build.sh` → tests green (53 + new rows − none deleted).
2. `infra/ci/nettest.sh --scripts FireRun --lag 125 --repeat 5` → 0 rejections / 0 act_failed, quote the per-run hits/stamp line.
3. `infra/ci/nettest.sh --repeat 3` → 3/3.

## Report back
Standard format; per item cause/fix/evidence; the regenerated `DA_CombatSettings_Default` diff (expect only the removed field); the `--lag 125` rows. Note for the lead: mark `08e` on `m2-combat` as folded into this task.

## Notes / outcome
(Filled by the agent or the lead after completion.)

## Follow-ups (lead, 2026-10-29)
- Add a test row: remote shooter with Haste → server refire wait ≤ the Haste interval (B37 under Haste is not covered).
- Architect: annotate ADR 0003 B18/B33 text that still names `SwitchTimingToleranceSeconds` (superseded by B37).
