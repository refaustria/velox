# 04b — Weapon timing fixes from the architect review

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** done (merged 85e15b7)
**Depends on:** 04 (review `docs/reviews/m2-04-weapons-hitscan-review.md`, ADR 0003 Amendments B18–B20, B22–B23). Independent of 05/06/07; must be merged before 08. 04c (server pattern seed) is a separate task.

## Goal
Close review defects 1, 2, 3, 5, 6, 7 and nits 10–12 (§2 of the review is the authoritative list with file:line). After this task the fire rate is Q3-exact on the shooter's machine (one-frame carry, no bankable double tap), the server bounds bursts as well as the long-term rate (token bucket + minimum spacing), weapon-switch timing is local on the shooter's machine and validated on the server with a tolerance instead of being ignored for remote shooters or gated by the RTT-late replicated tag, the LG beam keep-alive is a settings field, and the server validation path is a world-free, unit-tested function.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/VeloxGameplayAbility_Fire.h/.cpp` — `CanActivateAbility` switch gate (local `SwitchEndTime` / server tolerance, pending weapon finalises the switch), delete the `DoesAbilitySatisfyTagRequirements` override, remove `State_WeaponSwitching` from `ActivationBlockedTags`, `AdvanceNextFireTime` via the new world-free helper, spacing check in `ConsumeServerFireToken`
- `Source/Velox/Weapons/VeloxFireTiming.h` (new, world-free): `AdvanceNextFireTime(Next, Now, Interval, FrameDt)`, `IsSwitchFireAllowed(bLocal, Now, SwitchEndTime, SwitchRemaining, Tolerance)`
- `Source/Velox/Weapons/VeloxFireRateBucket.h` — `LastAcceptedTime`, `MinSpacing` parameter, reason out-param or a second query
- `Source/Velox/Weapons/VeloxShotValidation.h/.cpp` (new, world-free): `Validate(const FGameplayAbilityTargetData* Raw, FVeloxFireRateBucket&, double Now, float Interval, float Capacity, float SpacingTolerance) → EVeloxShotReject {None, Type, Rate, Spacing, Origin, Dir, Time}`; `VeloxGameplayAbility_FireHitscan.cpp` `OnServerTargetData` calls it and logs the reason name
- `Source/Velox/Weapons/VeloxGameplayAbility_WeaponSwitch.cpp` — set `Inventory->SwitchEndTime` in `ActivateAbility` on every machine; finite guard on `EventMagnitude`
- `Source/Velox/Weapons/VeloxWeaponInventoryComponent.h/.cpp` — `SwitchEndTime` (local, not replicated; cleared in `ResetToLoadout`), `KeepBeamAlive` uses `BeamKeepAliveSeconds`
- `Source/Velox/Combat/VeloxCombatSettings.h/.cpp` — new fields `SwitchTimingToleranceSeconds` (0.25), `FireRateSpacingTolerance` (0.25), `BeamKeepAliveSeconds` (0.25) with cvar mirrors; `Tools/editor/create_bootstrap_content.py` — the three bootstrap fields
- `Source/Velox/GameModes/VeloxPlayerController.h` — `EVeloxHitConfirmFlags` → `namespace VeloxHitConfirmFlags` (or `enum class` + `ENUM_CLASS_FLAGS`); update the two call sites
- `Source/Velox/Weapons/VeloxGameplayAbility_FireHitscan.cpp` — `Reject` log throttling (first N per shooter per second at `Log`, then `Verbose`)
- `Source/Velox/Tests/VeloxWeaponTests.cpp` — new tests `Velox.Combat.FireTiming`, `Velox.Combat.ShotValidation`; extend `FireRateBucket` and `Inventory`
- `docs/content-bootstrap.md` — weapon section (`DA_Weapon_*`, `DA_WeaponTable`, `weapon_table`, `spawn_loadout`, Q3-unit derivation, the three new fields)
(Anything not listed is out of scope — in particular no change to the pattern seed or cue policy; that is 04c.)

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` — §4, §5, Amendments B18 (switch), B19 (fire timing + spacing), B22 (accepted details), B23 (beam keep-alive)
- `docs/reviews/m2-04-weapons-hitscan-review.md` — §2 defects 1, 2, 3, 5, 6, 7, 10–12
- Engine: `GameplayEffect.cpp` l.2952–2955 (`RecomputeStartWorldTime`), `AbilitySystemComponent.h` l.806 (`GetActiveEffectsTimeRemaining`), `GameplayEffect.h` l.1536 (`MakeQuery_MatchAnyOwningTags`)

