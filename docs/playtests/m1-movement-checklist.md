# Velox M1 movement playtest (human, macOS)

Goal (M1 done criterion): a human strafe-jumps and bunny-hops across the test map **online, dedicated server + 2 clients, 100 ms / 2 % loss, no visible correction jitter**. Budget: about 1 hour after the first build. Spec: `docs/adr/0002-movement-component-design.md`; tooling: `docs/movement-tooling.md`.

## 1. Mac prerequisites and first build

1. Xcode (App Store) installed and opened once (accept the licence: `sudo xcodebuild -license accept`); `xcode-select --install` if the command line tools are missing.
2. Epic Games Launcher -> Unreal Engine -> Library -> install **UE 5.8** (about 40 GB, default path `/Users/Shared/Epic Games/UE_5.8`). Do not build the engine from source.
3. Git LFS: `brew install git-lfs && git lfs install`. Clone the repo, check out branch `m1-movement`, then `git lfs pull` (maps and data assets `*.umap`/`*.uasset` are LFS files; without this the editor reports corrupt assets).
4. The CI script needs a `timeout` command, which macOS lacks (the script falls back to `gtimeout` if present): `brew install coreutils`.
5. From the repo root: `infra/ci/build-mac.sh`. If UE is elsewhere: `UE_ROOT="/path/to/UE_5.8" infra/ci/build-mac.sh`. Builds `VeloxEditor` and `Velox` (Game), then runs the headless `Velox.*` automation tests. `VeloxServer`/`VeloxClient` print `SKIPPED` on Mac (expected, ADR 0001 section 5). Duration: first run 20-60 min (compile), later runs a few minutes; the Launcher engine needs no engine rebuild. Success ends with `[ci] CI OK. Logs: Saved/CI/<timestamp>`. On failure attach `Saved/CI/<timestamp>/*.log` and stop.
6. Open the project: double-click `Velox.uproject` (first start: shader compile, 5-10 min, do not interrupt). If it asks "Missing modules / rebuild?" answer **Yes** (it uses the same toolchain); if the rebuild fails, run step 5 again and report the log. The editor starts on `L_MovementTest` (configured startup map).

## 2. PIE setup (dedicated server + 2 clients)

1. Toolbar: the **three dots next to the Play button -> Advanced Settings** (Editor Preferences -> Level Editor -> Play).
2. Multiplayer Options: **Number of Players = 2**; **Net Mode = Play As Client** (this starts a dedicated server in the background); **Run Under One Process = on** (required: `velox.move.TestKnockback` finds the server world only inside the same process; velox cvars are then shared by server and clients, which is what we want).
3. Same panel -> **Server / Client Network Emulation** (name varies slightly): Enable Network Emulation, target **Everyone**, Custom: Packet Lag 100 ms, Packet Loss 2 %, both directions. Fallback if the panel is missing: type in **each client window's console** `Net PktLag=100` then `Net PktLoss=2` (outgoing only; ping on the HUD then reads about 100 instead of about 200 ms, that is fine, write down what you see).
4. Server tick is 60 Hz (`NetServerMaxTickRate=60` in `Config/DefaultEngine.ini`), nothing to set. Lower the editor viewport load: close other apps; PIE should run at 60+ fps (shown on the HUD).
5. Press **Play**. Two windows open (Client 1, Client 2); click into one to give it input focus. Mouse = look, **WASD** = move, **Space** = jump (hold it for auto-hop). **Esc** (or Shift+F1) releases the mouse. Console: **`~`** (German keyboard: the `^` key left of `1`); Enter runs, the console exists per client window.
6. The movement HUD (top-left, yellow) is on by default. Toggle: `velox.hud.Movement 0|1`. Lines: `speed XY <cm/s> (<ups>)`, GROUND/AIR, `grace/latch/jumpheld`, `corrections N max X cm last Y cm`, `fps/ping`. Both clients spawn at one of two PlayerStarts (corridor start x=120,y=0 or pads start x=-300,y=-2500); you walk between sections on the big floor (X -600..6000, Y -4200..7000, about 13 s end to end). If you fall off, stop PIE and restart.
7. Start of every session: in each client console run `velox.move.Preset CPM`, then `velox.move.DumpSettings` (writes to Output Log; look for `bAutoHop` = 1 or true). Default asset values are the CPM-style tuning; section 3.9 compares with `Q3`.

