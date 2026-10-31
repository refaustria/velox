# 18c — Weapon cosmetics are the single source of look and sound; async-loaded on clients

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** done
**Depends on:** 18b merged (both touch `VeloxCombatFeedbackComponent.cpp`), ADR 0006 E1 (ruling on task 10 deviation 1), review D4; must land before task 13 fills the weapon meshes

## Goal
`UVeloxWeaponCosmetics` is the one place that says how a weapon looks and sounds (ADR 0006 §9 as written): fire, impact and beam sounds, muzzle light colour/intensity, world and view mesh. `UVeloxAudioSettings::CueSounds` and the beam one-shots are removed; `DryFireSound` and `SwitchSound` are removed from the cosmetics (one global click and one global dry fire, Q3 parity). All six cosmetics assets and their soft references are loaded asynchronously once per client pawn through `UVeloxAssetManager` and stay resident, so no `LoadSynchronous` runs at a weapon swap or a cue once task 13's meshes exist.

## Scope — files/modules the agent may touch
- `Source/Velox/Core/VeloxWeaponCosmetics.h/.cpp` — remove `DryFireSound`, `SwitchSound`; add `void GetSoftReferences(TArray<FSoftObjectPath>&) const`
- `Source/Velox/Core/VeloxAudioSettings.h/.cpp` — remove `CueSounds`, `FVeloxCueSound`, `BeamStartSound`, `BeamStopSound`, `FindCueSound`; keep `LoadAttenuation/LoadConcurrency`, the generic sounds, `DryFireSound`, `WeaponSwitchSound`; `LoadAll` no longer preloads weapon sounds (the asset manager does)
- `Source/Velox/Core/VeloxAssetManager.h/.cpp` — `static bool LoadWeaponCosmetics(const UWorld*, TConstArrayView<const UVeloxWeaponDefinition*>, TFunction<void()> OnLoaded)`: stage 1 the definitions' `Cosmetics` paths, stage 2 every `GetSoftReferences` of the loaded assets; one resident handle keyed `"WeaponCosmetics"`; no-op (returns false) on `IsCosmeticsFreeWorld`
- `Source/Velox/Weapons/VeloxCombatFeedbackComponent.h/.cpp` — resolve the cue's `UVeloxWeaponCosmetics` from the owner's inventory table (match `FireCueTag`/`ImpactCueTag`/`BeamCueTag`; build the `TMap<FGameplayTag, const UVeloxWeaponDefinition*>` once per inventory bind), play `FireSound`/`ImpactSound`/`BeamStart`/`BeamLoop`/`BeamStop` with `SA_WeaponFire`/`SA_Impact` (+ `SA_Explosion` for RL/PG impacts: a new `EVeloxAudioAttenuation ImpactAttenuation` field on the cosmetics, default `Impact`), muzzle light from the cosmetics (`MuzzleLightIntensity > 0`; 0 means no light, as the header says — no fallback to a global light)
- `Source/Velox/Weapons/VeloxMovableLights.h/.cpp` (new) — the ≤ `MaxMovableLights` list (`Acquire(World, Location, Color, Intensity, Radius, Seconds)`), shared later by the rocket light (task 13)
- `Source/Velox/Character/VeloxCharacter.cpp` — `InitAbilityActorInfo` requests `LoadWeaponCosmetics` for the inventory's table on non-dedicated worlds (once per pawn, `TWeakObjectPtr` callback → `RefreshWeaponWorldMesh` and `ViewModel->ShowWeapon(current)`); `RefreshWeaponWorldMesh` uses `.Get()` only
- `Source/Velox/Character/VeloxViewModelComponent.cpp` — `ShowWeapon` uses `.Get()` only; `SettingsAsset` may keep its one `LoadSynchronous` at `Initialize` (one small asset, once per life) or join the batch
- `Tools/editor/create_audio_content.py` — writes the weapon sounds into `DA_WeaponCosmetics_*` instead of the ini `CueSounds` block; `--print-ini` drops the weapon rows; `Config/DefaultGame.ini` regenerated accordingly
- `Source/Velox/Tests/VeloxAudioTests.cpp`, `Tests/VeloxViewModelTests.cpp` — `Velox.Core.WeaponCosmeticsSoftRefs` extends to the new field; `Velox.Audio.WeaponCosmeticsContent`: every `DA_WeaponCosmetics_*` names a `FireSound` and an `ImpactSound` (LG: beam start/loop/stop and impact), and `UVeloxAudioSettings::CueSounds` no longer exists (compile-time)
- `docs/art/audio-list.md` — hook column updated
- Not: `Movement/`, `Combat/`, inventory replication, the fire abilities

## Relevant ADRs / references
- ADR 0006 §9 (hooks: "resolves `UVeloxWeaponCosmetics` from the weapon table by cue tag"), §1 (soft refs, server never loads), E1
- `docs/adr/reviews/0006-cosmetics-review.md` D4 and "Deferred"
- Pattern to copy: `UVeloxAssetManager::LoadCharacterCosmetics` (two stages, resident handle, next-tick delegates, `TWeakObjectPtr` capture at the call site)

