# Velox Roadmap

Milestones are sequential; a milestone is done when every acceptance criterion below is met, the project compiles for Editor, Client and Server targets, automation tests pass, and `STATUS.md` records it. Details per task live in `docs/tasks/<milestone>/`.

Build hosts: this Linux machine compiles and cooks Linux Editor/Client/Server targets headlessly (no GPU, no PIE). macOS clients are built on the human's Mac from the same repository (UE 5.8 via Epic Games Launcher + Xcode); Windows comes later. GitHub Actions is unavailable until 2026-11-01, so CI is a local script until then.

## M0 — Infrastructure

Goal: a compiling, empty Unreal project with a working toolchain, repo hygiene and the public vhost.

Acceptance criteria:
- [ ] Machine inventory recorded (OS, CPU, RAM, disk, GPU/driver, open ports) in `docs/machine.md`.
- [ ] Linux toolchain installed: clang, cmake, build-essential, git-lfs, dotnet (needed by UnrealBuildTool on 5.x), plus the UE Linux prerequisites.
- [ ] Git LFS configured; `.gitattributes` tracks `*.uasset *.umap *.fbx *.png *.wav *.blend *.psd *.tga *.ogg *.mp4` (and other binaries agreed in ADR 0001); `.gitignore` for UE (`Binaries/`, `Intermediate/`, `DerivedDataCache/`, `Saved/`, `.vs`, `*.xcworkspace`, …).
- [ ] UE 5.8 Linux editor installed under a documented path, `UE_ROOT` recorded in `infra/env.example`.
- [ ] `Velox.uproject` with C++ module per ADR 0001; compiles for `VeloxEditor` and `Velox` (Game) on Linux and macOS (`Development` config). `VeloxServer`/`VeloxClient` targets exist and compile once a source-built engine is available (installed/Launcher engines cannot build Server/Client targets; until then the dedicated server is `Velox -server`). CI reports them as SKIPPED on an installed engine, never as passed.
- [ ] `infra/ci/build.sh` builds all available targets and runs the automation tests headlessly (`-nullrhi`), parsing the JSON report; exits non-zero on failure. `infra/ci/build-mac.sh` does the equivalent on macOS.
- [ ] One trivial automation test exists and passes (proves the test harness works).
- [ ] nginx vhost `games.inceptum.at` serves a placeholder landing page over HTTPS (Let's Encrypt), HTTP redirects to HTTPS. Config checked into `infra/nginx/`.
- [ ] Everything merged to `main`; `STATUS.md` updated.

## M1 — Movement

Goal: Quake-feel movement, replicated and predicted, tunable.

Acceptance criteria:
- [ ] `UVeloxMovementComponent : UCharacterMovementComponent` with: ground friction, ground acceleration, Quake air acceleration (strafe jumping gains speed), bunny hopping without landing speed loss (auto-hop while jump held), jump pads (launch preserving/overriding velocity per pad), rocket/plasma jump knockback hook, ramp sliding (no speed loss on slopes below a configurable angle), configurable max speeds, step-up/stair handling.
- [ ] All tunables in a `UVeloxMovementSettings : UDataAsset`, hot-reloadable in editor; no magic numbers in code.
- [ ] Full network prediction: custom `FSavedMove_Character` subclass and move-data container; server-authoritative; client correction without visible jitter at 100 ms simulated latency and 2 % loss (`p.NetEmulation` / `Net PktLag`).
- [ ] Greybox test map `L_MovementTest` with ramps, stairs, jump pads, a long strafe corridor with distance markers, and a speed/velocity debug HUD.
- [ ] Automation tests: friction decay, air-accel speed gain over N ticks, bunny-hop speed retention, jump pad launch, prediction determinism (same inputs, same output on client and server simulation).
- [ ] Done when a human can strafe-jump and bunny-hop across the test map online with a dedicated server and two clients and no visible correction jitter (human playtest sign-off).

## M2 — Combat

Goal: the full Quake 3 weapon set on GAS, lag-compensated.

Acceptance criteria:
- [ ] GAS set up per ADR: `UVeloxAbilitySystemComponent`, `UVeloxAttributeSet` (Health, MaxHealth, Armor, MaxArmor), gameplay effects for damage with armor absorption (Q3 rule: armor absorbs 2/3), self-damage factor, knockback as a movement impulse through the movement component.
- [ ] Weapons as gameplay abilities with data assets: Machine Gun, Shotgun, Plasma Gun, Rocket Launcher, Lightning Gun, Railgun. Ammo attributes, weapon switching with switch times, fire rates matching Q3 defaults as initial tuning.
- [ ] Hitscan (MG, SG, LG, RG) uses server-side lag compensation (rewind of hitboxes to client fire time, bounded window). Projectiles (PG, RL) are server-authoritative with client-side fake projectiles for responsiveness.
- [ ] Rocket jumps and plasma climbing work via self-knockback.
- [ ] Death, respawn with spawn protection timer, spawn point selection avoiding enemies.
- [ ] Automation tests: damage/armor math, ammo consumption, lag-compensation rewind picks the right historical pose, projectile ownership/self-damage.
- [ ] Tested in PIE/dedicated server with two clients at 100 ms latency: hits register where the client aimed.

## M3 — Game loop

Goal: playable FFA and Duel matches.

Acceptance criteria:
- [ ] Pickups via GAS: health (5/25/50/Mega), armor (shards/yellow/red), ammo, weapons; respawn timers; powerups (Quad, Haste, Regen, Battle Suit) with timers and effects.
- [ ] Game modes FFA and Duel (1v1, frag limit and time limit, overtime/sudden death), warmup, ready-up, match end and map restart.
- [ ] Scoreboard, frag/death feed, match timer, HUD (health, armor, ammo, weapon bar, powerup timers, crosshair, hit markers, damage indicators).
- [ ] Main menu, server connect by IP, settings (sensitivity, FOV, keybinds, audio, video), persisted in config.
- [ ] Item timers for spectators/duel per standard rules.
- [ ] Automation tests for pickup respawn timing, score/limit logic, powerup stacking rules.

## M4 — Bots

Goal: bots that play FFA/Duel credibly using the player movement component.

Acceptance criteria:
- [ ] NavMesh on the test map with nav-links for jump pads, drops, and rocket jumps; link traversal implemented through the movement component (bots strafe-jump along long corridors).
- [ ] Item routing: bots evaluate item value vs. distance and respawn timers; duel bots time Mega/Red Armor.
- [ ] Aim model with configurable reaction time, aim error, tracking smoothness; weapon choice by range and ammo; dodge/strafe behaviour under fire.
- [ ] Difficulty presets as data assets.
- [ ] Bots work on a dedicated server with human players; `AddBot`/`KickBot` console commands.
- [ ] Automation tests for aim-error distribution and item-value scoring; functional test map where a bot reaches a jump-pad-only platform.

## M5 — First real map + characters

Goal: one polished, readable map and one character.

Acceptance criteria:
- [ ] One map designed for Duel/FFA (4–6 players), blocked out, iterated with bots, art pass with strong silhouettes and baked lighting (no Lumen/Nanite), performance budget met (target 240 fps on mid-range GPU at 1080p, no hitches).
- [ ] One rigged character with run/jump/air/land/fire animations, 4+ bright, team-readable skins; Blender source in repo (LFS), export scripts in `Tools/`.
- [ ] Weapon models and first-person view models for all six weapons.
- [ ] Audio pass: weapon fire, hits, pickups, footsteps, jump pads, announcer for countdown/frags/powerups.
- [ ] Human playtest sign-off on readability and feel.

## M6 — Online

Goal: people can download the client and play on a server hosted here.

Acceptance criteria:
- [ ] Dedicated server build (Linux) packaged by `infra/ci/`; systemd unit under an unprivileged user; UDP 7777 (and only that) opened; status JSON served via nginx.
- [ ] Online subsystem (Steam or EOS, decision via ADR; credentials from the human) for sessions and NAT traversal; server browser in the main menu.
- [ ] Client downloads (Linux, macOS, Windows) and server status on `https://games.inceptum.at`.
- [ ] Rust master-server/status service only if the online subsystem's listing is insufficient (ADR decides).

## M7 — Polish

- [ ] CTF mode with flag logic, team spawns, team colours.
- [ ] Replays/demos (record network stream, playback).
- [ ] Two more maps.
- [ ] Performance pass, playtest fixes, tuning pass on movement and weapons.
