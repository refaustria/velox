# Task 04b agent report (2026-10-25, merged as 85e15b7)

build.sh: 41 tests (39 + FireTiming + ShotValidation), 0 failures (`Saved/CI/20261025-032724-2731698` in the worktree). nettest full list 1/1 on port 7807. Headless FireTest: RG 100, MG 7, SG 18 (kill), LG 8 at 500 cm, no rejections; second RG in a burst logs "did not activate (fire rate)".

- B19 carry: `Weapons/VeloxFireTiming.h` `AdvanceNextFireTime(Next, Now, Interval, FrameDt)` keeps the schedule only when `Now − Next ≤ FrameDt + 1 µs`.
- B19 bucket: `FVeloxFireRateBucket::Consume(Now, Interval, Cap, MinSpacing)` → Accepted/Rate/Spacing; `MinSpacingFor(Interval, Tol) = max(0, Interval − Tol)`; spacing rejects cost no token. `ConsumeServerFireToken` keeps its signature and applies spacing.
- B18 switch: `State_WeaponSwitching` removed from fire `ActivationBlockedTags`; switch ability sets `Inventory->SwitchEndTime` on every machine; `ResetToLoadout` clears it; `VeloxFireTiming::IsSwitchFireAllowed` (shooter: Now ≥ SwitchEndTime; server for remote: longest GE_WeaponSwitching remaining ≤ tolerance); an accepted pending-weapon shot calls `FinalisePendingSwitch`. Switch ability overrides `EndAbility` to reset `SwitchEndTime` to 0 on early end (rejection/cancel).
- B23: `BeamKeepAliveSeconds`, never less than one interval.
- `Weapons/VeloxShotValidation.{h,cpp}` world-free `Validate(...)` → `EVeloxShotReject` {None, Type, Rate, Spacing, Origin, Dir, Time}; base helper `ValidateServerShot` for all fire abilities (projectile ability should adopt it; today it calls `ConsumeServerFireToken`).
- Settings: `SwitchTimingToleranceSeconds` 0.25, `FireRateSpacingTolerance` 0.25, `BeamKeepAliveSeconds` 0.25 (cvars `velox.combat.<Field>`; DA byte-identical since equal to class defaults).
- Switch `EventMagnitude` range-checked; `VeloxShotRejected` rate-limited (`RejectLogLimitPerSecond` 5); `EVeloxHitConfirmFlags` → namespace; `docs/content-bootstrap.md` weapons section.
- Not run end to end: switch-then-fire via `IA_Weapon_Slot2` on a listen server (no console command for the switch input); task 08's switch sequence covers it.

## For the architect / lead
- Unity-build trap: `Tests/VeloxMoveSimTest.cpp` has a file-scope `using namespace VeloxMoveSimTest;` that leaks into later files of the same unity blob; adding a .cpp shifts the blob and `-Wshadow` broke a `Flags` parameter (04b renamed the parameter, 06 renamed the constant to `MoveSimFlags`). Proper fix: scope the using-directive (small dev task).
- For a remote shooter the switch gate failure adds `ActivateFailTagsBlockedTag` and logs nothing; task 08 counts them as `ClientActivateAbilityFailed`.
