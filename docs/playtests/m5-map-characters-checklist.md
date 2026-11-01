# Velox M5 map + characters playtest (human, macOS, Apple Silicon)

Goal (M5 done criterion): a human plays **Praesidium (`L_DM1`) with the real character, view models, skins and audio, dedicated server + 2 clients + 3 bots**, and signs off readability, feel, lighting, audio and frame time. Budget: about 60 minutes after the first build (the Production bake in section 1 step 4 is extra and runs unattended). Spec: `docs/adr/0006-first-map-and-characters.md` (section 4.3 skins, 8 budget, 9 audio, 10 human criteria; amendments E0.5, E1). Headless evidence (audit, navlint, soaks, unit tests) says the map and assets are *correct*; this is the only check of how they *look, sound and run*: **everything here is a first look on a real display**. Write down everything odd, even when it is not a row.

The playtest machine is an **Apple Silicon Mac** (the Intel Mac cannot render UE 5.8). Movement, combat, game loop and bots were checked in `m1-movement-checklist.md` ... `m4-bots-checklist.md`; this list re-checks them only where the art pass touches them. The map display name is **Praesidium** (concrete and steel bunker, a q3dm6-class two-atrium map, 64 x 44 m).

## 1. Setup

1. Mac setup as in `docs/playtests/m1-movement-checklist.md` section 1 (macOS Sonoma 14.5 or newer, Xcode 26.0 or 26.1.1 and **not** 26.4, UE 5.8 from the Epic Launcher, `brew install git-lfs coreutils`, `git lfs install`), arm64 build, no Rosetta. Check out branch `m5-map-characters` (or the branch the lead names), then **`git lfs pull`** (maps, `.fbx`, `.wav`, `.png` are LFS: without it assets are stubs).
2. From the repo root: `infra/ci/build-mac.sh`. Success ends with `[ci] CI OK. Logs: Saved/CI/<timestamp>`; `VeloxServer`/`VeloxClient` print `SKIPPED` on Mac, expected. On failure attach `Saved/CI/<timestamp>/*.log` and stop.
3. Open `Velox.uproject` (first start: shader compile, 5-10 min).
4. **Task 16 part B (Production bake) comes first or as part of this playtest.** `L_DM1` on the branch may still be *unbaked* (Fallback C, unbuilt preview lighting, "LIGHTING NEEDS TO BE REBUILT" on screen). If the lead says the baked `L_DM1.umap` + `L_DM1_BuiltData.uasset` are already pushed, you only `git lfs pull`. Otherwise bake on this Mac (1-3 h, Metal, unattended, close other apps): `infra/ci/build-mac.sh --bake /Game/Maps/DM1/L_DM1 Production`. Then check with `audit_map.py --lighting` (see `docs/content-bootstrap.md`, "Lighting bake") that it prints `registry=1 lightmapped=<n>/<m> vlm=1` with `n` > 0, commit and push `L_DM1.umap` and `L_DM1_BuiltData.uasset` (LFS) and tell the lead the wall time and the `LogLightmassSolver` completion line. The lead (VM) then adds `L_DM1` to `+MapsToCook` and, once you confirm **decision 40**, switches `ServerDefaultMap` to `L_DM1`. Tables 4 and 6 (lighting, frame time) are only valid on the baked map; tables 2, 3, 5 can run before the bake, but say so in the sign-off.
5. Open map **`/Game/Maps/DM1/L_DM1`** in the editor. Play settings (three dots next to Play -> Advanced Settings): **Number of Players = 2**, **Net Mode = Play As Client** (dedicated server), **Run Under One Process = on** (the cheats find the server world only in the same process). Network emulation: Enable, target Everyone, Packet Lag **100 ms**, Packet Loss **2 %** (fallback in each client console `Net PktLag=100`, `Net PktLoss=2`); switch it **off for table 6** (frame time). Window size **1920 x 1080** for table 6 (Advanced Settings -> New Window Size).
6. **Additional Server Game Options:** `?game=FFA?Bots=3?BotDifficulty=Medium` (FFA has the Quad; Duel: `?game=Duel?Bots=1?BotsFill=1?BotDifficulty=Hard`).
7. Console (`~`, German keyboard `^`; one per client window):
   - `velox.vis.PlaceholderBody 0` (default; **1** shows the old capsule placeholder; read when a pawn spawns, so set it before the match). Rows 1-2 are judged with it at 0.
   - Frame time: `stat fps`, `stat unit`, `stat scenerendering` (draw calls, primitives; type again to toggle, `stat none` clears), `ProfileGPU` (opens the GPU visualizer; screenshot it).
   - Cheats: `velox.combat.GiveAllWeapons`, `velox.match.Ready 1 all`, `velox.items.List`, `velox.bots.Freeze 1` (bots stand still: use it for the 40 m and 20 m rows), `velox.combat.KillSelf`, `velox.move.LogCorrections`, `velox.settings.Dump` (prints the settings including the forced-skin fields). The settings menu (Escape) has the **Force enemy skin** checkbox.
   - There is no teleport cheat: walk to the positions in table 6.
