# Velox M2 combat playtest (human, macOS)

Goal (M2 done criterion): a human fires all six weapons, rocket-jumps, kills and dies **online, dedicated server + 2 clients, 100 ms / 2 % loss**, and the combat feels right: hits register where you aimed, no snap on rocket jumps, readable feedback, sane death and respawn. Budget: about 45-60 minutes after the first build (a repeat build is a few minutes). Spec: `docs/adr/0003-combat-gas-and-lag-compensation.md`; setup is the same as M1 (`docs/playtests/m1-movement-checklist.md` sections 1-2).

## 1. Build and PIE setup

1. Prerequisites as in the M1 checklist section 1 (Xcode, UE 5.8 from the Epic Launcher, `brew install git-lfs coreutils`, `git lfs install`). Check out branch `m2-combat` (or the branch the lead names), then **`git lfs pull`** (maps and data assets are LFS files; without it the editor reports corrupt assets).
2. From the repo root: `infra/ci/build-mac.sh` (`UE_ROOT="/path/to/UE_5.8" infra/ci/build-mac.sh` if UE is elsewhere). Success ends with `[ci] CI OK. Logs: Saved/CI/<timestamp>`. `VeloxServer`/`VeloxClient` print `SKIPPED` on Mac, expected. On failure attach `Saved/CI/<timestamp>/*.log` and stop.
3. Open `Velox.uproject` (first start: shader compile, 5-10 min). Answer **Yes** to "Missing modules / rebuild?". Then open the map **`L_CombatTest`** (Content Browser -> Maps; the startup map is still `L_MovementTest`, which also works for most items; if `L_CombatTest` is missing, use `L_MovementTest` and note it).
4. Play settings (three dots next to Play -> Advanced Settings): **Number of Players = 2**, **Net Mode = Play As Client** (starts a dedicated server), **Run Under One Process = on** (required: the cheats below find the server world only in the same process; cvars are then shared by server and both clients).
5. Network emulation: same panel, Enable Network Emulation, target Everyone, Packet Lag **100 ms**, Packet Loss **2 %**. Fallback: in **each client console** `Net PktLag=100` then `Net PktLoss=2`. For item 21 you need **250 ms** (`Net PktLag=250`, loss 2 %); switch back with `Net PktLag=100` afterwards.
6. Press **Play**. Two windows (Client 1, Client 2); click into one for input. Mouse = look, WASD = move, Space = jump, **left mouse = fire**. Console: **`~`** (German keyboard: `^`), one console per client window; Esc releases the mouse.
7. Weapon keys: **2 = Machine Gun, 3 = Shotgun, 5 = Rocket Launcher, 6 = Lightning Gun, 7 = Railgun, 8 = Plasma Gun**; mouse wheel = next/previous weapon (skips weapons you do not own). If a key does nothing, write the key and the weapon down and use `velox.combat.FireTest <MG|SG|RL|LG|RG|PG>` (selects and fires once) as a workaround.
8. **There is no health/armor HUD and no pickups yet (M3).** Health and armor are read from the **Output Log** (Window -> Output Log, filter `Velox`; lines `VeloxShot`, `VeloxHitConfirm`, `VeloxKill`, `VeloxDeath`, `VeloxRespawn`, `VeloxSelfSplash`) and set with the cheats below. The movement HUD (`velox.hud.Movement 0|1`) still shows speed, fps, ping, corrections.
9. Start of every session, in **Client 1's console** (works from any client; the cheat runs on the server):
   - `velox.combat.GiveAllWeapons` -> you own all six weapons with ammo at the cap. Re-run after every death (you respawn with the machine gun only).
   - `velox.combat.SpawnDummy` -> a strafing dummy 1000 cm in front of you. `velox.combat.SpawnDummy x y z` places it at a world position (cm); add `invulnerable` as last argument for a dummy that never dies (use for aim/tracking checks). Dummies respawn in place after they die.
   - `velox.combat.DebugDraw 0|1` toggles the weapon tracer/impact/beam debug draws (default 1, they are the only visuals for shots right now).