Map orientation: the corridor runs along +X (white/orange floor strips every 512 cm, text in Q3 units and metres); ramp lanes at Y = 1000/1700/2400/3100/3800 (10/20/30/45/50 deg, blue = walkable, red = not), stairs lanes at Y = 4700/5300/5900 (yellow, risers 20/40/60 cm), pads A/B/C at Y = -1500/-2500/-3500 (green discs at X=500, cyan target markers), gap pairs at Y = 7600/8400/9200 (pink start platform, grey landing platform).

## 3. Checklist

Rule for all rows: look at the *other* client's window now and then; it must show your pawn moving smoothly. "Snap" means a visible teleport of your own view (a rubber-band) at the 100 ms / 2 % setting.

| # | What to do | What you should see | Pass criterion | Your observation |
|---|---|---|---|---|
| 1 | Spawn in the corridor (or walk to x=120,y=0). Hold W, hold Space, alternate A+W / D+W while turning the mouse smoothly in the strafe direction (turn *into* the direction you hold). Run to the far end (x=5120). | Speed climbs hop by hop; each landing keeps speed (no loss). | HUD `speed XY` >= **1120 cm/s (1.4 x 800)** before marker 4096 (marker 8). Up to ~1500 is plausible. Peak: ____ cm/s at marker ____ | |
| 2 | Same, but WASD without strafing or turning (just W+Space). | Speed stays near 800, no meaningful gain. | No acceleration beyond 800 cm/s without strafe+turn. | |
| 3 | Gaps (Y = 7600, 8400, 9200; walk in from the south, drop into the pit, jump up onto the pink platform, run to its +X edge). Hold Space the whole time (auto-hop) and jump at the edge. | Landing on the grey platform. 500 cm gap needs >= ~740 cm/s, 700 cm needs >= ~1040 cm/s, 900 cm needs >= ~1330 cm/s at take-off (jump airtime 0.675 s, flat landing). | **500 gap cleared.** 700 and 900: note which you clear and from which speed (platform run-up is only 800 cm, so build speed first by strafe-jumping in the pit and hopping up onto the platform). Cleared: 500 / 700 / 900 : ____ | |
| 4 | Pad A (X=500, Y=-1500): walk onto the green disc. | Launch forward and up, you land **on the platform** (X 1100..1500, top 320 cm high) and stay on it. | Lands on `JumpPadA_Platform`. | |
| 5 | Pad B (Y=-2500). | Flat arc, lands on the cyan marker at X=2100 (1600 cm away). | Lands within about 2 m of the marker. | |
| 6 | Pad C (Y=-3500). | Straight up (apex about 680 cm), then drift +X and land **on the ledge** (X 700..1100, 640 cm high); hold D/W lightly on the way down. | Lands on `JumpPadC_Ledge`. Pad launches repeatable 3 times each. | |
| 7 | Ramps: run up the 30 deg lane (Y=2400) from x=-500 at full speed, then the 45 deg lane (Y=3100); 50 deg (red) is not walkable and must block. | Speed along the ramp stays (HUD XY speed does not drop when entering). At the ramp top you are **launched upward** over the platform (flight, then landing), not stuck. | XY speed on the ramp >= 95 % of speed at entry; upward launch at the exit on 30 and 45 deg; 50 deg lane cannot be climbed. | |
| 8 | Stairs: run at full speed up the 20 cm (Y=4700) and 40 cm (Y=5300) flights, then the 60 cm flight (Y=5900). | 20/40: smooth, no stutter; 60: you are stopped by the first step. | 20 and 40 cm: HUD speed stays >= 98 % over 12 steps. 60 cm blocks. | |
| 9 | Rocket jump: stand still on flat floor, run `velox.move.TestKnockback 0 0 1200 1` in your console (it must be a client window). Look at the other client during it. | You are thrown straight up (about 360 cm apex: 1200^2 / (2 x 2000)), the other client shows the same arc. Then try `velox.move.TestKnockback 800 0 800 0` while running (adds to velocity). | Apex height ____ cm above the start (read the highest `Z` from the HUD, which shows vertical velocity; or the recorder CSV column `z`); other client sees the same; no snap on your side beyond the expected echoes (row 12). | |
| 10 | Wall-hug: in the corridor run at 45 deg into the left wall holding W+A, no Space (ground). | You slide along the wall. HUD speed **above 800 (about 900)** is expected and correct (Q3 behaviour, ADR A23). | HUD reads > 800, no jitter. Not a failure. | |
| 11 | Presets: `velox.move.Preset Q3`, repeat row 1 (jump is now a *press per hop*, release Space between hops: no auto-hop). Then `velox.move.Preset CPM` and repeat. Return to default with `velox.move.ClearOverrides`. | Q3: slower build-up, need to tap Space at landing. CPM: holding Space bunny-hops, stronger air control. | Both presets playable, no snaps. Peak Q3: ____ CPM: ____ cm/s. Which feels better? | |
| 12 | Jitter criterion, throughout rows 1-11 at 100 ms / 2 %. Check the HUD `corrections` line after each row. | Movement feels as on a LAN: no rubber-band, no teleport. | HUD `corrections` stays **0** (or +1-2 isolated at most, last <= 5 cm) except right after `TestKnockback` where a burst of 0-cm "echo" corrections (up to ~5) is expected and invisible. Any correction > 5 cm or visible snap = fail, note the row and `last` value. Final counter ____ | |