8. Keys as in M3: WASD, Space, left mouse = fire, 2-8 weapons, Tab = scoreboard, F3 = ready, Escape = pause menu (on a Mac keyboard F3 may need Fn).

Map orientation (ADR 0006 section 3): **A** Mega Hall (west atrium, Mega on the +320 ledge, pad lower -> ledge), **B** RA Yard (east atrium, RA on the +320 balcony, pad and stairs), **C** North Lane (28 m strafe lane, LG on the step at mid, YA-north), **D** South Hall (28 m lane, RL lower centre, YA-south on the balcony, RG pad to E top), **E** Rail Tower in the centre (ground, +320 mid bridges to A and B, +640 top with the Railgun; rocket jumps from the two mid bridges).

## 2. Readability (FFA, 2 clients, 3 Medium bots)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 1 | **[first look]** Press Play, ready up (F3), look at a bot in the open and at your own figure from the other client. | A 140 cm low-poly figure (not the capsule), run cycle, jump, crouch pose, fire animation; no T-pose. | Reads as a character in motion; animation matches movement (sequence playback until `ABP_Velox` exists). | T-pose, foot sliding, wrong run direction, mesh sunk or floating |
| 2 | **[first look]** `velox.bots.Freeze 1`. Stand 40 m from a frozen bot against **each room's dominant surface** (A, B, C, D, E: floor, wall, door, shadowed corner). | Bot visible at once; silhouette and colour stand out from the concrete and steel. | Enemy identified in about 1 s in **every** room (ADR section 10). | Rooms and surfaces where it blends in |
| 3 | **Skin colours told apart at 20 m**: set **Force enemy skin off**, then look at the bots (skins Red, Blue, Green, Yellow, Magenta, Cyan by player slot). | Each bot a clearly different, saturated colour, the same colours as in the scoreboard and kill feed. | All colours distinguishable at 20 m; none vanishes into the wall colour. | Colours easily confused; mismatch with the scoreboard |
| 4 | **Forced enemy skin on (default, Green)**: delete your settings file or use `velox.settings.Dump` on a fresh profile, then tick the box. | Every non-owned pawn wears Green on your screen (Q3 `cg_forceModel`); scoreboard and kill feed still show true colours. | Forcing is **on by default**, Green is the most saturated thing in the room, enemies are never lost. | Default state; bots not green; own pawn recoloured |
| 5 | **Skin in the dark**: compare forced on/off in the darkest room (note which). | Skin stays visible in shadow (emissive strips at 0.35 x body colour). | Not black, not glowing like a lamp. | Room; which setting reads better |
| 6 | **Item silhouettes at 20 m** (bots frozen): **Mega Health, 50 HP, Red Armor, Yellow Armor**. | Four different shapes and colours. | You can name each at 20 m without walking closer. | Pairs you confuse |
| 7 | **Weapon pickups and ammo at 10 m**: RL, RG, LG, SG, PG and their ammo boxes (emblems). | Each weapon pickup shows its weapon mesh; each ammo box carries the matching emblem. | Weapon vs ammo and the weapon type readable at 10 m. | Which are unclear |
| 8 | **Pickup glow**: walk through a room with several items. | A glow marks items but does not dominate the frame. | Visible from 20 m, not blinding, no sorting artefacts. | Too strong or weak; flicker |
| 9 | **View model vs crosshair**: `GiveAllWeapons`, cycle 2-8 and look at each weapon in hand, then face a wall. | Weapon at the lower right, no stretch at 110 degree FOV, no wall clipping. | The crosshair area stays free for every weapon. | Weapon, what occludes |
| 10 | **Weapon in the third-person hand** (`weapon_r` socket): watch the other client's pawn and bots with each weapon. | Weapon in the right hand, barrel forward, not through the body. | Believable hold for all six weapons. | Per weapon how far off (cm, degrees) |
| 11 | **Muzzle flash**: fire each weapon in a dark room. | Short flash and brief light (at most 2 movable lights). | Not blinding, does not hide the target, no light left behind. | Weapon; too bright or long |
| 12 | **Projectile visuals** (task 13b): fire rockets and plasma at 5 m and 50 m; watch a bot's rockets. | Rocket with glow and a small light, plasma ball glow, visible explosion. | You can see and dodge incoming projectiles at 30 m. | Too faint, wrong scale |
| 13 | **Death**: kill a bot, `velox.combat.KillSelf`. | Death animation plays and holds its last frame; no capsule left over. | Body readable, no pop, no T-pose. | |