10. Other cheats used below: `velox.combat.KillSelf` (suicide, 1000 environment damage), `velox.combat.KillTest [env|rail]` (kills the nearest dummy; `rail` = 100 damage then a railgun shot), `velox.combat.TestDamage <Damage> [Armor (-1 keeps)] [Self 0|1] [Pellets] [DirX DirY DirZ]` (damage and armor on the local player), `velox.combat.TestRespawn` (restart your pawn), `velox.combat.RocketJumpTest [PitchDeg=-90] [quit]` and `velox.combat.FireTest <weapon> [AimDistanceCm]` (automated, optional). Live tuning: `velox.combat.SelfDamageScale`, `SelfKnockbackScale`, `KnockbackSpeedPerDamage`, `MaxKnockbackDamage`, `ArmorProtectionPercent`, `velox.combat.LagComp 0|1`. Vanilla Q3 numbers are the defaults (ADR 0003): self damage halved, knockback capped by `MaxKnockbackDamage`, armor absorbs 2/3, spawn health 125, **spawn protection 0 s** (see item 24).

Rule for all rows: keep the other client's window visible now and then. "Snap" = a visible teleport/rubber-band of your own view at 100 ms / 2 %.

## 2. Checklist

Mark **P**ass / **F**ail in the box; write the requested value in the last column.

### A. Weapons (against a dummy, then against the other client)

| # | Done | Pass/Fail | What to do | Pass criterion | What to write down |
|---|---|---|---|---|---|
| 1 | [ ] | [ ] P [ ] F | `GiveAllWeapons`, `SpawnDummy`. Key **2**, hold fire at the dummy 10 m away. | Rapid hitscan fire, tracer lines, hits register while the dummy strafes (aim at where it is on your screen). Output Log `VeloxShot hits=1`. | Hits out of ~20 shots: ____ ; feel 1-5 |
| 2 | [ ] | [ ] P [ ] F | Key **3**, shoot the dummy at 3 m, then at 15 m. | Many pellets close up (high damage), few or none at range; spread looks even. | Pellets hitting at 3 m / 15 m: ____ / ____ ; shots to kill: ____ |
| 3 | [ ] | [ ] P [ ] F | Key **5**, shoot the dummy, then the floor/wall next to it. | Visible rocket flies, explodes on contact, splash damages and pushes the dummy; slow rate of fire. | Does the rocket start where you expect (muzzle/eye)? Seen at what delay after click (ms estimate): ____ |
| 4 | [ ] | [ ] P [ ] F | Key **6**, hold fire on the dummy for 3 s while it strafes. | Continuous beam (debug draw), damage ticks, beam stops when you release or switch; short range. | Range at which it stops hitting: ____ cm; stutter? |
| 5 | [ ] | [ ] P [ ] F | Key **7**, rail the dummy 3 times from 20 m. Then `velox.combat.KillTest rail`. | Instant hit, one-shot damage (100), slow refire (~1.5 s), a kill shows `VeloxKill ... score_killer=1`. | Misses where the crosshair was on the dummy: ____ ; feel 1-5 |
| 6 | [ ] | [ ] P [ ] F | Key **8**, hold fire at the dummy at 10 m, then aim up and fire a volley at a wall. | Fast, visible plasma balls, small splash, steady rate; balls hit what the crosshair shows. | Climb height with plasma (see item 12); do balls lag behind the crosshair? |
| 7 | [ ] | [ ] P [ ] F | All of 1-6 with Client 2 as the target: Client 2 stands still in the open, Client 1 shoots it with each weapon, then Client 2 shoots Client 1. | Each weapon damages the other player; Client 2's window shows the same shots and Output Log hit lines; no ghost hits. | Weapons that fail to hit or hit through walls: ____ |
| 8 | [ ] | [ ] P [ ] F | Moving target at 100 ms / 2 %: Client 2 strafe-runs past at 10 m; Client 1 rails it, aiming on its **screen** position. | Hits register where you saw the player (lag compensation). Then `velox.combat.LagComp 0` and repeat: noticeably more misses. Restore with `velox.combat.LagComp 1`. | Rail hits out of 10 with LagComp 1 / 0: ____ / ____ |
| 9 | [ ] | [ ] P [ ] F | Rail duel: Client 1 vs Client 2, 100 ms / 2 %, first to 5 kills; both use only the railgun. | Fair: nobody feels shot "around corners" or "after cover". | Score ____ : ____ ; cheap deaths (describe): |

### B. Damage, armor, rocket jump