Run rows 1, 4-9 on **both** clients (one after the other) and keep the other client's window visible while you play.

## 4. Recording a run and reporting

- Record: console `velox.move.Record Saved/Runs/m1-strafe-client1.csv`; do the run; `velox.move.Record` (no argument) stops. One CSV per row you want to document (path relative to the project folder).
- Analyse (optional): `python3 Tools/analyze_run.py Saved/Runs/m1-strafe-client1.csv --all --min-peak-factor 1.4`.
- Send back: the CSVs, the newest `Saved/Logs/Velox*.log` (lines `VeloxCorrection` and `LogVeloxMovement`), the screenshot of the HUD at your peak speed, and the table above filled in.
- Feel feedback, use the Q3 numbers (HUD shows ups = cm/s / 2.5, `UnitScale 2.5`; Q3 walk speed 320 ups = 800 cm/s, jump 270 ups = 675 cm/s, gravity 800 ups^2 = 2000 cm/s^2). Format: `<topic> | too low / right / too high | what you expected in ups`. Topics: top speed, acceleration on the ground (`GroundAcceleration` 10), air acceleration / strafe gain (`AirAcceleration` 1, `AirStrafeAcceleration` 70 in CPM), air control, jump height (about 114 cm), gravity, friction/stopping. Try changes live without rebuilding: `velox.move.GroundAcceleration 12`, `velox.move.JumpVelocity 700` (then `velox.move.Preset <name>` to re-apply engine-side fields), list all fields with `velox.move.DumpSettings`. Send the final values, not "a bit more".

## 5. Sign-off (paste into `STATUS.md`)

```
M1-06 human playtest (<date>, <name>)
Build: infra/ci/build-mac.sh -> CI OK (Mac, UE 5.8.x, <Apple Silicon|Intel>)
Setup: PIE, dedicated server + 2 clients, Run Under One Process on, 100 ms / 2 % (<both ways|outgoing only>), ping on HUD <n> ms
Corridor peak: <n> cm/s (<x.xx> x)    Gaps cleared: 500 <y/n>, 700 <y/n>, 900 <y/n>
Pads A/B/C: <y/n> <y/n> <y/n>    Ramps 30/45: <y/n>    Stairs 20/40 pass, 60 blocks: <y/n>
TestKnockback apex: <n> cm, seen by other client: <y/n>    Wall-hug HUD: <n> cm/s
Presets Q3/CPM: peak <n>/<n> cm/s, preference <Q3|CPM>
Visible snapping at 100 ms / 2 %: <none|describe>    HUD corrections: <n>, max <n> cm
Feel notes: <topic | verdict | target in ups>
Verdict: M1 done criterion <MET | NOT MET: reasons>
```
