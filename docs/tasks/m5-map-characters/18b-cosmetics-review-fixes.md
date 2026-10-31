# 18b — Cosmetics review fixes: own-pawn audio rule, proxy landings, dry fire, skin UI rows

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** done
**Depends on:** 18 (review `docs/adr/reviews/0006-cosmetics-review.md`, defects D1–D3, D5–D7), ADR 0006 E1

## Goal
The audio of tasks 10 behaves the same on a dedicated server, a listen server and standalone: a bot is never "the local player" (D1), other players' landings are audible on clients (D2), an empty weapon clicks (D3), and the two skin settings and the UI click are reachable from the menus (D7). Must land before the task-17 checklist is run on the Mac: without D1 every bot's fire plays 2D in the host's ears and the "direction of enemy fire" line cannot pass.

## Scope — files/modules the agent may touch
- `Source/Velox/Core/VeloxAudioSettings.h/.cpp` — `static bool IsLocalPlayerPawn(const APawn*)`; new Config `float DryFireMinInterval = 0.25f`; `static void PlayUIClick(const UObject* WorldContext)`
- `Source/Velox/Weapons/VeloxCombatFeedbackComponent.h/.cpp` — D1 call sites (`PlayCueAudio`, `UpdateBeamAudio`, `HandlePowerupCue`); D3 `AbilityFailedCallbacks` binding + dry fire; nit: move the `OnFireCue` delegate declaration above the getters
- `Source/Velox/Character/VeloxFootstepComponent.cpp` — D1 (`OnHealthChanged`, `HandleCorpse`), D2 (proxy landing in `TickComponent`)
- `Source/Velox/Movement/VeloxJumpPad.cpp` — **only** `MulticastLaunchCue_Implementation` (D1) and the `const_cast` nit in `OnTriggerBeginOverlap`; nothing else in `Movement/`
- `Source/Velox/Character/VeloxCharacter.cpp` — D5 guard in `ApplyCharacterSkin` (one `if`); nothing else
- `Source/Velox/UI/VeloxAnnouncerComponent.cpp`, `GameModes/VeloxGameState.h/.cpp` (kill feed entry) — D6 `KillerPlayerId`
- `Source/Velox/UI/VeloxSettingsWidget.h/.cpp`, `UI/VeloxMainMenuWidget.cpp`, `UI/VeloxPauseMenuWidget.cpp` — D7 rows and click sound
- `infra/ci/nettest.sh` — server-log assertion (no cosmetic/audio play line in the `-server` log)
- `Source/Velox/Tests/VeloxAudioTests.cpp` — `Velox.Audio.OwnPawnRule` if a world-free formulation exists (see criteria), else none
- Not: `Combat/`, `Weapons/VeloxWeaponInventoryComponent.*`, the fire abilities, `Core/VeloxAssetManager.*`, `Core/VeloxWeaponCosmetics.h` (18c owns those)

## Relevant ADRs / references
- `docs/adr/reviews/0006-cosmetics-review.md` D1–D3, D5–D7, nits
- ADR 0006 §9 and E1 (proxy landing wording, own-pawn rule), §4.3 (settings)
- Engine facts the fixes rest on: `AController::IsLocalController` (`Engine/Private/Controller.cpp:90–112`) is true for AI controllers on authority; `UCharacterMovementComponent::SimulateMovement` lands via `SetPostLandedPhysics` (`CharacterMovementComponent.cpp:2354`), never `ACharacter::Landed`; `UVeloxGameplayAbility_Fire::CheckCost` adds `UAbilitySystemGlobals::ActivateFailCostTag`

