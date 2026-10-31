# 08 — Character cosmetics data, skins, `UVeloxAnimInstance`

**Milestone:** M5 Map + characters
**Agent:** dev-hard
**Status:** done
**Depends on:** ADR 0006 §1, §4.1–4.5 (no art needed: works with a placeholder skeletal mesh or none)

## Goal
The pawn can wear a skeletal mesh with a skin chosen by `SkinIndex`, animated by a C++ anim instance whose state logic is unit-tested and which works without an AnimBlueprint (`bUseSequencePlayback`). Cosmetics are soft-referenced data assets the dedicated server never loads. Enemy skin forcing is a user setting. The placeholder capsule body remains as a cvar fallback.

## Scope — files/modules the agent may touch
- `Source/Velox/Core/VeloxCharacterCosmetics.h/.cpp` (new), `Core/VeloxPawnData.h` (`Cosmetics` soft field), `Core/VeloxAssetManager.*` (async load helper for cosmetics, non-server only)
- `Source/Velox/Character/VeloxAnimInstance.h/.cpp`, `Character/VeloxAnimState.h` (world-free rules) (new), `Character/VeloxCharacter.h/.cpp` (mesh set-up, skin application, placeholder fallback, `OnRep` hook for the skin), `GameModes/VeloxPlayerState.h/.cpp` (`PlayerColorIndex` → `SkinIndex`, `GetPlayerColor()` from the skin), `UI/VeloxGameUserSettings.*` (`bForceEnemySkin`, `ForcedEnemySkin`), `Weapons/VeloxCombatFeedbackComponent.cpp` (set `bFiring` on `Fire` cues)
- `Tools/editor/create_cosmetics_content.py` (new: `DA_Cosmetics_Velox` with empty soft refs and the six skin names/colours; wires `DA_PawnData_Default`), `Config/DefaultGame.ini`
- `Source/Velox/Tests/VeloxCharacterTests.cpp` (new: `Velox.Character.AnimState`, `Velox.Core.CosmeticsSoftRefs`)
- Not: `Movement/`, `Combat/VeloxHitboxComponent.*`, `Combat/VeloxLagCompensationSubsystem.*`.

## Relevant ADRs / references
- ADR 0006 §4.1 (capsule stays the hitbox; mesh offset −70, yaw −90), §4.3 (skins as MIs, `SkinIndex`, forcing default on/Green), §4.4 (sequence names, cadence, play-rate clamp), §4.5 (variables, `LegState`, sequence playback fallback), Implementation notes (interface sketch)
- ADR 0003 §10 (`PlaceholderBody`, `PlayerColor` palette; keep the HUD colours stable), ADR 0001 §4 (asset manager)