| # | Done | Pass/Fail | What to do | Pass criterion | What to write down |
|---|---|---|---|---|---|
| 10 | [ ] | [ ] P [ ] F | `velox.combat.TestDamage 40`, then `TestDamage 40 100` (sets armor 100), read the Output Log. Compare with `TestDamage 100 0`. | Health drops by the Q3 table (e.g. 100 dmg vs 100 armor: -33 health / -67 armor; vs 0 armor: -100/0). | Values logged: ____ |
| 11 | [ ] | [ ] P [ ] F | Rocket jump: key **5**, look straight down (or `velox.combat.RocketJumpTest`), jump and fire at your feet. | You fly about 3x higher than a normal jump (Q3 apex, ~7 m), health drops by ~50 (halved self-damage). | Apex above start: ____ cm (HUD vertical speed/Z); health lost: ____ |
| 12 | [ ] | [ ] P [ ] F | Rocket jump at 100 ms / 2 %, 5 times, look at the HUD `corrections` line before and after. | **No snap at launch and none at landing**; `corrections` +0-1 per jump (last <= 5 cm). The other client sees the same arc. | corrections before/after: ____ / ____ ; any visible snap (launch/landing)? |
| 13 | [ ] | [ ] P [ ] F | Forward rocket jump: run, aim 45 degrees down-forward, jump and fire; chain two (second in the air). | Speed gain, controllable, predictable direction. | Peak speed XY: ____ cm/s |
| 14 | [ ] | [ ] P [ ] F | Plasma climb: key **8**, look straight down against a wall/at the floor, hold fire and jump. | Plasma splash lifts you in small steps; you can gain height slowly. | Height gained: ____ ; usable? |
| 15 | [ ] | [ ] P [ ] F | Self damage: fire a rocket at a wall 1 m in front of you with full health. | Splash hurts you (halved) and pushes you back; no damage if you stay 5 m away. | Health lost: ____ |

### C. Feedback, switching

| # | Done | Pass/Fail | What to do | Pass criterion | What to write down |
|---|---|---|---|---|---|
| 16 | [ ] | [ ] P [ ] F | Hit marker, hitscan: shoot the dummy with 2, 3, 7. Hit the **world** next to it. | A marker appears on a hit, **not** on a miss; also on a kill. Visible against bright and dark backgrounds, long enough to notice (default `HitMarkerSeconds`). | Visible at 5 m / 30 m: ____ ; too small/large/short? |
| 17 | [ ] | [ ] P [ ] F | Hit marker, rocket/plasma: hit the dummy directly and with splash only. | Marker comes when the explosion hits someone, **after** the server confirms (a delay of about your ping is normal and by design, server-confirmed policy). | Delay noticeable (ms estimate): ____ ; acceptable? |
| 18 | [ ] | [ ] P [ ] F | Switch weapons quickly with **keys** 2, 3, 5, 6, 7, 8 and with the **mouse wheel** up/down (all 6 owned). | Selection is immediate, each weapon needs raise/drop time (~0.2 s drop, ~0.25 s raise); wheel skips unowned weapons. | Wrong weapon ever selected? Wheel direction OK? |
| 19 | [ ] | [ ] P [ ] F | Redirect a switch mid-drop: press 5, and within 0.2 s press 7 (and 3). Fire at the end. | Ends on the **last** requested weapon, no stuck state, no free shot with the old weapon, no double fire. | Final weapon: ____ ; anything odd? |
| 20 | [ ] | [ ] P [ ] F | Switch feel at 100 ms / 2 %: fire rail, press 2 then immediately hold fire, repeat 10 times; also 3 -> 7 and 5 -> 6. | The first shot after a switch comes **when you expect** (after raise time, not noticeably later), no dropped click. | Late/early shots out of 10: ____ ; feel 1-5 |
| 21 | [ ] | [ ] P [ ] F | Same as 20 with `Net PktLag=250`. Then back to `Net PktLag=100`. | Still no lost shot or wrong weapon; a visible extra delay up to ~one RTT is acceptable. | Late/lost shots out of 10: ____ ; worse than at 100 ms? |

### D. Death, respawn, colours