## Acceptance criteria
- [ ] **D1.** `UVeloxAudioSettings::IsLocalPlayerPawn(const APawn* Pawn)` returns `Pawn && Cast<APlayerController>(Pawn->GetController()) && Pawn->IsLocallyControlled()`; it replaces `IsLocallyControlled()` in `PlayCueAudio`, `UpdateBeamAudio`, `HandlePowerupCue`, `OnHealthChanged`, `HandleCorpse` and `MulticastLaunchCue_Implementation`. `grep -n 'IsLocallyControlled' Source/Velox/Weapons/VeloxCombatFeedbackComponent.cpp Source/Velox/Character/VeloxFootstepComponent.cpp Source/Velox/Movement/VeloxJumpPad.cpp` returns nothing.
- [ ] **D2.** In `UVeloxFootstepComponent::TickComponent`, a simulated proxy that goes from not grounded to grounded plays the soft/hard landing from `LastFallingVz` through the same banding as `HandleLanded` (factor the body into a private `PlayLanding(float Vz)`; `HandleLanded` keeps its `bClientUpdating`/corpse checks and calls it). Owner and authority paths unchanged.
- [ ] **D3.** On the local player's pawn only, the feedback component binds `UAbilitySystemComponent::AbilityFailedCallbacks` of the ASC whose avatar is its owner (bind in the same place the pawn's ASC becomes known — `InitAbilityActorInfo` calls into the component, or the component finds the ASC lazily on first `HandleCue`; unbind in `EndPlay`). When the failed ability `IsA<UVeloxGameplayAbility_Fire>()` and the reason tags contain `ActivateFailCostTag`, play `DryFireSound` 2D, at most once per `DryFireMinInterval` (Config, default 0.25).
- [ ] **D5.** `AVeloxCharacter::ApplyCharacterSkin`: when the pawn has no `AVeloxPlayerState` and `AppliedSkinSlot != INDEX_NONE`, the slot is kept and only the tint is re-applied (no `ChooseSkinSlot` call).
- [ ] **D6.** `FVeloxKillFeedEntry` carries `int32 KillerPlayerId` (set from `APlayerState::GetPlayerId()` where the entry is built on the server); the announcer compares ids, not names. Existing kill-feed tests still pass.
- [ ] **D7.** `VeloxSettingsWidget` has a checkbox bound to `bForceEnemySkin` and a combo bound to `ForcedEnemySkin` whose options are the skin names of `DA_Cosmetics_Velox` (loaded through the pawn data's soft ref; falls back to the six names if the asset is missing); Apply/Defaults handle both like the other rows. `UVeloxAudioSettings::PlayUIClick` is called from every `OnClicked` handler of `VeloxMainMenuWidget`, `VeloxPauseMenuWidget`, `VeloxSettingsWidget` (nine buttons).
- [ ] **Server-log gate.** `nettest.sh` fails the round if the server log contains `VeloxAudio play`, `VeloxViewModel weapon=` or a `VeloxCosmetics .*: mesh=` line (the dedicated server must never load or play a cosmetic).
- [ ] Nits: `OnFireCue` declaration moved above the getters; the pad looks the pawn up non-const instead of `const_cast`; `ApplyToView` sets the first-person parameters only while the view mesh is visible.
- [ ] Build green (Editor + Game); 115+ tests green; nettest `FireRun BotRun ItemRun` PASS.

## Verification steps
1. `infra/ci/build.sh` → compiles, tests listed and passing.
2. Standalone with bots: `UnrealEditor-Cmd "$VELOX_PROJECT" L_ItemTest?Bots=2 -game -nullrhi -nosound -ExecCmds="log LogVeloxAudio Verbose" …` for 60 s → every `VeloxAudio play2d` line names a sound in {hit_confirm, pickup, pickup_predicted, weapon_switch, announcer, ui_click, dry fire, jumppad_own, pain_own, death_own} **and** belongs to the local player's pawn; bot fire/pain/death/pad lines are `VeloxAudio play` (3D). Count: `grep -c 'play2d .*Fire' log` is the local player's shots, not the bots'.
3. Nettest under the soak lock, idle host: `nettest.sh --scripts "FireRun BotRun ItemRun"` → PASS; the client log of BotRun shows `land_soft|land_hard` lines for bot pawns (D2); the server log passes the new gate.
4. Dry fire: a FireRun-family script that empties a weapon is not available headless — verify by code reading plus the Verbose line `VeloxAudio play2d dry_fire` in a manual PIE run on the Mac when it arrives; state this in the report.

## Report back
Standard format; list every call site changed for D1; paste the grep of step 2; say whether the kill-feed struct change needed a test update.

## Notes / outcome

Done 2026-10-31 (dev). D1-D3, D5-D7, nits and the nettest server-log gate implemented. Build green (Editor + Game), 116 tests, 0 failures (new: `Velox.Audio.OwnPawnRule`, world-free: null / uncontrolled / AI-controlled pawn are not the local player). Standalone `L_ItemTest?Bots=2` Verbose: the only `play2d` lines are the local player's (`pain_own`); bot pain/pad/jump/beam are 3D. nettest `FireRun,BotRun,ItemRun` PASS; BotRun client log with `log LogVeloxAudio Verbose`: 197 `land_soft|land_hard` lines (D2), 0 bot `play2d`; server logs: 0 gate lines. Not verifiable headless: dry fire (D3, code reading; check `VeloxAudio play2d dry_fire` on the Mac) and the positive-controller case of `IsLocalPlayerPawn`.
Deviations: D6 field added in `GameModes/VeloxKillFeed.h` and set in `GameModes/VeloxGameMode.cpp` (the struct and the builder live there, not in `VeloxGameState`); `Character/VeloxViewModelComponent.cpp` touched for the `ApplyToView` nit; the three menus have ten OnClicked handlers (4 + 3 + 3) plus the key-row proxy, all call `PlayUIClick`.
