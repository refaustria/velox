# Velox M6 online playtest (human; public server `Velox FFA | games.inceptum.at`)

Goal (M6 done criterion, ADR 0007): a human on the **Apple Silicon Mac** downloads the client from `https://games.inceptum.at/`, finds the public server in the in-game browser, joins it over the real internet, plays an FFA against the 3 bots (and a second human if one is available), leaves cleanly and rejoins by IP. Budget: about 45 minutes. Headless evidence (packaged gate P1-P3, hardening probes, replication budget) says the server and the protocol are *correct*; this is the only check of how it *feels* at real latency and whether the download path works for a stranger. Write down everything odd, even when it is not a row.

Items marked **[Mac only]** need the Apple Silicon Mac (the VM has no GPU; the Intel Mac cannot render UE 5.8). Items marked **[lead]** are run by the lead on the VM and need no human. The Mac client package (task 11) delivers the macOS zip; until it exists the download rows are `blocked: task 11`.

## 0. Prerequisites

- **[lead]** Release deployed: `release.sh <ver> <LinuxServer archive>` ran, `systemctl status velox-server@ffa` is active, `publish-download.sh` ran, landing page current. `curl -s https://games.inceptum.at/status/servers.json` lists the server with `players` 0 and `version` equal to `latest.json`'s.
- **[Mac only]** Apple Silicon Mac as in STATUS q26 (macOS Sonoma 14.5 or newer). Xcode is needed only to *build* the package (task 11), not to play.
- The client **version must equal the server's** (`0.6.1` for this release). A mismatch is refused at the handshake (row 7).
- Network: note your connection type (home Wi-Fi / cable / VPN). Packet loss on Wi-Fi is real; write it down.

## 1. Download **[Mac only]**

| # | What to do | Expected | Write down |
|---|---|---|---|
| 1 | Open `https://games.inceptum.at/` in Safari. | Page loads over HTTPS (valid certificate), shows the current version, a macOS download button and the server status row. | Certificate warning, layout break, version shown |
| 2 | Download `Velox-<ver>-macOS.zip`; compare `shasum -a 256` with `SHA256SUMS` on the page. | Hash identical. | Size, speed, mismatch |
| 3 | Unzip, **double-click** `Velox.app` once. Then follow the page's Gatekeeper note (q47 (a), ad-hoc signed): right-click -> Open, or `xattr -d com.apple.quarantine Velox.app`. | First double-click is blocked ("unidentified developer"), the documented route opens it. | Exact dialog text, steps needed |
| 4 | The game starts windowed and reaches the main menu without a crash or error box. | Main menu with Servers, Play, Settings. | Time to menu, FPS in menu |

## 2. Connect through the browser **[Mac only]**

| # | What to do | Expected | Write down |
|---|---|---|---|
| 5 | Main menu -> **Servers**. | List loads within a few seconds; one row `Velox FFA | games.inceptum.at`, map **Praesidium**, FFA, players `n/16` (the 3 bots are `bots`, not players), version `0.6.1`, state Ok. | Load time, row text, anything greyed |
| 6 | Select the row, **Connect**. | "Connecting", the map loads, warm-up with the 3 bots, your pawn spawns. | Time from click to spawn, hitches while loading |
| 7 | (Optional, only if an old client is around) connect with a client of another version. | Row greyed or a clear "version mismatch" refusal; no crash. | Message text |
| 8 | Refresh the list while you are in the match. | `players` counts you within about 30 s; agrees with `servers.json`. | Lag of the count |

## 3. Play (5 minutes minimum, FFA, 3 Medium bots)

Movement and combat were accepted at a *simulated* 100 ms / 2 % (`m1-movement-checklist.md`, `m2-combat-checklist.md`). This is the first time at real internet latency. Open the scoreboard (Tab) and **note your ping before and after each row**.

| # | What to do | Expected | Write down |
|---|---|---|---|
| 9 | Strafe-jump and bunny-hop along the North Lane (28 m) and the South Hall, both directions. | Speed builds as in the PIE test; no rubber-banding. | Ping; any visible snap-back, where |
| 10 | Take a jump pad (3 times) and do a rocket jump from a mid bridge. | Arc and landing as in the PIE test. | Ping; correction jitter at takeoff or landing |
| 11 | Fire every weapon you get (Machine Gun, Shotgun, Rocket, Plasma, Lightning, Railgun) at a bot at 10-30 m. | Hits register where the crosshair is; Railgun and Lightning hit what you see (lag compensation); hit marker consistent with the scoreboard. | Ping; "shots that should have hit" per weapon |
| 12 | Get killed by a bot. | Death camera follows the killer; kill feed names both; respawn after the delay. | Camera behaviour, delay |
| 13 | Scoreboard (Tab). | All names, frags, deaths, ping column. | Columns off, wrong values |
| 14 | Let a match end (10-minute limit; or skip). | Intermission shows the winner; the next match starts. | Hang, wrong winner, stuck state |
| 15 | `stat fps` in the console (`^` on a German keyboard). | 60 FPS or more windowed. | Value and window size |

