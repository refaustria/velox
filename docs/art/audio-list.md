# Audio list (ADR 0006 section 9, task M5-10)

Placeholders are synthesised by `Tools/audio/synth_placeholders.py` (22.05 kHz mono 16-bit; announcer lines by `espeak-ng`) into `ArtSource/Audio/placeholders/` and imported by `Tools/editor/create_audio_content.py` as `/Game/Audio/SFX/<Group>/SW_<name>`. Replace a WAV by a real one **under the same file name** and run the import script with `VELOX_AUDIO_REIMPORT=1`. The asset paths, attenuation and concurrency assignment live in `Config/DefaultGame.ini` `[/Script/Velox.VeloxAudioSettings]` (generated from the script's table: `python3 -I Tools/editor/create_audio_content.py --print-ini`).

| File | Duration | Hook | Attenuation |
|---|---|---|---|
| `ann_excellent.wav` | 1.06 s | Announcer line `excellent` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_fight.wav` | 0.85 s | Announcer line `fight` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_impressive.wav` | 1.09 s | Announcer line `impressive` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_lead_lost.wav` | 1.67 s | Announcer line `lead_lost` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_lead_taken.wav` | 1.78 s | Announcer line `lead_taken` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_lead_tied.wav` | 1.49 s | Announcer line `lead_tied` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_match_over.wav` | 1.18 s | Announcer line `match_over` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_one.wav` | 0.84 s | Announcer line `one` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_one_minute.wav` | 1.59 s | Announcer line `one_minute` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_overtime.wav` | 1.10 s | Announcer line `overtime` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_quad_damage.wav` | 1.35 s | Announcer line `quad_damage` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_three.wav` | 0.78 s | Announcer line `three` | 2D, SC_Announcer, SM_Announcer duck |
| `ann_two.wav` | 0.76 s | Announcer line `two` | 2D, SC_Announcer, SM_Announcer duck |
| `hit_confirm.wav` | 0.12 s | ClientConfirmHit (2D) | 2D |
| `imp_bullet.wav` | 0.06 s | `DA_WeaponCosmetics_Shotgun.ImpactSound` (also MachineGun) | SA_Impact |
| `imp_lightning_hit.wav` | 0.14 s | `DA_WeaponCosmetics_LightningGun.ImpactSound` | SA_Impact |
| `imp_plasma.wav` | 0.18 s | `DA_WeaponCosmetics_PlasmaGun.ImpactSound` | SA_Explosion |
| `imp_rail_hit.wav` | 0.45 s | `DA_WeaponCosmetics_Railgun.ImpactSound` | SA_Impact |
| `imp_rocket_explosion.wav` | 0.90 s | `DA_WeaponCosmetics_RocketLauncher.ImpactSound` | SA_Explosion |
| `pickup_ammo.wav` | 0.12 s | `PickupSound` of the items using it (own take 2D at prediction, global items 2D for everyone) | SA_Pickup |
| `pickup_armor.wav` | 0.20 s | `PickupSound` of the items using it (own take 2D at prediction, global items 2D for everyone) | SA_Pickup |
| `pickup_armor_shard.wav` | 0.12 s | `PickupSound` of the items using it (own take 2D at prediction, global items 2D for everyone) | SA_Pickup |
| `pickup_health_large.wav` | 0.27 s | `PickupSound` of the items using it (own take 2D at prediction, global items 2D for everyone) | SA_Pickup |
| `pickup_health_medium.wav` | 0.18 s | `PickupSound` of the items using it (own take 2D at prediction, global items 2D for everyone) | SA_Pickup |
| `pickup_health_mega.wav` | 0.75 s | `PickupSound` of the items using it (own take 2D at prediction, global items 2D for everyone) | SA_Pickup |
| `pickup_health_small.wav` | 0.09 s | `PickupSound` of the items using it (own take 2D at prediction, global items 2D for everyone) | SA_Pickup |
| `pickup_powerup.wav` | 1.38 s | `PickupSound` of the items using it (own take 2D at prediction, global items 2D for everyone) | SA_Pickup |
| `pickup_weapon.wav` | 0.22 s | `PickupSound` of the items using it (own take 2D at prediction, global items 2D for everyone) | SA_Pickup |
| `ply_death.wav` | 0.70 s | Corpse transition | SA_Player (own 2D) |
| `ply_jump.wav` | 0.14 s | AVeloxCharacter::OnJumped | SA_Player |
| `ply_land_hard.wav` | 0.22 s | AVeloxCharacter::Landed, |Vz| >= 1000 | SA_Footstep |
| `ply_land_soft.wav` | 0.10 s | AVeloxCharacter::Landed, |Vz| >= 400 | SA_Footstep |
| `ply_pain_100.wav` | 0.34 s | Health loss, remaining health < 25 | SA_Player (own 2D) |
| `ply_pain_25.wav` | 0.22 s | Health loss, remaining health >= 50 | SA_Player (own 2D) |
| `ply_pain_50.wav` | 0.28 s | Health loss, remaining health 25-49 | SA_Player (own 2D) |
| `ply_step_1.wav` | 0.09 s | Footstep cadence (`UVeloxFootstepComponent`, `FootstepSounds`) | SA_Footstep |
| `ply_step_2.wav` | 0.09 s | Footstep cadence (`UVeloxFootstepComponent`, `FootstepSounds`) | SA_Footstep |
| `ply_step_3.wav` | 0.09 s | Footstep cadence (`UVeloxFootstepComponent`, `FootstepSounds`) | SA_Footstep |
| `ply_step_4.wav` | 0.09 s | Footstep cadence (`UVeloxFootstepComponent`, `FootstepSounds`) | SA_Footstep |
| `ui_click.wav` | 0.04 s | (in the settings only; no menu hook yet) | 2D |
| `world_item_respawn.wav` | 0.45 s | Pickup respawn (replicated) | SA_World |
| `world_jumppad.wav` | 0.28 s | AVeloxJumpPad launch (own: predicted, 2D) | SA_World (own 2D) |
| `wpn_dryfire.wav` | 0.03 s | Empty weapon, local player (`UVeloxAudioSettings::DryFireSound`, one global click) | 2D |
| `wpn_lightninggun_loop.wav` | 0.51 s | `DA_WeaponCosmetics_LightningGun.BeamLoop` (beam cue) | SA_WeaponFire |
| `wpn_lightninggun_start.wav` | 0.12 s | `DA_WeaponCosmetics_LightningGun.BeamStart` (own: 2D) | SA_WeaponFire (own 2D) |
| `wpn_lightninggun_stop.wav` | 0.20 s | `DA_WeaponCosmetics_LightningGun.BeamStop` | SA_WeaponFire |
| `wpn_machinegun_fire.wav` | 0.09 s | `DA_WeaponCosmetics_MachineGun.FireSound` (cue `...MachineGun.Fire`) | SA_WeaponFire (own fire 2D) |
| `wpn_plasmagun_fire.wav` | 0.14 s | `DA_WeaponCosmetics_PlasmaGun.FireSound` (cue `...PlasmaGun.Fire`) | SA_WeaponFire (own fire 2D) |
| `wpn_railgun_fire.wav` | 0.50 s | `DA_WeaponCosmetics_Railgun.FireSound` (cue `...Railgun.Fire`) | SA_WeaponFire (own fire 2D) |
| `wpn_rocketlauncher_fire.wav` | 0.45 s | `DA_WeaponCosmetics_RocketLauncher.FireSound` (cue `...RocketLauncher.Fire`) | SA_WeaponFire (own fire 2D) |
| `wpn_shotgun_fire.wav` | 0.35 s | `DA_WeaponCosmetics_Shotgun.FireSound` (cue `...Shotgun.Fire`) | SA_WeaponFire (own fire 2D) |
| `wpn_switch.wav` | 0.10 s | Local inventory current-weapon change (`UVeloxAudioSettings::WeaponSwitchSound`, one global click, 2D) | 2D |

Total: 52 files, 1.11 MB.

Attenuation (Natural Sound, no occlusion, no reverb send; inner radius / falloff distance in cm): SA_WeaponFire 300/6000, SA_Explosion 500/8000 (air-absorption LPF from 3000), SA_Impact 200/3000, SA_Footstep 100/2000, SA_Player 150/3000, SA_Pickup 200/2500, SA_World 200/3000. Concurrency: SCon_Weapons 12, SCon_Footsteps 8, SCon_Impacts 8 (stop oldest). Own-player sounds are 2D and outside the concurrency groups.

Sound classes: `SC_Master` > `SC_Effects`, `SC_Music`, `SC_Announcer`, `SC_UI`; the settings menu's Effects slider drives `SC_Effects`, `SC_Announcer` and `SC_UI`. `SM_Announcer` (-6 dB on `SC_Effects`) is pushed while an announcer line plays.
