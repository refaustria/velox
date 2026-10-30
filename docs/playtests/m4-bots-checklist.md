# Velox M4 bots playtest (human, macOS, Apple Silicon)

Goal (M4 done criterion): a human plays **FFA and Duel against bots online, dedicated server + 2 clients + 4 bots, 100 ms / 2 % loss**, and judges whether the bots move, fight and compete like players. Budget: about 60 minutes after the first build. Spec: `docs/adr/0005-bots.md` (D1-D38). Headless evidence (`infra/ci/nettest.sh --scripts BotRun,BotDuel --repeat 3`, `infra/ci/fts.sh`) says the bots *work*; this playtest is the only check of how they *look and feel*, which no log can show: **readability of bot movement, strafe-jumping, difficulty feel and the HUD/scoreboard details are first looks on a real display**. Write down everything odd, even when it is not a row.

The playtest machine is an **Apple Silicon Mac** (the Intel Mac cannot render UE 5.8). Movement, combat and the game loop were checked in M1-M3 (`m1-movement-checklist.md`, `m2-combat-checklist.md`, `m3-gameloop-checklist.md`); this list re-checks them only where bots touch them.

## 1. Setup

1. Mac setup as in `docs/playtests/m1-movement-checklist.md` section 1 (macOS Sonoma 14.5 or newer, Xcode 26.0 or 26.1.1 and **not** 26.4, UE 5.8 from the Epic Launcher, `brew install git-lfs coreutils`, `git lfs install`); on Apple Silicon the editor and the game build as arm64 (no Rosetta). Check out branch `m4-bots` (or the branch the lead names), then **`git lfs pull`**.
2. From the repo root: `infra/ci/build-mac.sh`. Success ends with `[ci] CI OK. Logs: Saved/CI/<timestamp>`; `VeloxServer`/`VeloxClient` print `SKIPPED` on Mac, expected. On failure attach `Saved/CI/<timestamp>/*.log` and stop.
3. Open `Velox.uproject` (first start: shader compile, 5-10 min). Open map **`L_ItemTest`** (6000 x 4000 cm, layout in `m3-gameloop-checklist.md` section 1 item 5; the bots' nav mesh is built at map load, **expect 1-3 s of hitch at the start**).
4. Play settings (three dots next to Play -> Advanced Settings): **Number of Players = 2**, **Net Mode = Play As Client** (dedicated server), **Run Under One Process = on** (required: the cheats find the server world only in the same process). Network emulation: Enable, target Everyone, Packet Lag **100 ms**, Packet Loss **2 %**; fallback in each client console `Net PktLag=100` then `Net PktLoss=2`.
5. **Additional Server Game Options** (bots are added by URL options, no console needed): FFA with four bots `?game=FFA?Bots=4?BotDifficulty=Medium`, Duel `?game=Duel?Bots=1?BotsFill=1?BotDifficulty=Hard` (with two humans in a Duel the bots only fill free slots; use one client and `velox.match.RemovePlayer` / spectate for the bot-vs-bot rows). Other difficulties: `Easy`, `Medium`, `Hard`, `Expert`.
6. Console (`~`, German keyboard `^`, one per client window): `AddBot [Easy|Medium|Hard|Expert] [Name]` and `KickBot [Name|all]` work from a **client** (forwarded to the server). Debug: `velox.bots.List` (one `VeloxBot` line per bot on the server), `velox.bots.Players` (the PlayerArray as the client sees it), `velox.bots.Debug 1` (server: one line per bot per second and path/aim debug draw), `velox.bots.Watch` (client: logs `VeloxBotWatch` phase/feed/pickup lines), `velox.bots.Freeze 1` (bots stand still), `velox.bots.Goal <bot|all> <Velox.Item.Health.Mega | point x y z | none>`, plus the earlier cheats (`velox.combat.GiveAllWeapons`, `velox.match.Ready 1 all`, `velox.items.List`). Output Log filters: `VeloxBot`, `VeloxKill`, `VeloxMatch`, `VeloxPickup`.
7. Keys as in M3: WASD, Space, left mouse = fire, 2-8 weapons, Tab = scoreboard, F3 = ready, Escape = pause menu (on a Mac keyboard F3 may need Fn).

## 2. Bot movement readability (FFA, 2 clients, 4 Medium bots, 100 ms / 2 %)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 1 | **[first look]** Press Play, ready up (F3), watch the countdown and the first 30 s of Live from a safe corner (or the other client). | Four named bots with distinct colours run to items (Mega/RA platforms via pads, YA ramp, weapons along the south wall). | Bots visibly head for items, not in a pack, none stands still at the spawn. | Bots standing, circling or hugging walls; screenshot |
| 2 | **[first look]** Follow one bot for 30 s from behind/aside at 100 ms. | Smooth runs with turns; hops and air-strafing on long straights; no stop-and-go. | Reads as a player moving, motion looks continuous (not a slide show). | Stutter, sliding on the spot, odd turns |
| 3 | **No visible bot teleports or snaps**: watch 10 bots-minutes in total (several bots, both clients). | None. | Zero visible snaps/pops on **both** clients (bots are server pawns replicated like players). | Time, bot, place, which client saw it |
| 4 | **No bot hopping in place against a corner (D35)**: watch the cross inner corners (near the map centre, |x| and |y| below about 150 cm) and the platform undersides for 3 minutes. | Bots pass corners and obstacles; nobody bunny-hops in place for more than a few seconds. | No dwell loop longer than about 8 s anywhere. | Where, how long, which bot, what it was heading for |
| 5 | **Strafe-jumping in the corridor**: stand at the end of the 50 m north-wall corridor (y = 1775, x = -2400 ... ) and watch a bot run it, or send one: `velox.bots.Goal all point -2400 1775 100`, then `none`. | Bot gathers speed with hops and air control along the corridor and arrives faster than a plain run (it reaches about 1.25x ground speed, ~1000 cm/s). | Visibly faster than walking; no jitter. | Time over 50 m vs your own run; stumbles |
| 6 | **Jump pad use**: watch bots take Mega and Red Armor (platforms with pads). | A bot steps on the pad, flies up, lands on the platform, takes the item. | Reliable (several successes, no pad missed repeatedly). | Failures, bot falling off, wrong pad |
| 7 | **Drop links**: a bot on a platform goes down again. | It walks off the edge deliberately (no hesitation or getting stuck). | No stuck at platform edges. | Place and time |
| 8 | **Rocket jumps (Hard)**: `AddBot Hard` and send it to the third platform: `velox.bots.Goal <name> Velox.Item.Ammo.Rockets` (the SE platform has no pad). | The Hard bot fires a rocket at its feet while jumping and flies onto the platform; it takes damage from it. | It lands on the platform in at most 3 tries; the arc looks like your own rocket jump. | Tries, falls, health left |
| 9 | **Bot vs bot flow**: run a full 5 minutes of FFA. | Frags happen among bots and against you, bots pick up weapons they lack, switch weapons by distance (rocket/plasma in the open, shotgun/lightning close, rail far). | Bots use more than the machine gun; fights look like fights (strafing, retreating when hurt). | Weapon misuse (e.g. lightning at 30 m), passive bots |

## 3. Combat feel and difficulty (FFA, you against the bots)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 10 | **[first look]** Play 3 minutes against 3 **Easy** bots (`?Bots=3?BotDifficulty=Easy`). | Slow turning, poor aim, short reaction, rarely dodges. | Easy is beatable by a casual player and clearly weaker than Medium. | Frags/deaths; feels easy enough? |
| 11 | 3 minutes against 3 **Medium** bots. | Competent: collects items, aims with error, dodges some rockets, retreats when hurt. | A fair fight for a player who knows the map. | Frags/deaths; feels fair? |
| 12 | 3 minutes against 3 **Hard** bots. | Fast aim, good dodging, rocket jumps, takes Mega/RA reliably, camps item respawns in Duel. | Hard wins more often than you do, but is not a hitscan aimbot. | Frags/deaths; unfair situations (instant headshots through walls, perfect rail) |
| 13 | 3 minutes against 2 **Expert** bots. | Strongest preset. | Expert is clearly above Hard; beatable only with good play. | Frags/deaths; feels fair or aimbot-like? |
| 14 | Order the four presets by difficulty after rows 10-13. | Easy < Medium < Hard < Expert. | The order matches your impression; the steps feel distinct. | Any pair that feels the same |
| 15 | **Reaction to your fire and rockets**: shoot at a bot from behind; fire a rocket at one in the open; stay hidden and shoot. | It turns toward the shooter (hearing, Medium reaction time ~0.4 s), jumps or strafes away from rockets (Hard/Expert). | Plausible reactions, not instantaneous. | Too fast, too blind |
| 16 | **Bots hurt you on the client at 100 ms**: take rocket and plasma hits. | Damage and knockback feel the same as from a human; no snaps. | Same hit/knockback behaviour as from a human (M2). | Any difference |

## 4. Duel timing (`?game=Duel?Bots=2?BotsFill=1?BotDifficulty=Hard`, spectate or play)

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 17 | **[first look]** Join as a spectator (`velox.match.RemovePlayer` or a second client that only watches) and watch a 3 minute Duel between two Hard bots. | Countdown -> Live without a human readying; the bots fight over Mega/RA with timing. | The match starts and ends cleanly; both bots fight, none idles. | Phase glitches; idle bot |
| 18 | **Mega / Red Armor timing**: note the Mega respawn (35 s, item timer on HUD) and when a bot arrives. | A bot is at the Mega pad within a few seconds of the respawn (waits, then takes). | At least one take within about 3 s of the respawn per two cycles. | Seconds late/early per cycle |
| 19 | **Camping**: watch a bot around a respawn. | A Hard bot may wait near the item before it respawns. | Visible waiting is fine; being stuck is not. | Where it waits and for how long |
| 20 | Play the Duel yourself (1 human + 1 Hard bot). | Fast duel, bot contests items and uses the pad/ramp routes. | A real contest for Mega/RA. | Who controls the items; fun? |

## 5. Bot management and HUD

| # | What to do | What you should see | Pass criterion | Write down |
|---|---|---|---|---|
| 21 | **`AddBot` from a client**: in a client console `AddBot Hard Reaper`. | A bot named Reaper joins within a second (spawn effect, appears on the scoreboard, feed or join message if any). | Works from either client; the name and difficulty are right (`velox.bots.List`). | Delay; wrong name/difficulty |
| 22 | **`KickBot` from a client**: `KickBot Reaper`, then `KickBot all`. | Reaper leaves; `all` removes every bot; the scoreboard updates on both clients. | Gone immediately, no ghost row, no error in the log. | Leftovers |
| 23 | **Scoreboard `BOT` marker**: hold Tab with bots in the match. | Bots are listed with names, score/kills/deaths, **ping shown as `BOT`** (not a number). | Humans show ping, bots show `BOT`; sorted by score. | Bot ping value, bot row colour |
| 24 | **Kill feed names**: have a bot kill you and kill a bot. | Feed lines with the real bot names (not `Player 3`/`None`), weapon icon/name, suicide line for a bot's self-kill. | Names and weapons correct on **both** clients. | Misattributed lines |
| 25 | **Bot colours/names**: look at the bots in play. | Distinct colours (a colour index per bot, same for both clients), a name label or nameplate if the game has one. | Each bot individually recognisable. | Two bots with the same colour; unreadable names |
| 26 | **Bots vs full server**: fill the server (humans + bots at the player limit) and join a third client. | Joining a full FFA makes the new client a spectator or kicks a bot; no crash. | Clean behaviour, message in the log. | What happened |
| 27 | **Hitching**: play 5 minutes with 4 bots and `stat fps` on one client. | No periodic hitches; the frame rate matches the same map without bots (greybox: high). | No hitch longer than 50 ms at 100 ms / 2 %; frame time within about 1 ms of botless. | FPS with/without bots; hitch interval |
| 28 | **Reconnect with bots**: a client disconnects (`velox.menu.Disconnect`) and reconnects. | The bots keep playing; the scoreboard is correct after the return. | Bots unaffected, no duplicate rows. | Anything odd |

## 6. Sign-off

Tester: ______________  Date: ____________  Mac (model / chip / macOS / Xcode): ______________________________

Branch and commit built: ______________________  `infra/ci/build-mac.sh` result: ______________

- [ ] Bots move readably (rows 1-4, 6, 7): no snaps, no hopping in place, no stuck at pads/edges.
- [ ] Strafe-jumping in the corridor and rocket jumps (rows 5, 8) look like player movement.
- [ ] Difficulty steps Easy < Medium < Hard < Expert feel distinct and fair (rows 10-14).
- [ ] Duel timing and camping (rows 17-20) behave like a duel.
- [ ] `AddBot`/`KickBot`, scoreboard `BOT` marker, kill feed names and colours (rows 21-25) are right.
- [ ] No hitches, no crashes (rows 26-28).

Rows failed / odd observations (row number, what, how often): _____________________________________________

Verdict: **PASS / PASS WITH NOTES / FAIL**  (the lead ticks the ROADMAP M4 "bots usable in multiplayer matches" box on PASS or PASS WITH NOTES)