## 4. Leave, reconnect, edge cases **[Mac only]**

| # | What to do | Expected | Write down |
|---|---|---|---|
| 16 | Escape -> Disconnect (or `disconnect` in the console). | Main menu, **no error dialog**. | Dialog text if any |
| 17 | Wait 30 s, open Servers. | `players` back to 0 on the row. | Delay |
| 18 | Direct-IP field: `games.inceptum.at:7777` (numeric IP if the name is refused), Connect. | Joins like row 6. | Hostname accepted? Time |
| 19 | Password box: enter `wrong`, join. The public server is open (q45): expected result is a **normal join**. If the lead set a password, expect a clear "wrong password" message and no crash. | As stated. | What happened |
| 20 | Force Quit the game while connected, reconnect after 10 s. | Server frees the slot within 60 s; reconnect works. | Time until the slot is free |
| 21 | Settings -> name: paste 80 characters, join. | Name truncated or refused; no crash, scoreboard stays aligned. | Behaviour |

## 5. Second human (another Mac, or a friend with the Linux archive; Linux client is labelled headless-tested, q48)

| # | What to do | Expected | Write down |
|---|---|---|---|
| 22 | Both join. | Both on each other's scoreboards; `servers.json` `players 2`. | |
| 23 | Duel for 3 minutes, shoot, rocket-jump. | The other pawn moves smoothly (no teleporting), hits register both ways, kill feed correct. | Both pings; jitter seen on the other pawn |
| 24 | One leaves, one stays. | No hitch for the stayer; `players 1`. | |

## 6. Public status

| # | What to do | Expected | Write down |
|---|---|---|---|
| 25 | **[Mac only]** While in the match, refresh the landing page. | Status row: server online, `players` equal to the humans connected, map Praesidium. | Delay of the count |
| 26 | **[Mac only]** After leaving, wait 2 minutes, refresh. | `players 0`, still online (not stale). | |

## 7. Lead's section **[lead]** (no human needed; before the session and again after)

1. `curl -s https://games.inceptum.at/status/servers.json | python3 -m json.tool` shows the instance with the manifest's `version`. (`infra/ci/release-smoke.sh --public` is the planned automation, see task 13 notes.)
2. `sudo journalctl -u velox-server@ffa --since "1 hour ago" | grep -a -i -E 'error|ensure|fatal'` returns nothing beyond known engine noise.
3. `sudo nft list ruleset` is exactly the allow-list of ADR 0007 section 3 (22, 80, 443, 7777/udp, ICMP, established; 8765 and 3000 closed). `ss -ltnup | grep -E '7777|8790'` shows only the server (UDP 7777) and `veloxmaster` on 127.0.0.1:8790.
4. After the human leaves: the journal shows the human's login and logout, and the `anomaly` summary lines show no rejects or time clamps beyond the headless baseline (thresholds are 0 = off, so read the counters and attach them).
5. `systemctl status veloxmaster velox-server@ffa` active; record `MemoryCurrent` of the server unit (about 1.8 GB expected, ADR 0007 F2).

## 8. What to report

Ping (min / typical / max from the scoreboard), connection type, any correction jitter (row, place), FPS, the exact text of every dialog or error, hit-registration complaints (weapon, distance, ping), and the time of day (UTC, so the lead can match the server log). Screenshots: the Servers screen, the scoreboard with ping, any error.

## 9. Sign-off

Tester: ______________  Date/time (UTC): ____________  Mac (model / chip / macOS): ______________________________

Release version (client and server): ______________  Connection type: ______________

- [ ] Download and Gatekeeper (rows 1-4): PASS / FAIL / blocked (task 11)
- [ ] Browser lists the public server and Connect joins (rows 5-8): PASS / FAIL
- [ ] 5 minutes of play, feel and hit registration at real latency (rows 9-15): PASS / PASS WITH NOTES / FAIL, ping ____ ms
- [ ] Leave, reconnect, direct IP, kill-and-rejoin (rows 16-21): PASS / FAIL
- [ ] Second human (rows 22-24): PASS / SKIPPED
- [ ] Landing page status (rows 25-26): PASS / FAIL
- [ ] Lead's section 7: PASS / FAIL

Findings (row number, what, how often): _____________________________________________

Verdict: **PASS / PASS WITH NOTES / FAIL**  (the lead ticks the ROADMAP M6 box on PASS or PASS WITH NOTES)