## 3. Feel (FFA or empty server, 2 clients, 100 ms / 2 %)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 14 | **Strafe lane C (North Lane)**: strafe-jump the whole 28 m at full speed, both directions. | Speed builds as on `L_MovementTest`. | No catch or speed loss at tile seams, doors or the 80 cm step at mid. | Position of any catch |
| 15 | **Strafe lane D (South Hall lower)**: same. | Same. | No catch, no jitter at 100 ms. | Position of any catch |
| 16 | **All three jump pads** (A -> Mega ledge, B -> RA balcony, D -> E top): 3 times each. | Same arc and landing as in the blockout. | Lands on the intended platform 3 of 3, no ceiling bump, no correction. | Pad; overshoot, short, bump |
| 17 | **Rocket-jump shortcuts**: from the A-ledge bridge and the B-balcony bridge to E top (RL). | One rocket jump from the mid bridge reaches the top. | Both reachable in a few tries; no ceiling in the way. | Tries per shortcut |
| 18 | **Stairs and ramps**: B stairs, D stairs, A ramp (15 degrees); bunny-hop up the ramp. | Smooth. | No camera jitter on stairs, no speed loss on the ramp. | Where |
| 19 | **Crouch**: crouch-walk through doors and around cover in C and E. | Capsule and figure lower to 100 cm. | Never stuck, never required to pass. | Where blocked |
| 20 | **Seams where clip and kit meet**: strafe along every wall, door frame, tile join and trim in A-E. | Invisible collision matches the visible walls. | No invisible wall, no snag at a joint, no standing on a trim. | Position and what |
| 21 | **Drops**: walk off the D balcony, B balcony, A ledge and E mid bridges. | Free fall, land sound. | No invisible edge; KillZ never reached. | Where |
| 22 | **Corrections**: `velox.move.LogCorrections`, play 5 minutes. | Few or no corrections. | No rubber-banding on lanes, pads or stairs. | Counts and places |

## 4. Lighting (baked map only)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 23 | **[first look]** Walk through every room. | No "LIGHTING NEEDS TO BE REBUILT" overlay; lit floors, walls, ceilings. | Message absent; no flat unlit surfaces. | Surfaces still unlit |
| 24 | **No black pits**: corners, under bridges, under stairs, door recesses. | Dark but never pure black; shapes readable. | No black hole to hide an enemy. | Place |
| 25 | **Characters in shadow**: a frozen bot in the darkest part of A, B and E. | Lit by the volumetric lightmap: darker in shadow, still its skin colour. | Not flat black; identifiable (row 5). | Room |
| 26 | **Lightmap artefacts**: walls and floors at grazing angles. | Smooth shadows. | No seams at tile borders, no light leaks. | Where |
| 27 | **Reflections** near the sphere captures. | Plausible, soft. | Nothing distracting. | |
| 28 | **Movable lights**: fire in a dark room. | Muzzle and rocket lights add light without breaking the baked look. | Consistent. | |

## 5. Audio (headphones, then speakers)

| # | What to do | What you should hear | Pass criterion | Write down |
|---|---|---|---|---|
| 29 | **[first look]** Play 2 minutes at normal volume. | Fire, impact, pain, death, jump, land, footsteps, pads, pickups, announcer. | Nothing silent that should sound; nothing clips. | Missing sounds |
| 30 | **Direction of enemy fire, eyes closed**: another player or bot fires from left, right, front, behind. | Panning tells the side (hearing through walls is intended). | Left/right right in at least 4 of 5 trials. | Hits per trials |
| 31 | **Footsteps at 10 m**: a bot runs, you stand still. | Four variants, alternating feet; none when crouched or airborne. | Audible at 10 m, quieter than weapons. | Distance heard |
| 32 | **Own weapon vs enemy**: fire while a bot fires 15 m away. | Own weapon clear (2D), enemy still audible. | Enemy not drowned out. | |
| 33 | **Six weapons distinct**: MG, SG, PG, RL, RG, LG beam (start, loop, stop), eyes closed. | Each different. | You can tell the weapon by sound. | Pairs confused |
| 34 | **Dry fire** (Mac-only, task 18b): empty a weapon and keep pressing fire. | One click per press, rate-limited, only your own. | Sounds, does not machine-gun. | |
| 35 | **Announcer**: countdown, fight, excellent, impressive, lead taken/lost/tied, one minute, overtime, match over, Quad. | One line at a time, effects duck while it speaks. | Every line intelligible and well timed. | Missing or unclear lines |
| 36 | **Pickups distinguishable**: health (5/25/50/Mega), shard, RA/YA, weapon, ammo, Quad. | Different sounds per class; respawn sound in range. | Health, armor, weapon told apart eyes closed. | Pairs confused |
| 37 | **Pad, impacts, pain**: use a pad, shoot walls and bots, take hits at three health levels. | Pad whoosh; impact per weapon; pain light/medium/heavy; death grunt. | Fit the events, no delay. | |
| 38 | **Volume sliders** in settings. | Each slider changes only its class; UI click plays. | Works and persists after restart. | |

