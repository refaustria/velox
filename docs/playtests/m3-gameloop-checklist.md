# Velox M3 game loop playtest (human, macOS)

Goal (M3 done criterion): a human plays **FFA and Duel online, dedicated server + 2 clients, 100 ms / 2 % loss** through the whole loop (main menu -> connect -> warmup -> countdown -> live -> end -> restart) and judges pickups, powerups, HUD, scoreboard and menus. Budget: about 60 minutes after the first build (a repeat build is a few minutes). Spec: `docs/adr/0004-game-loop-items-modes-ui.md` (with amendments C1-C10). Movement and combat feel were checked in M1/M2; this checklist re-checks them only where M3 changed them (mouse look, Haste, Quad).

Read first: the **menus, HUD layout, keyboard keys (Tab/F3/Escape), mouse-click rebinding and the new mouse sensitivity were never run on a real display** (all automation was headless). Rows marked **[first look]** are why this playtest exists; write down everything odd, even when it is not a row.

## 1. Mac setup

1. Intel Mac, macOS Sonoma 14.5 or newer, Xcode **26.0 or 26.1.1** (not 26.4), UE 5.8 from the Epic Launcher, `brew install git-lfs coreutils`, `git lfs install`. Details: `docs/playtests/m1-movement-checklist.md` section 1.
2. Check out branch `m3-gameloop` (or the branch the lead names), then **`git lfs pull`** (maps and data assets are LFS files; without it the editor reports corrupt assets).
3. From the repo root: `infra/ci/build-mac.sh` (`UE_ROOT="/path/to/UE_5.8" infra/ci/build-mac.sh` if UE is elsewhere). Success ends with `[ci] CI OK. Logs: Saved/CI/<timestamp>`; `VeloxServer`/`VeloxClient` print `SKIPPED` on Mac, expected. On failure attach `Saved/CI/<timestamp>/*.log` and stop.
4. Open `Velox.uproject` (first start: shader compile, 5-10 min). Answer **Yes** to "Missing modules / rebuild?". The game now starts in **`L_MainMenu`** (project default map); the editor startup map is `L_ItemTest`.
5. Maps: **`L_MainMenu`** (menus) and **`L_ItemTest`** (M3 arena, 6000 x 4000 cm): 18 labelled items in a row along the south wall (y = -1700: Health Small/Medium/Large, Armor Shard, six Ammo, six Weapons, Regen, Battle Suit); Mega Health and Red Armor on 3 m platforms, each reached by a jump pad; Yellow Armor at the top of a 15 deg ramp (y = 1200); Haste at the start of the 50 m marked corridor along the north wall (y = 1775, x = -2400); Quad near the centre (500, 0); 8 player starts; an intermission camera. Powerups first appear 30-60 s after a level reset.
6. Game mode: the map's own default may still be the base mode (no match rules, no phase banner). Select the mode with PIE's **Additional Server Game Options**: `?game=FFA` (sections 3, 4, 5, 6) or `?game=Duel` (section 7); a short FFA match is `?game=FFA?FragLimit=3`. If the field is missing in your editor, tell the lead; the alternative is the console command `open L_ItemTest?game=FFA` in a standalone game.
7. Play settings (three dots next to Play -> Advanced Settings): **Number of Players = 2** (3 in section 7), **Net Mode = Play As Client** (starts a dedicated server), **Run Under One Process = on** (required: the cheats find the server world only in the same process). Network emulation: Enable, target Everyone, Packet Lag **100 ms**, Packet Loss **2 %**; fallback in each client console `Net PktLag=100` then `Net PktLoss=2`.
8. Press Play. Two windows (Client 1, Client 2); click into one for input. Console: **`~`** (German keyboard `^`), one per client window. **Esc** now opens the game's pause menu; Shift+F1 releases the mouse. Keys: WASD, Space, left mouse = fire, 2 MG, 3 SG, 5 RL, 6 LG, 7 RG, 8 PG, wheel = next/previous weapon, **Tab** (hold) = scoreboard, **F3** = ready, **Escape** = pause menu. On a Mac keyboard F3 may need the Fn key (System Settings -> Keyboard -> "Use F1, F2 etc. keys as standard function keys"): if Tab/F3/Escape do nothing, write down exactly what you pressed. Same-code fallbacks: `velox.match.Ready 1`, `velox.menu.Disconnect`, `velox.ui.DumpScoreboard`.
9. Cheats (non-shipping builds; the server-side ones work from any client window): `velox.combat.GiveAllWeapons`, `velox.combat.KillSelf`, `velox.combat.SpawnDummy`, `velox.combat.TestDamage <Damage> [Armor (-1 keeps)] [Self 0|1]`, `velox.items.List`, `velox.items.Spawn <Name> [cm]` (e.g. `Health.Medium`, `Armor.Yellow`, `Powerup.Quad`), `velox.items.GivePowerup <Quad|Haste|Regen|BattleSuit> [seconds]` (every player pawn), `velox.move.Haste <seconds>`, `velox.match.Phase <Warmup|Countdown|Live|Overtime|PostMatch>`, `velox.match.Ready [0|1] [all]`, `velox.match.Join [index]`, `velox.match.AddPlayer`, `velox.feed.Frag [N] [delay]`, `velox.feed.List`, `velox.settings.Dump`, `velox.settings.Set <sens|fov|invert|autoswitch|name|master|effects|music|aa|crosshair|fps> <value>`, `velox.settings.Rebind <Mapping> <Key>`, `velox.settings.ResetKeys`, `velox.menu.Connect <host[:port]>`, `velox.menu.Disconnect`, `velox.ui.DumpHUD`, `velox.ui.DumpScoreboard`, `velox.hud.Movement 0|1`. Output Log filters: `VeloxPickup`, `VeloxMatch`, `VeloxHUD`, `VeloxMenu`.