| # | Done | Pass/Fail | What to do | Pass criterion | What to write down |
|---|---|---|---|---|---|
| 22 | [ ] | [ ] P [ ] F | Death camera: `velox.combat.KillSelf` in open space, then Client 2 kills Client 1 with the railgun. Watch your own view. | View switches to the corpse: sensible **facing** (toward the corpse/killer?), **distance** not too close/far, nothing clipping. Your body stays where it died (3 s) and does not move or slide on Client 1. | Facing, distance OK? Should the camera turn toward the killer? (y/n) |
| 23 | [ ] | [ ] P [ ] F | Death camera at a wall: die with your back/side against a wall, in a corridor, and under a low ceiling. | Camera backs off from the wall (no view inside geometry). | Clipping seen where: |
| 24 | [ ] | [ ] P [ ] F | Respawn: after death wait, then **press fire or jump**. Try pressing at 0.5 s, 1.7 s, 3 s. Do nothing once and wait for the forced respawn (~20 s). | Pressing earlier than 1.7 s is ignored; at/after 1.7 s you respawn at a start away from other players; forced respawn comes by itself. | Time to respawn after press: ____ ; any lost press? |
| 25 | [ ] | [ ] P [ ] F | Spawn protection (default **0 s**): respawn next to a shooting Client 2. | You are hittable immediately (Q3). Judge: does that feel right, or would you want ~1-2 s? | Prefer 0 s / 1 s / 2 s: ____ |
| 26 | [ ] | [ ] P [ ] F | Kill an invulnerable dummy (`SpawnDummy 0 0 110 invulnerable`) and a normal one; kill both. | Invulnerable: no damage, hits still count; normal dummy dies, corpse, respawns in place, strafes again. | Any dummy stuck/falling/not respawning? |
| 27 | [ ] | [ ] P [ ] F | Colours: have both clients plus 3-6 dummies visible in `L_CombatTest` (several `SpawnDummy`), look from near and far, in bright and dark areas. Palette: red, blue, green, yellow, magenta, cyan, orange, violet. | All 8 colours are easy to tell apart from the floor/walls and from each other; corpses look visibly darker. | Hardest colour(s) to read: ____ ; vs which background |

## 3. Frame rate and stability

| # | Done | Pass/Fail | What to do | Pass criterion | What to write down |
|---|---|---|---|---|---|
| 28 | [ ] | [ ] P [ ] F | During rapid fire (items 1, 6) with 3 dummies on screen, watch the HUD fps. | No frame drop below 60 fps, no hitches when rockets explode. | Min fps ____ |
| 29 | [ ] | [ ] P [ ] F | Play 5 minutes freely, with deaths and weapon changes; scan the Output Log for `Error`/`Warning` lines with `Velox`. | No crash, no ensure/assert; warnings only the known ones. | Crashes/ensures: ____ |

## 4. Reporting

- Send back: this file filled in, the newest `Saved/Logs/Velox*.log` for each session, a screenshot of the death camera and of a hit marker, and anything from `Saved/Crashes/`.
- Format for free feedback: `<topic> | too low / right / too high | what you expected`. Numeric wishes as final values (e.g. `HitMarkerSeconds 0.25`), not "a bit more". Live tuning works for the `velox.combat.*` cvars in section 1.10; other fields are data asset values in `Content/Data/DA_CombatSettings_Default`.

### Feedback template

```
Feel ratings, 1 (bad) .. 5 (great):
Machine Gun __   Shotgun __   Rocket Launcher __   Lightning Gun __   Railgun __   Plasma Gun __
Rocket jump __   Weapon switch (100 ms) __   Switch (250 ms) __   Hit marker __
Death camera __   Respawn __   Player colours __   Overall combat __

Weapon notes (damage, rate, spread, projectile speed too slow/fast):
Rocket jump / plasma climb notes (snap at launch/landing?):
Hit marker (visibility, timing, hitscan vs projectile):
Death camera (facing, distance, wall back-off, turn toward killer?):
Respawn (1.7 s wait, fire/jump press, spawn protection 0 s ok?):
Colours that are hard to read:
Anything else / bugs (with item number):
```

## 5. Sign-off (paste into `STATUS.md`)

```
M2-09 human playtest (<date>, <name>)
Build: infra/ci/build-mac.sh -> CI OK (Mac, UE 5.8.x, <Apple Silicon|Intel>)
Setup: PIE, dedicated server + 2 clients, Run Under One Process on, 100 ms / 2 % (<both ways|outgoing only>), map <L_CombatTest|L_MovementTest>
Weapons MG/SG/RL/LG/RG/PG work vs dummy: <y/n x6>    vs player: <y/n>    LagComp hits 1/0: <n>/<n> of 10
Armor/damage table: <y/n>    Rocket jump apex: <n> cm, HUD corrections: <n>, snap at launch/landing: <none|describe>
Hit markers (hitscan/projectile): <ok|issue>    Switch feel 100 ms / 250 ms: <ok|issue>/<ok|issue>
Death camera: <ok|issue>    Respawn 1.7 s: <ok|issue>    Spawn protection: <0 s ok | want n s>    Colours: <ok|issue>
Min fps: <n>    Crashes: <none|describe>
Feel ratings: MG _ SG _ RL _ LG _ RG _ PG _ RJ _ switch _ marker _ deathcam _ respawn _ colours _
Verdict: M2 done criterion <MET | NOT MET: reasons>
```