## 6. Performance (1920 x 1080, no network emulation, baked map)

Budgets (ADR 0006 section 8): **<= 600 draw calls, <= 300 000 triangles in view, <= 4.1 ms per frame (240 fps)**. Type `stat fps`, `stat unit` and `stat scenerendering`, `velox.bots.Freeze 1`, stand at the position, wait 5 s, read the values. The Mac GPU is probably slower than the ADR's mid-range PC: the numbers matter for which position is worst and for draw calls and triangles, which are GPU independent.

| # | Position | Draw calls | Triangles | Frame / GPU ms | fps | Pass criterion |
|---|---|---|---|---|---|---|
| 39 | **A ledge**: Mega ledge, look east across the atrium. | | | | | <= 600 draw calls, <= 300 k triangles |
| 40 | **B balcony**: RA balcony, look west. | | | | | same |
| 41 | **C mid**: North Lane at mid on the LG step, look east along the lane. | | | | | same |
| 42 | **D lower**: South Hall lower centre, look west along the lane. | | | | | same |
| 43 | **E top**: Rail Tower top (RG), look toward A. | | | | | same |
| 44 | **E ground**: Rail Tower ground centre, look at a door to C. | | | | | same |
| 45 | **`ProfileGPU`** at the worst of rows 39-44. | | | | | Screenshot attached to the sign-off; any single pass > 1 ms listed |
| 46 | **Hitches**: 5 minutes with 3 bots and `stat fps`. | | | | | No periodic hitch over 50 ms; no stall at the first shot, first weapon switch or first pickup (async cosmetics load) |
| 47 | **Reference**: row 41's view versus greybox `L_ItemTest`. | | | | | Art cost in ms noted |

Static figures (`audit_map.py`, unbaked art map): draw_calls_est 568/600, 18 180 triangles, 12.35 MB lightmaps. The audit counts the whole map, `stat scenerendering` counts one view, so rows 39-44 should be below it.

## 7. Mac-only checks left open by earlier tasks

| # | What to do | Pass criterion | Write down |
|---|---|---|---|
| 48 | **`ABP_Velox`** (tasks 08/12; recipe in `docs/art/character-spec.md`, "ABP_Velox state machine", about 30 minutes): build it, set `DA_Cosmetics_Velox` `AnimClass = ABP_Velox`, play again. | Blends between idle/run/jump/land/crouch plus the fire slot; no regression against sequence playback. **Optional**: skipping it gives PASS WITH NOTES at most. | Built? Better? |
| 49 | **`weapon_r` socket and view-model offsets** (rows 9-10): tune where wrong. | Believable hold for all six weapons. | Final values |
| 50 | **Bot feel (STATUS.md question 30)**: 3 minutes each against 3 Easy, 3 Medium and 2 Hard bots on Praesidium. (a) Is Medium too easy now (20-28 % fewer kills)? (b) Easy A/B: `velox.bots.DirectHitRadiusScale 0` (pure sprayer) vs the shipped 0.5, which feels better? (c) Hard (MG about 54-58 % accuracy) fine or more human? | Answers recorded; all are data changes. | (a) (b) (c) |
| 51 | **Bots on the art map**: watch them take Mega, RA and the Quad and use the three pads and the rocket-jump links. | Same behaviour as on the blockout; none stuck at doors or trims. | Place and time |

## 8. Sign-off

Tester: ______________  Date: ____________  Mac (model / chip / macOS / Xcode): ______________________________

Branch and commit built (`git rev-parse --short HEAD`): ______________  `infra/ci/build-mac.sh` result: ______________

Map baked on this Mac? yes / no, wall time ________  (or: pulled baked map from commit ________)

- [ ] Readability (table 2, rows 1-13): PASS / FAIL
- [ ] Feel (table 3, rows 14-22): PASS / FAIL
- [ ] Lighting (table 4, rows 23-28): PASS / FAIL / not run (unbaked)
- [ ] Audio (table 5, rows 29-38): PASS / FAIL
- [ ] Performance (table 6, rows 39-47), `ProfileGPU` screenshot attached: PASS / FAIL
- [ ] Mac-only items (table 7, rows 48-51), answers to question 30 written down: DONE / SKIPPED
- [ ] Decision 40 confirmed: `ServerDefaultMap` -> `L_DM1`: YES / NO

Findings (row number, what, how often): _____________________________________________

Verdict: **PASS / PASS WITH NOTES / FAIL**  (the lead ticks the ROADMAP M5 box on PASS or PASS WITH NOTES, after task 16 part B is merged)