## 2. Main menu and settings (open `L_MainMenu`, Play with Number of Players = 1, Net Mode Standalone)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 1 | **[first look]** Start in `L_MainMenu`. | Menu on screen with a **visible, movable mouse cursor**; address field, Connect, Host, Settings, Quit, status line. | Cursor visible, every button reacts to hover and click, text readable. | Resolution; what is cut off, too small or overlapping |
| 2 | **[first look]** Settings: change sensitivity, FOV, a volume, toggle invert Y, Apply, Back, reopen. | Values stick; Defaults resets them. | All controls clickable and persistent in the session. | Which control misbehaves |
| 3 | Persistence: set sensitivity 7, FOV 100, name `Tester`, Apply. Quit PIE and the editor, reopen, Play `L_MainMenu`, open Settings; also `velox.settings.Dump`. | Same values. | All three persist. | Any value that reverted |
| 4 | **[first look]** Rebind **by mouse click**: Settings -> key list -> click the Jump row, press `E`, Apply, Back; join a match (section 3) and jump with `E`; restart PIE and check it persisted; then use Defaults. | Row shows a waiting state, then `E`. | Click-to-rebind works through real Slate input and survives a restart. | Text while waiting; does Escape cancel; does Space still jump |
| 5 | Console fallback for row 4: `velox.settings.Rebind Fire MouseRightButton`, test, `velox.settings.ResetKeys`. | Fire moves to the right mouse button and back. | Works and resets. | Output Log result line |
| 6 | **[first look]** Wrong IP: type `10.255.255.1`, click Connect, wait. | Status "connecting", then after about **20 s** back in `L_MainMenu` with a failure message. | Message within about 25 s, menu usable again, no hang or black screen. | Seconds until message; exact text |
| 7 | Host: click Host (listen server on the arena), pick up a health item, Escape -> Disconnect. | Arena loads, you play, you return to the main menu with the cursor. | No crash. | Load time; leftovers of the old map |

## 3. FFA: HUD, pickups, scoreboard (`?game=FFA`, 2 clients, 100 ms / 2 %)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 8 | **[first look]** HUD layout during the first 30 s. | Health and armor (numbers, colour bands), ammo and weapon name, weapon bar, clock and phase banner, crosshair. | Health, armor, ammo and clock readable at a glance. | Hidden/overlapping/too small elements; screenshot |
| 9 | Phase banner at the start. | "Warmup", then a countdown (about 20 s), then Live; the clock starts at the time limit. | Same phase and countdown on both clients (+-1 s). | Countdown value on each client at the same moment |
| 10 | Health: `velox.combat.TestDamage 60`, walk over Health.Medium (+25) and Health.Large (+50); at full health walk over them again. | Rises when hurt; refused (item stays) at full health; taken item reappears after its label time (35 s). | Gains match; at 100 ms the item vanishes for you without a visible delay. | Touch-to-vanish delay (guess); any taken item that stayed visible or refused one that vanished |
| 11 | Armor: Shard (+5), Yellow (+50, ramp top), Red (+100, platform, jump pad). | Armor rises, band colour changes; the pad throws you onto the platform. | Values correct; platform reachable only via the pad. | Pad feel; any snap |
| 12 | Ammo and weapons: `velox.combat.GiveAllWeapons`, fire some, then walk over each Ammo item; take a weapon you do not own (die once to lose them). | Ammo rises (Bullets 50, Shells 10, Rockets 5, Cells 30, Lightning 60, Slugs 10); the new weapon appears in the bar; **auto-switch** to it when you hold the machine gun and the setting is on. | Auto-switch only when enabled. | Switch late or firing by accident |
| 13 | **Sounds and "took" message** for each pickup type. | A pickup message for the item you took and a sound per pickup (placeholder sounds). | You notice every pickup without looking at the HUD. | Silent items; message position and duration |
| 14 | Weapon respawn: take a weapon, stand on the spot. | Respawn after about 5 s, no double grant (standing on it gives +1 ammo per respawn, Q3). | Returns after about 5 s. | Seconds |
| 15 | **[first look]** Hold **Tab**, release. | Scoreboard while held: names, score, kills, deaths, ping; hidden on release. | Reliable show/hide; own row clear. | Key used; layout problems |
| 16 | Fight: `velox.combat.GiveAllWeapons`, kill each other 3 times with different weapons, one `velox.combat.KillSelf`. | Feed lines (killer, weapon, victim, colours), suicide line; scores and deaths update on both clients. | Feed readable, scoreboard sorted by score, no mismatch between clients. | Misattributed lines; feed duration; sorting |
| 17 | Death and respawn. | Death title and respawn prompt, then a new life with the default loadout. | HUD returns cleanly. | Leftover screen state |
| 18 | Get shot from behind and from the side. | Red bar on the side of the hit; none for your own rocket-jump splash; hit marker on your hits (red and longer on a kill). | Correct side. | Wrong or missing cases |