## Acceptance criteria
- [ ] `Velox.Combat.FireTiming` (world-free): held fire at 60 Hz for 1200 shots keeps the exact rate for intervals 0.05/0.1/1.5 (sum of intervals, not of frames); a shot 1.49 s after the schedule of a 1.5 s weapon yields `Next = Now + 1.5` (no bank); a shot inside the crossing frame (`Now − Next = 0.004`, `FrameDt = 1/60`) carries (`Next + Interval`); `IsSwitchFireAllowed`: local blocked before `SwitchEndTime`, allowed at it; server allowed at remaining 0.25, blocked at 0.26 with tolerance 0.25; remaining 0 (no GE) allowed.
- [ ] `Velox.Combat.FireRateBucket` extended: three rails at `t = 10.0` with spacing tolerance 0.25 → 1 accepted (`Spacing`); the next at `t + 1.3` accepted, at `t + 1.2` rejected; MG/LG rows unchanged (spacing 0, the 40 ms jitter row still all accepted); the existing rows pass with the new signature.
- [ ] `Velox.Combat.ShotValidation`: every `EVeloxShotReject` reason is produced by its input (wrong struct type, NaN origin, zero and NaN dir, +inf/−inf/NaN view time, NaN stamp, over-rate, under-spacing) and a valid shot returns `None` with the dir normalised; the bucket is consumed before the payload checks (a wrong-type shot still costs a token, as today).
- [ ] `Velox.Combat.Inventory` extended: after the slot-7 switch, the fire press at `t = SwitchEndTime − 1/60` does not fire and the press at `t = SwitchEndTime` fires (local gate, independent of `State_WeaponSwitching`); the tag is still present during the switch (GE unchanged); `ResetToLoadout` clears `SwitchEndTime`.
- [ ] `Velox.Combat.HitscanDummy` unchanged and green; `Velox.Combat.WeaponTable` asserts the three new settings fields exist in `DA_CombatSettings_Default` with the defaults (0.25 each) — or extend `Velox.Combat.Ammo`'s asset block.
- [ ] `KeepBeamAlive` has no literal multiplier; `BeamKeepAliveSeconds` from the settings; `Velox.Combat.HitscanDummy`'s beam-stop assertion advances ≥ `BeamKeepAliveSeconds` + one frame.
- [ ] No `State_WeaponSwitching` in any fire ability's `ActivationBlockedTags`; no `DoesAbilitySatisfyTagRequirements` override in `VeloxGameplayAbility_Fire`.
- [ ] `docs/content-bootstrap.md` has the weapon section.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests). M1 soak unaffected (no movement code touched); use `--port 7804` if a soak is run while other agents soak on 7777.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`, test count 39 + 2 new.
2. Bootstrap script run (`UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=Tools/editor/create_bootstrap_content.py …` per `docs/content-bootstrap.md`) → `DA_CombatSettings_Default` carries the three new fields; commit the updated `.uasset`.
3. Headless `-game -nullrhi` listen run with `velox.combat.FireTest RG` twice within 1.3 s → second attempt logs "did not activate (fire rate …)"; `velox.combat.GiveAllWeapons`, switch 7 → 2 by `IA_Weapon_Slot2` and fire → the `VeloxWeaponSwitch` log line and the shot ≥ `wait + drop + raise` later, no `VeloxShotRejected`.

## Report back
Standard dev-hard format; list each defect with the change and the test that covers it. Report the final values of the three new settings fields and whether any existing test needed a number changed (and why).

## Notes / outcome
- 2026-10-25 (dev-hard): defects 1, 2, 3, 5, 6, 7, 10-12 closed. build.sh CI OK, 41 tests (39 + FireTiming, ShotValidation), 0 failures; nettest `--port 7807` round 1/1 PASS; headless L_CombatTest FireTest RG/MG/SG/LG (at 5 m) all hit, RG refire within 1.5 s refused.
- Settings: SwitchTimingToleranceSeconds 0.25, FireRateSpacingTolerance 0.25, BeamKeepAliveSeconds 0.25 (cvars `velox.combat.<Field>`). DA_CombatSettings_Default re-saved by the bootstrap script byte-identical (values equal the class defaults, delta serialisation) - no asset change to commit.
- Extras: switch ability clears SwitchEndTime when it ends before its drop end (server rejection / cancel); carry comparison has a 1 us epsilon (`VeloxFireTiming::CarryEpsilonSeconds`); `UVeloxCombatFeedbackComponent::OnHitConfirmed` parameter renamed (unity-build -Wshadow against `using namespace VeloxMoveSimTest` in VeloxMoveSimTest.cpp).