## Acceptance criteria
- [x] `UVeloxCharacterCosmetics` as sketched in ADR 0006 (all references soft; `Velox.Core.CosmeticsSoftRefs` walks the class properties by reflection and fails on any hard `UObject` pointer in `UVeloxCharacterCosmetics`/`UVeloxWeaponCosmetics`).
- [x] `AVeloxCharacter`: on `BeginPlay`/`OnRep_PlayerState` for non-dedicated-server worlds, async-loads the pawn data's cosmetics, sets `Mesh` (offset Z −70, yaw −90, `AnimClass` if set, else `bUseSequencePlayback`), applies the skin material for `SkinIndex` (or the forced enemy skin when the local player does not own the pawn and `bForceEnemySkin`), and hides/destroys `PlaceholderBody` unless `velox.vis.PlaceholderBody 1`. Corpses keep their skin, darkened as today. Dedicated server: no load, no mesh set (log once at Verbose).
- [x] `AVeloxPlayerState::SkinIndex` replaces `PlayerColorIndex` (rename with the same replication; assignment order unchanged; `GetPlayerColor()` returns the skin's `BodyColor`; HUD/scoreboard call sites compile unchanged or are updated). Skin list order = old palette order for the first six colours.
- [x] `UVeloxAnimInstance` exposes `Speed2D`, `DirectionDeg`, `bAirborne`, `bCrouching`, `AimPitch`, `bFiring`, `bDead`, `LegState`, `PlayRate`; the transitions and durations live in `VeloxAnimState.h` (`VeloxAnimState::Step(const FVeloxAnimInput&, FVeloxAnimStateData&, float Dt)`), covered by `Velox.Character.AnimState` (idle→run at speed ≥ 100, run→air on airborne, jump start 0.15 s then air, land 0.2 s then idle/run, crouch variants, dead sticky, play rate = clamp(Speed/800, 0.5, 1.6), direction quadrant pick).
- [x] Sequence playback fallback: on `LegState` change with no AnimClass, `PlayAnimation(Sequences[State], bLoop)`; `Fire_Upper` is skipped in this mode (no slot). Proxies use the smoothed replicated velocity.
- [x] `UVeloxGameUserSettings::bForceEnemySkin` (default true) and `ForcedEnemySkin` (default `Green`), persisted, exposed to the settings menu data (UI widget wiring may be a one-line addition if the settings screen is data-driven; otherwise note it for the UI task).
- [x] Build green; existing tests green; nettest `FireRun` + `BotRun` PASS (pawn changes touch the replicated pawn: this is the regression gate).

## Verification steps
1. `infra/ci/build.sh` → tests listed and passing.
2. Headless: `UnrealEditor-Cmd "$VELOX_PROJECT" L_ItemTest -game -nullrhi -ExecCmds="velox.vis.PlaceholderBody 0" …` with `?Bots=2` → log shows the cosmetics load attempt and "no SkeletalMesh set" (empty soft ref) without errors; with `-server` the log shows the Verbose "cosmetics skipped on dedicated server" line.
3. Under the soak lock, idle host: `nettest.sh --scripts "FireRun BotRun"` → PASS.

## Report back
Standard format; list every call site renamed from `PlayerColorIndex`; state whether the settings menu got the new rows.

## Notes / outcome

Done (dev-hard). `UVeloxCharacterCosmetics` (all soft; `ViewModelSettings` typed `UVeloxViewModelSettings`, pointing at task 09's `DA_ViewModelSettings_Default`), `UVeloxPawnData::Cosmetics`, `UVeloxAssetManager::LoadCharacterCosmetics` (async, two stages, one resident handle per data asset, never on dedicated-server worlds), `UVeloxAnimInstance` + `FVeloxAnimSampler`, world-free `VeloxAnimState::Step`, sequence-playback driver in `AVeloxCharacter::Tick`, `SkinIndex` (old names kept as inline aliases, no call site touched), `bForceEnemySkin`/`ForcedEnemySkin`, `DA_Cosmetics_Velox` by `Tools/editor/create_cosmetics_content.py`.
Deviations: the leg-state enums and `FVeloxAnimTuning` live in `Core/VeloxCharacterCosmetics.h` (Core may not include Character); the fifth skin is `Magenta` (palette index 4), not the ADR's `Orange`, to keep HUD colours; `PlaceholderBody` is hidden (not destroyed) and only when a skeletal mesh was set; added `RunSequences`, `BodyColorParameter`, `MeshYawDeg`, `AnimTuning`; class-default mesh placement (0,0,-70)/yaw -90 in the constructor (the engine crouch code reads the CDO). Settings screen rows not added (`VeloxSettingsWidget` is hand-built, out of scope) -> UI task.
Verification: build.sh Editor + Game OK (Server/Client SKIPPED, installed engine), 115 tests, 0 failures on the merged 08+09+10 head (new: `Velox.Character.AnimState`, `Velox.Character.SkinChoice`, `Velox.Character.CosmeticsContent`, `Velox.Core.CosmeticsSoftRefs`); headless `-game` L_ItemTest?Bots=2 logs the load and "no SkeletalMesh set" for every pawn incl. respawns, no errors; `-server` logs the Verbose skip line; nettest FireRun + BotRun PASS (alone, after the 09 merge and on the 08+09+10 head).