## 4. Powerups, Haste, Quad rocket jump (same session)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 19 | **Quad**: pick up the centre Quad once it has spawned, or `velox.items.GivePowerup Quad 30`; shoot with MG and rocket. | HUD entry with **seconds remaining**; about 3x damage; picking it up again extends instead of restarting. | Damage about 3x (`VeloxHitConfirm` lines); time counts down correctly; effect ends cleanly. | Shown vs real seconds; damage numbers |
| 20 | **Quad rocket jump**: Quad on, `velox.combat.GiveAllWeapons`, look down, fire a rocket (flat floor and from a platform). Turn on `velox.hud.Movement 1` and note the corrections. | A strong launch (knockback is capped, expect roughly 2x a normal rocket jump, not 3x); the other client sees the same arc. | No visible snap; at most one small correction around pickup or expiry (known edge). | Apex vs normal rocket jump; correction count |
| 21 | **Powerup tint visibility**: look at the other client's pawn with Quad, Haste, Regen, Battle Suit (`GivePowerup`). | A tint on the body (priority Quad > Suit > Regen > Haste). If no tint is visible, note it (the parameter exists since M3-02b; the tint was only verified in logs). | Report visible or not per powerup, and at 10 m / 30 m. | Visible? Colour? Distance |
| 22 | **Haste feel**: run the 50 m corridor (markers every 5 m) without and with `velox.move.Haste 20`; fire the MG during Haste. | About 1.3x ground speed and 1.3x fire rate. | Speed near 1040 cm/s without strafe-jumping; no rubber band when Haste starts or ends (corrections 0-2). | Times over 50 m; correction count; too fast, too slow, or good |
| 23 | **Regen**: take damage, `velox.items.GivePowerup Regen 20`. | Health rises about 15 per second, slows near 110 %. | Visible rise. | Rate |
| 24 | **Battle Suit**: `velox.items.GivePowerup BattleSuit 20`, then `velox.combat.TestDamage 50 -1 1` (self damage) and take a hit from the other client. | Splash does nothing, direct hits do half. | As described. | Damage numbers |
| 25 | Die with a powerup active. | Powerup gone after respawn (not dropped), no tint, no HUD entry. | Gone. | Leftovers |

## 5. Mouse feel (sensitivity changed)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 26 | **[first look]** M3 changed mouse look from **1 deg per count** to **0.022 deg x sensitivity per count** (default 5 = 0.11 deg per count). Turn 180 deg with your normal hand sweep at sensitivity 3, 5 and 8 (`velox.settings.Set sens 8`). | Controllable turning at one of the values. | One value feels like your usual Quake setting. | Mouse dpi; best sensitivity; FOV 90 vs 105; invert Y works |
| 27 | Same hand sweep slow and fast. | Same angle (no acceleration). | Linear. | Any acceleration or stutter |

