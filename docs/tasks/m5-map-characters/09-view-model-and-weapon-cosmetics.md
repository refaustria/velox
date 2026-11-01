# 09 — Weapon cosmetics, first-person view model, third-person weapon attach

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** done
**Depends on:** 08 (cosmetics loading pattern, `UVeloxCharacterCosmetics::ViewModelSettings`), ADR 0006 §4.6

## Goal
Each weapon has a `UVeloxWeaponCosmetics` data asset (soft refs; meshes may still be empty). The local player sees the current weapon as a first-person view model with procedural bob/sway/recoil/raise-lower driven by movement and inventory state; other players see the world mesh in the pawn's right hand. No arms, no animation assets.

## Scope — files/modules the agent may touch
- `Source/Velox/Core/VeloxWeaponCosmetics.h/.cpp`, `Core/VeloxViewModelSettings.h` (new), `Weapons/VeloxWeaponDefinition.h` (`TSoftObjectPtr<UVeloxWeaponCosmetics> Cosmetics`), `Character/VeloxViewModelComponent.h/.cpp` (new), `Character/VeloxCharacter.*` (create the component for the local pawn; world mesh attach on `weapon_r`; `SetOwnerNoSee`), `Character/VeloxViewModelMath.h` (world-free bob/sway)
- `Tools/editor/create_bootstrap_content.py` (`build_weapons()` sets `cosmetics`), `Tools/editor/create_cosmetics_content.py` (`DA_WeaponCosmetics_<Name>` × 6, `DA_ViewModelSettings_Default`)
- `Source/Velox/Tests/VeloxCharacterTests.cpp` (`Velox.Character.ViewModelBob`)
- Not: `Movement/`, fire abilities, inventory replication.

## Relevant ADRs / references
- ADR 0006 §4.6 (first-person primitive path names to verify in 5.8: `Components/PrimitiveComponent.h` `FirstPersonPrimitiveType`, `Camera/CameraTypes.h` `FirstPersonFieldOfView`/`FirstPersonScale`; fallback plain owner-only mesh), Implementation notes (sketch)
- `Source/Velox/Weapons/VeloxWeaponInventoryComponent.h` (predicted switch state, `SwitchEndTime`), `Weapons/VeloxCombatFeedbackComponent.h` (`HandleCue` for the recoil kick)

## Acceptance criteria
- [ ] `UVeloxWeaponCosmetics` as sketched; six assets created by script with empty meshes/sounds; `UVeloxWeaponDefinition::Cosmetics` soft and set by the bootstrap script; `Velox.Core.CosmeticsSoftRefs` (task 08) covers the class.
- [ ] `UVeloxViewModelComponent`: created in `AVeloxCharacter` only when `IsLocallyControlled()` on a non-server world; attached to the camera component; one `UStaticMeshComponent` child, `bOnlyOwnerSee`, no collision, no shadow; swaps `ViewMesh` on the inventory's current-weapon change (predicted index, so the switch is immediate locally); applies `ViewOffset`/`ViewScale`; uses the 5.8 first-person primitive path when the names exist (document what you found) with `FirstPersonFieldOfView` 70 and `FirstPersonScale` 0.6 from `UVeloxViewModelSettings`.
- [ ] Procedural motion from `VeloxViewModelMath` (world-free): bob = distance-driven sine on the ground (`BobCycleCm`, `BobAmplitudeCm`, zero when airborne or speed < 50), sway = clamped first-order lag of the view-rotation delta (`SwayDegPerDeg`, `SwayMaxCm`), recoil = impulse on `Fire` cue decaying with `RecoilReturnSeconds`, raise/lower = vertical offset following the inventory switch (`LowerCm` during drop, back during raise, timed by `SwitchEndTime`). Tunables in `DA_ViewModelSettings_Default`. Unit test `Velox.Character.ViewModelBob`: bob is zero airborne, periodic in distance (not time), recoil decays to < 1 % within `RecoilReturnSeconds`.
- [ ] Third person: a `UStaticMeshComponent WeaponWorldMesh` on `Mesh` socket `weapon_r` (falls back to the capsule if no mesh/socket), `SetOwnerNoSee(true)`, mesh from the current weapon's `WorldMesh` on every weapon change for all clients (replicated inventory index), hidden on corpses.
- [ ] Dedicated server creates neither component content (asserted by a Verbose log line and no asset load).
- [ ] Build green; nettest `FireRun` PASS (switch timing unchanged).

## Verification steps
1. `infra/ci/build.sh`.
2. Headless `-game -nullrhi` on `L_CombatTest` with the `FireRun.json` scripted input (see `docs/movement-tooling.md`) → no warnings from the view model; log one line per weapon swap at Verbose.
3. `nettest.sh --scripts "FireRun"` under the soak lock → PASS.

## Report back
Standard format; include the exact 5.8 first-person API names you found (file + line) or the fallback you used.

## Notes / outcome

Done (dev). 5.8 first-person API found: `EFirstPersonPrimitiveType` (None/FirstPerson/WorldSpaceRepresentation) at `Engine/Classes/Components/PrimitiveComponent.h:153`, `UPrimitiveComponent::FirstPersonPrimitiveType` (:730) and `SetFirstPersonPrimitiveType` (:2120); camera side is `FMinimalViewInfo::bUseFirstPersonParameters` (`Camera/CameraTypes.h:109`), `FirstPersonFOV` (:58, not "FirstPersonFieldOfView") and `FirstPersonScale` (:62). No fallback needed. The pawn has no camera component (the engine eye point is the camera), so the view model is a world-space-tracking scene component ticked in `TG_PostUpdateWork` from `GetActorEyesViewPoint`, and `AVeloxCharacter::ApplyViewSettings` writes the three camera fields.
Deviations: tests are in `Tests/VeloxViewModelTests.cpp` (not `VeloxCharacterTests.cpp`, owned by task 08 in parallel) and also hold `Velox.Core.WeaponCosmeticsSoftRefs`; the weapon cosmetics assets and `DA_ViewModelSettings_Default` are created in `create_bootstrap_content.py` (the weapon definitions need them there) instead of `create_cosmetics_content.py`; `UVeloxCombatFeedbackComponent` gained an `OnFireCue` delegate for the recoil kick. Task 08 should extend the soft-ref test to `UVeloxCharacterCosmetics`.
Verification: build.sh (Editor + Game) OK, 109 automation tests pass incl. `Velox.Character.ViewModelBob`, nettest `FireRun` PASS twice (switch timing unchanged).
