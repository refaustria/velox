# 10 — Audio foundation: settings, attenuation, hooks, footsteps, announcer, placeholder sounds

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 09 (`UVeloxWeaponCosmetics` sound fields), ADR 0006 §9

## Goal
Every event in ADR 0006 §9 makes a sound on clients with the right attenuation, from data: weapons through the cue handler, pickups and respawns, footsteps by distance cadence, jump/land/pad, pain/death, and the announcer. All ≈ 48 placeholder WAVs are synthesised by script so the pass is testable without the human's pack.

## Scope — files/modules the agent may touch
- `Source/Velox/Core/VeloxAudioSettings.h/.cpp` (new `UDeveloperSettings`, config `DefaultGame.ini`), `Character/VeloxFootstepComponent.h/.cpp`, `Character/VeloxFootstepMath.h` (new), `Character/VeloxCharacter.*` (component, `Landed`, jump event hook, pain/death hook via the health component delegate), `Weapons/VeloxCombatFeedbackComponent.cpp` (sounds + beam audio component + ≤ 2 movable lights), `Items/VeloxItemDefinition.h` (`PickupSound`, `RespawnSound`), `Items/VeloxPickup.cpp` (play on the rep path and on prediction), `Movement/../VeloxJumpPad.*` — **only** adding `MulticastLaunchCue` (the pad actor lives in `Movement/` or `Items/`; touch nothing else there), `UI/VeloxAnnouncerComponent.h/.cpp` (new), `GameModes/VeloxPlayerController.cpp` (create the announcer for local controllers)
- `Tools/audio/synth_placeholders.py`, `Tools/audio/announcer_lines.txt` (new), `Tools/editor/create_audio_content.py` (new: imports the WAVs, creates `SA_*`, `SCon_*`, `SC_Announcer`, `SC_UI`, `SM_Announcer`, fills the cosmetics/item sound fields), `Content/Audio/**` (LFS), `docs/art/audio-list.md`
- `Source/Velox/Tests/VeloxAudioTests.cpp` (`Velox.Audio.FootstepCadence`)
- `UI/VeloxGameUserSettings.*` only to map the new sound classes onto the existing sliders.

## Relevant ADRs / references
- ADR 0006 §9 (set, attenuation table, concurrency, hooks, announcer rules), §1 (soft refs, server never loads audio)
- ADR 0004 §1/§3 (pickup cosmetics path `OnRep_RespawnServerTime`, powerup cue), ADR 0003 §4 (cue tags), `Source/Velox/Weapons/VeloxCombatFeedbackComponent.h`

## Acceptance criteria
- [ ] `synth_placeholders.py` (pure Python `wave`/`math`, no numpy) writes 48 kHz 16-bit mono WAVs for every item of §9 to `ArtSource/Audio/placeholders/` with distinct timbres per weapon (noise burst, chirp, square, saw, sine + envelope); announcer lines via `espeak-ng` when `which espeak-ng` succeeds, else two-tone stingers; total ≤ 15 MB. `docs/art/audio-list.md` lists each file, its hook and its attenuation asset.
- [ ] `create_audio_content.py` imports them to `Content/Audio/SFX/...`, creates the seven `SA_*` with the table's inner radius / falloff (Natural Sound, no occlusion), `SCon_Weapons 12 / SCon_Footsteps 8 / SCon_Impacts 8` (steal oldest), `SC_Announcer`, `SC_UI`, `SM_Announcer` (−6 dB on `SC_Effects`), and sets the sound fields of `DA_WeaponCosmetics_*`, `DA_Item_*` and `UVeloxAudioSettings` (config). Idempotent.
- [ ] `UVeloxAudioSettings`: soft refs for attenuation/concurrency/classes and the generic sounds (jump, land soft/hard, pain × 3, death, pad, item respawn, hit confirm, dry fire, switch, UI click, announcer lines), `FootstepDistanceCm 170`, `FootstepMinSpeed 100`, `LandSoftVz 400`, `LandHardVz 1000`, `AnnouncerQueueDepth 2`, `LeadLineCooldown 5`.
- [ ] Hooks implemented as §9 states; the own weapon and own pickups play 2D; everything else `PlaySoundAtLocation` with its `SA_*`; the LG beam is one `UAudioComponent` per beam cue start/stop; movable muzzle/rocket lights ≤ 2 concurrent (`CastShadows false`).
- [ ] `UVeloxFootstepComponent`: distance cadence in `VeloxFootstepMath` (unit-tested: 170 cm spacing, none crouched/airborne/below 100 cm/s, L/R alternation), sound at `foot_l`/`foot_r` sockets (fallback: actor location ± 15 cm); identical for bots and proxies.
- [ ] `AVeloxJumpPad::MulticastLaunchCue` (Unreliable) with the owner's predicted-launch dedupe; `Landed` soft/hard; jump grunt on the movement jump event (read the CMC's existing jump latch/event — do not modify `Movement/`).
- [ ] `UVeloxAnnouncerComponent` on local controllers only: countdown `three/two/one/fight` from the GameState phase/time, `one_minute`, `overtime`, `match_over`, lead lines (own score vs. best other, cooldown), `quad_damage` on the powerup cue, `excellent` (two frags ≤ 2 s), `impressive` (two consecutive rail hits); queue depth 2, one at a time.
- [ ] Dedicated server: no audio asset loaded (Verbose line). Build green; `nettest.sh --scripts "FireRun ItemRun"` PASS.

## Verification steps
1. `python3 -I Tools/audio/synth_placeholders.py --out ArtSource/Audio/placeholders` → 48 files, sizes printed.
2. `infra/ci/build.sh` after the import script run; `Velox.Audio.FootstepCadence` passes.
3. `-game -nullrhi` on `L_ItemTest` with `?Bots=2` and `-ExecCmds="log LogVeloxAudio Verbose"` (add the category): expect footstep, pickup, respawn and announcer lines without `LogAudio` errors (NullRHI still runs the audio mixer with the null device; if the mixer is disabled headless, assert the *attempt* lines).
4. `nettest.sh --scripts "FireRun ItemRun"` under the soak lock → PASS. `du -sh Content/Audio ArtSource/Audio`.

## Report back
Standard format; the file count/size, and which hooks could not be exercised headless.

## Notes / outcome