## 6. Match flow: warmup -> countdown -> live -> end -> restart (`?game=FFA?FragLimit=3`, 2 clients)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 28 | Start PIE, wait for warmup -> countdown -> Live. | The level **resets** on going Live (items back, scores 0, players respawn at starts). | Same on both clients; clock starts at the full time. | Banner texts; what did not reset |
| 29 | Play to 3 frags (or `velox.feed.Frag 3`). | Match ends: intermission camera, scoreboard forced open with the winner, everyone frozen, no damage. | Correct winner; nobody can move or fire. | Winner line; camera; anything still moving |
| 30 | **[first look]** Intermission: the **cursor must appear**; the scoreboard ready button must be clickable (or F3). | Cursor visible; button toggles Ready. | Click works; all humans ready returns to warmup at once, otherwise after 15 s. | Click vs F3; seconds to restart |
| 31 | After the restart. | Scores 0, items fresh, feed kept, you can move and fire, cursor hidden. | Playable, no stuck frozen state or cursor. | Leftovers |
| 32 | Escape during Live. | Pause menu (Resume, Settings, Disconnect) with cursor; game keeps running; Resume restores control. | Open/close repeatedly; Disconnect returns to `L_MainMenu`. | Does the pawn still react to input while paused |
| 33 | **Menu connect**: with the PIE server running, start a standalone client, Connect to `127.0.0.1:<port from the server log>` (or console `velox.menu.Connect <host[:port]>`). | You appear in the arena with a HUD. Check that the scoreboard shows your name from Settings (fixed in task M3-13; if it shows a machine name, note it). | Join works; address remembered next time. | Name shown; join time |
| 34 | Late join during Live (Number of Players 3, or `velox.match.AddPlayer`). | Joiner gets the phase, clock and the last 8 feed lines, scoreboard complete. | Clock may read "--:--" until synced; nothing else wrong. | Clock/feed glitches |

## 7. Duel with a spectator (3 PIE clients, or two machines)

Number of Players = **3**, Additional Server Game Options = `?game=Duel`. Duel has 2 active players; client 3 becomes a **spectator**. Two-machine variant: machine A runs the PIE server and 2 clients, machine B connects with `velox.menu.Connect <A-ip>:7777` (UDP 7777 open on A) and is the spectator. Write down which setup you used.

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 35 | Duel warmup: both duelists press **F3** (or `velox.match.Ready 1`). | Ready prompt "F3"; the countdown starts only when both are ready; the spectator cannot take a slot mid-match (`velox.match.Join 2` refused). | Countdown only after both ready. | Prompt text; whether F3 label is right |
| 36 | **Spectator item timers**: spectator client after the level reset. | Majors (Mega, Yellow/Red Armor, powerups) with `mm:ss`, soonest first, "UP" when available. Duelists' HUDs show none. | Spectator only; counts down correctly. | Items listed; accuracy |
| 37 | **"Took" messages**: a duelist takes Mega, Red Armor or Quad. | Spectator sees "<name> took <item>" and the timer restarts (35 / 25 / 120 s); the duelist sees their own pickup message only. | One message per take, correct name. | Wrong/missing messages; timer error |
| 38 | Spectator: free fly and follow a player. | Both players visible, smooth, HUD intact. | Usable. | Controls you found |
| 39 | Overtime: `velox.match.Phase Overtime` (or equal score at the time limit). | Overtime banner; next frag wins. | Match ends on the next frag. | Banner text |
| 40 | Forfeit: one duelist uses Escape -> Disconnect. | Match ends, the remaining player wins; the spectator can join in PostMatch. | Winner = remaining player. | Reaction time |

## 8. Performance and logs

| # | What to do | Pass criterion | Write down |
|---|---|---|---|
| 41 | `stat fps` in a fight with HUD, then open the scoreboard and let a pickup respawn. | 60 fps or more, no hitch on scoreboard or respawn. | fps range; hitch moments |
| 42 | Output Log filter `Error`, `Warning`, `Ensure` after the session. | No ensures, no repeating errors. | First 5 distinct messages |

## 9. Feedback template (ratings 1 = bad, 5 = great)

```
M3 game loop playtest (<date>, <name>)
Build: infra/ci/build-mac.sh -> CI OK (Intel Mac, macOS <ver>, Xcode <ver>, UE 5.8.x)
Setup: PIE, dedicated server + <2|3> clients, Run Under One Process on, 100 ms / 2 %, FFA <y/n>, Duel <y/n>, two machines <y/n>
Checklist: pass <n> / fail <n> / skipped <n>; failed rows: <numbers + one line each>

Ratings (1-5)           Rating   One line why
Menus                   [ ]
HUD readability         [ ]
Scoreboard              [ ]
Item feel               [ ]
Powerups                [ ]
Haste                   [ ]
Match flow              [ ]
Mouse look / sens       [ ]   (dpi ___ , best value ___)

Free text:
- Best thing:
- Worst thing / most annoying:
- Confusing or missing info on screen:
- Sounds missing or too quiet/loud:
- Would you play another round, and why:
- Other:
```

Send back: the filled template, screenshots of the HUD, scoreboard and menus, and the newest `Saved/Logs/Velox*.log`.