## Acceptance criteria
- [x] `grep -rn 'CueSounds\|FindCueSound\|BeamStartSound\|BeamStopSound' Source/ Config/ Tools/` returns nothing; `grep -rn 'LoadSynchronous' Source/Velox/Character/VeloxCharacter.cpp Source/Velox/Character/VeloxViewModelComponent.cpp Source/Velox/Weapons/VeloxCombatFeedbackComponent.cpp` returns at most the `SettingsAsset` line.
- [x] Every fire/impact/beam cue plays the sound named by that weapon's `DA_WeaponCosmetics_*`; the LG beam loop is one `UAudioComponent` per beam start/stop as before; own fire still 2D (18b's `IsLocalPlayerPawn`), others 3D at the muzzle/hit point with the cosmetics' attenuation.
- [x] Muzzle light colour/intensity come from the weapon's cosmetics (`MuzzleLightIntensity 0` = none); the global list in `VeloxMovableLights` enforces `MaxMovableLights`; the settings keep only `MaxMovableLights`, `MuzzleLightSeconds`, `MuzzleLightRadius`.
- [x] Dedicated server: `LoadWeaponCosmetics` returns false and logs one Verbose line; the 18b server-log gate stays green.
- [x] Client: one `VeloxCosmetics: loading N asset(s) of WeaponCosmetics` line per process (resident), none per weapon swap; `RefreshWeaponWorldMesh` and `ShowWeapon` log `mesh=None` until the batch completes, then the mesh (verify with a temporary test mesh assigned to one `DA_WeaponCosmetics_*` in a local, uncommitted edit, or with the engine's `/Engine/BasicShapes/Cube`).
- [x] `create_audio_content.py` is idempotent and `--print-ini` matches the committed ini; `Velox.Audio.WeaponCosmeticsContent` passes on the committed content.
- [x] Build green; all tests green; nettest `FireRun BotRun` PASS (the FireRun Verbose audio lines show the same fire/impact sounds as before per weapon).

## Verification steps
1. `python3 -I Tools/editor/create_audio_content.py --print-ini` → no weapon rows; run the import script headless; `git status` shows the six `DA_WeaponCosmetics_*` changed and `DefaultGame.ini` shrunk.
2. `infra/ci/build.sh` → tests listed and passing, incl. the two cosmetics content tests.
3. Headless `-game -nullrhi L_CombatTest` with `FireRun.json` and `log LogVeloxAudio Verbose` → one load line, one `play`/`play2d` per shot with the per-weapon sound names, `VeloxViewModel weapon=… mesh=…` only on swaps.
4. `nettest.sh --scripts "FireRun BotRun"` under the soak lock → PASS; server log passes the 18b gate.

## Report back
Standard format; the reverse map build site; the size of the resident weapon-cosmetics set (`LoadWeaponCosmetics` log line); any field you had to add to the cosmetics beyond `ImpactAttenuation`.

## Notes / outcome

Done (dev, 2026-10-31). `UVeloxWeaponCosmetics` is the single source of weapon sound and muzzle light: `DryFireSound`/`SwitchSound` removed (global ones stay in the settings), `ImpactAttenuation` added (the only new field), `GetSoftReferences` added. `CueSounds`, `FVeloxCueSound`, `FindCueSound`, `BeamStartSound/StopSound` and the settings' `MuzzleLightIntensity/Color` are gone; `create_audio_content.py` writes sounds and the muzzle light (5000 cd, 1/0.7/0.3; LG 0 as before) into the six `DA_WeaponCosmetics_*` (idempotent, byte-identical on a second run), `--print-ini` matches the shrunk ini. `UVeloxAssetManager::LoadWeaponCosmetics` (two stages, one resident handle, waiters while loading, no-op on dedicated). `UVeloxCombatFeedbackComponent` builds the cue-tag -> definition map once per table (`FindCueDefinition`) and plays only sounds that are resident (`Get()`); `VeloxMovableLights::Acquire` enforces `MaxMovableLights`. `AVeloxCharacter::RequestWeaponCosmetics` (once per pawn, retried until the combat settings arrived) -> `OnWeaponCosmeticsLoaded` refreshes the world mesh and `ViewModel->ShowWeapon` (now public). Resident set: 13 assets (5 fire + 5 distinct impact + 3 beam sounds; meshes still empty) + 6 cosmetics assets, one `loading 13 asset(s) of WeaponCosmetics` line per client process. Deviation: `LoadSynchronous` remains in `VeloxCharacter.cpp` for `DefaultPawnData` and `PlayerBodyMaterial` (PostInitializeComponents, not weapon related, needed before the first tick) and in the `SettingsAsset` line; none in the weapon / cue paths.
