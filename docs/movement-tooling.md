# Movement tooling (M1 task 05)

Debug HUD, tuning cvars, CSV recorder, scripted input and the headless movement tests. Spec: ADR 0002 section 5 (instrumentation), section 6 (tooling), section 7 (tests). All of it is compiled out of Shipping builds except the (inert) scripted-input component class.

## HUD and showdebug

- `velox.hud.Movement 0|1` (default 1 in non-shipping): top-left block on `AVeloxHUD` with XY speed in cm/s and ups, Z velocity, GROUND/AIR, knockback grace, jump latch and jump-held, correction count / max / last (with age), fps and ping.
- `showdebug VeloxMovement`: the same block plus mode, pending knockback, effective parameters and substeps per move (`AVeloxCharacter::DisplayDebug` -> `UVeloxMovementComponent::DisplayVeloxDebug`; the engine only forwards to the CMC for `showdebug Physics`).

## Tuning cvars

`velox.move.<Field>` exists for every field of `UVeloxMovementSettings` (`MaxGroundSpeed`, `GroundAcceleration`, ... `bAutoHop`, `bCombineAirMoves`). Value `-1e30` (the default) means "use the asset"; bools are 0/1. Overrides are applied in `UVeloxMovementSettings::BuildSimParams()` every move, so they take effect immediately in the sim. Engine-side fields (`MaxWalkSpeed`, `MaxStepHeight`, ...) follow on `SetMovementSettings`; `velox.move.Preset` and `velox.move.ClearOverrides` re-apply it, after setting single cvars call `velox.move.Preset` or restart the pawn if you changed `StepHeight` / `WalkableFloorAngle` / `EdgePerchRadius` / `JumpVelocity`.

Cvars are process-global: in PIE server and clients share them. On a real dedicated server set them on both ends (for example `-ExecCmds="velox.move.Preset Q3"` on server and client), otherwise the difference shows up as corrections.

| Command | Effect |
|---|---|
| `velox.move.DumpSettings` | Logs asset value, effective value and an override marker per field (to the log, category LogVeloxMovement). |
| `velox.move.Preset Q3` | Clears overrides, then VQ3: Q3 numbers x UnitScale, `pm_accelerate 10`, no CPM air rules, **`bAutoHop` off** (jump only after a release, the PMF_JUMP_HELD latch). |
| `velox.move.Preset CPM` | CPM: `pm_accelerate 15`, air stop 2.5, strafe accel 70 / wish cap 30 u, air control 150 / k 32 u, `bAutoHop` on. |
| `velox.move.ClearOverrides` | Back to the asset. |
| `velox.move.Probe X0 Y0 Z0 X1 Y1 Z1` | Sweeps the local pawn's capsule on the pawn channel and logs every hit (found the map defects, now fixed). |
| `velox.move.TestKnockback X Y Z [override]` | (task 04) Server-side knockback on the local pawn. |

## CSV recorder

`velox.move.Record Saved/Runs/strafe.csv` starts recording the local pawn once per frame (path relative to the project dir, or absolute); `velox.move.Record` without argument or `velox.move.StopRecord` stops it. A script run stops it automatically. The file is flushed every 120 rows, so a killed process loses at most one second.

Columns: `time` (s since the first sample), `st` (script time, -1 outside a script), `x,y,z`, `vx,vy,vz`, `speed2d` (cm/s), `speed_ups`, `onground`, `grace`, `latch`, `corrections` (cumulative), `corr_last_cm`, `corr_max_cm`.

`Tools/analyze_run.py <csv> [--ground-speed 800] [--markers 512] [--min-peak-factor 1.4] [--max-corrections N] [--max-correction-cm X] [--mean-correction-cm Y]` prints samples, peak speed and its factor over ground speed, the speed at the first crossing of every distance marker (corridor markers are every 512 cm), correction statistics and a PASS/FAIL line per given threshold; exit status 1 on any failure. Only script samples are analysed unless `--all`.

## Scripted input

`UVeloxScriptedInputComponent` (a default subobject of `AVeloxCharacter`, non-shipping) replays a JSON script through `AddMovementInput`, `SetLocalMoveInput`, `SetJumpHeld`/`Jump`, and the controller yaw: the same entry points as `AVeloxCharacter::Input_*`. Scripts live in `Content/Data/MoveScripts/`:

| Script | Start | What it does |
|---|---|---|
| `StrafeRun` | corridor (40, 0, 100) | 0.3 s run-up, then holds jump (auto-hop) and alternates forward+right / forward+left strafes with the view turning 33 deg/s. Four full hops fit in the 5120 cm corridor. |
| `PadRun` | `PlayerStart_Pads` (-300, -2500, 100) | Walks onto pad B, lands on its marker; walks back, pad A, lands on `JumpPadA_Platform`; drops off, pad C, lands on `JumpPadC_Ledge`. Dead-reckoned legs (hold forward, coast to a stop), a checkpoint after every landing. |
| `KnockbackRun` | corridor | Stands 1 s, `serverKnockback [1250, 0, 600]`, waits 1.5 s. |
| `LatchRun` | corridor | Run with `velox.move.Preset Q3`: take-off, release, press again mid-air and hold, mid-air `serverKnockback [0, 0, 300]`, through two landings (first jumps, second does not). |
| `CrouchRun` | corridor | (M2 task 02) Stands, crouches on the ground (checks crouched), crouch-walks, uncrouches, jumps and presses crouch in the air (check: refused), lands. `VeloxCrouch: start`/`end` log lines show half height 70 <-> 50 and eye 55 <-> 40 on every machine. |

Format: `{ "name", "startYaw", "startLocation": [x,y,z], "rows": [ { "duration", "forward", "right", "yawRate", "jump", "crouch", "yaw", "until": "airborne"|"ground", "serverKnockback": [x,y,z], "override", "label", "check": { "name", "box": [minX,minY,maxX,maxY], "onGround", "crouched" } } ] }`. Rows are applied by script time (deterministic for a given dt); `startLocation` teleports the pawn there on the server (on a client over the test RPC `ServerTeleportTestPawn`) and the script waits until it stands on the ground for 0.5 s. `until` ends a row early when the pawn becomes airborne/grounded (used to align with a pad launch). `serverKnockback` calls `AddKnockback` on the authority directly or over the test RPC `ServerApplyTestKnockback` (validated `|impulse| <= 20 x MaxGroundSpeed`, rejected in shipping). A `check` is evaluated when its row ends.

Running:

```
UnrealEditor-Cmd /abs/path/Velox.uproject L_MovementTest -game -nullrhi -nosound -unattended -UseFixedTimeStep -FPS=120 -log \
  -ExecCmds="velox.move.RunScript StrafeRun quit, velox.move.Record Saved/Runs/strafe.csv"
python3 Tools/analyze_run.py Saved/Runs/strafe.csv --min-peak-factor 1.4
```

`velox.move.RunScript <name|path> [quit]` waits for a locally controlled Velox pawn standing on the ground, teleports to `startLocation`, runs the script and logs `VeloxScript:` progress lines, `VeloxCorrection t=.. cm=..` for every client correction and a final `VeloxNetSoak: script=.. moves=.. corrections=.. max=.. mean=.. checks=ok/total result=PASS|FAIL reason=..` line. With `quit` it exits with code 0/1. `-VeloxScript=<name>` on the command line does the same and always quits. The net-soak window rules (task 05 second half) are layered on top of this line by `infra/ci/nettest.sh`. Notes: when the component ticks itself, the input of frame N is moved in frame N+1 (constant one-frame latency; a prerequisite on the movement component would be a tick cycle). Pass the `.uproject` as an absolute path: `UnrealEditor-Cmd` resolves a relative one against the engine directory.

## Automated tests (`Automation RunTests Velox`)

`Tests/VeloxMovementFunctionalTest.cpp` builds a private game world per test (engine cubes with the dimensions of `Tools/editor/create_movement_test_map.py`: floor, corridor, stairs, ramps), spawns a real `AVeloxCharacter` under an AI controller (locally controlled, authority) and steps the world at a fixed 1/120 s, driving the script with `StepScript` before every world tick:

- `Velox.Functional.FT_MovementDeterminism`: `StrafeRun` twice from fresh worlds, every tick bit-identical; reports the peak speed factor.
- `Velox.Movement.Scripted.Stairs` (A13), `.WallSlide` (A13), `.RampExit` (A9), `.JumpLatch` (A12, VQ3 vs auto-hop on `LatchRun`), `.Knockback` (`KnockbackRun`).

Why not `L_MovementTest` / PIE: during automation runs the map is already loaded as the editor world, PIE ticks in real time (not deterministic), and the map's ramps are currently built rolled instead of pitched (see below). Test-world ticking needs `++GFrameCounter` per tick (the tick manager runs each tick function once per engine frame).

## Net soak (`infra/ci/nettest.sh`, `build.sh --net-soak`)

Dedicated server plus one scripted client with 100 ms / 2 % loss each way (see `infra/ci/README.md`). The client logs, besides `VeloxNetSoak: script=.. peak2d=.. peak_t=..`:

- `VeloxCorrection t= cm= st= air= grace= latch= speed2d= vz=` per observed correction (state right after the replay, `t` = client world time) and `VeloxCorrectionResponse t= ts= grace= latch=` (what the server's response container delivered, logged by the CMC before the replay),
- `VeloxNetSoak event=knockback|land|post_correction_sample|info` (land: `jumped`, `vz`, `speed_before/after`),
- `VeloxJumpPad: pad= role= t= launch=` on client and server for every pad launch.
- `VeloxResend old= new= pending= event=0|1 extra=` per RPC that carried an OldMove (time stamps in seconds, `-1.000000` = absent; `event=1` at Log level, `event=0` at Verbose, so the client needs `log LogVeloxNet Verbose`; shipping builds log neither). `extra=<ts>` (task M1-07, ADR 0002 A32/A35) is the time stamp of the second old move that rode along in the extra slot (the move before the OldMove: pending half of a lost dual RPC, two-RPC burst, start boundary of a take-off jump); `extra=-1.000000` = slot empty. The server simulates it as an OldMove before the engine's. Counters: `GetNumResentMoves()` (OldMove sent), `GetNumExtraResentMoves()` (extra slot used), both reset with the RPC counter.
- Server: `VeloxMoveGap ts= dt=` for a move > 20 ms (merged lost RPCs or a slow client frame) and `VeloxMoveRepaired ts= dt= slot=old|extra` for an old move simulated in its own partition (lost RPC repaired from the next RPC's OldMove, `slot=old`, or extra move, `slot=extra`; ADR 0002 A32/A35; the nettest row prints `repaired=N repaired_extra=M resent_extra=K`). An extra or old move the server already has is rejected by the engine's time stamp check without a Velox line (`ServerMove_PerformMovement`, `LogNetPlayerMovement` Log `ServerMove: TimeStamp expired`, Warning only beyond `net.NetServerMoveTimestampExpiredWarningThreshold`; visible with `log LogNetPlayerMovement Log`); it is not counted in `VeloxMoveRepaired`.

`nettest.sh` evaluates the windows from these lines (stray correction = at most one correction of <= 5 cm in the window). Known property of the setup: a server-side knockback is not predicted by the client, so every client move already in flight when the first correction is sent is also rejected by the server and answered with another (0 cm after the client replay) correction; see STATUS.md for the measured numbers.

## Bots (M4 task 04, ADR 0005 sections 12 and 14)

Bots are `AVeloxBotController`s on the server; they drive the same `UVeloxMovementComponent` as players through the player
input entry points only. Commands (authority world: dedicated server console, `-ExecCmds`, listen host):

- `AddBot [Easy|Medium|Hard|Expert] [Name]`: adds a bot (default preset `UVeloxBotSettings::DefaultDifficulty`, name from
  `BotNames`, palette colour, `IsABot` = `BOT` in the scoreboard). Refused at the mode's `MaxActivePlayers`
  (`VeloxBot add refused reason=full`). On a client the command is forwarded through `AVeloxPlayerController::ServerAddBot`
  (non-shipping, rate-limited by `BotCommandMinIntervalSeconds`); the client retries for 30 s while it is still connecting.
- `KickBot [Name|all]`: removes the bot (pawn and controller destroyed, `Logout` runs: `VeloxBot logout`). Client: `ServerKickBot`.
- URL: `?Bots=N` (capped to `MaxActivePlayers - 1` unless `?BotsFill=1`), `?BotDifficulty=Hard`; without `?Bots=` the
  settings' `DefaultBotCount` applies (0). Example: `L_ItemTest?game=Duel?Bots=2?BotsFill=1?BotDifficulty=Hard`.
- `velox.bots.List`: one `VeloxBot name= goal= item= enemy= vis= weapon= speed= link= filter= ...` line per bot.
- `velox.bots.Debug 0/1` (cvar): that line once per second per bot (`DebugIntervalSeconds`), including the controller's
  `tick_us_mean` / `tick_us_max` (rough per-bot cost; the section 13 stats land with task 05).
- `velox.bots.Goal <bot|all> <item tag | point x y z | none>`: forces a goal (kept across respawns until reached, taken
  or `none`); e.g. `velox.bots.Goal Celer Velox.Item.Health.Mega`.
- `velox.bots.Freeze 0/1` (cvar): every bot releases its keys and stands.
- `velox.bots.Kill <bot>`: environment-damage kill by the local player (kill feed, B8 death, auto-respawn).
- `velox.bots.Players`: the PlayerArray as this machine sees it (`VeloxPlayers net=client|server name= bot= color=`).
- `velox.bots.After <seconds> <command ...>`: runs a command after game-world seconds (headless scripts; restarts the
  count when the client travels to the server's map).
- `velox.bots.DumpNav`, `velox.bots.DumpPadLandings`, `velox.bots.Seed`: task 01 (nav paths, pad landings, master seed).

Log lines: `VeloxBotLink bot= type=JumpPad|Drop|Jump|JumpFast|RocketJump from= to= result=ok|missed|aborted reason= dt=`
(every traversal), `VeloxBotStuck bot= at= goal= strikes=`, `VeloxBotGoal`, `VeloxBotPath` (Verbose), `VeloxBot possess`,
`VeloxBotOffMesh bot= at= to= target=walkable|nearest tested= attempt= result=` / `VeloxBotOffMesh bot= recovered after=`
(task 04b: feet on the ground but off the nav mesh for `OffMeshGraceSeconds`, the bot walks straight to the nearest nav
point with no wall in between). `velox.bots.ListPresets` logs each preset with its effective `reaction_s` /
`reaction_jitter` (cvar overrides such as `velox.bots.ReactionJitter 0` applied).
Tunables: `[/Script/Velox.VeloxBotController]` (acceptance radii, stuck speed/strikes, repath interval, CPM key snap,
roam attempts) and `[/Script/Velox.VeloxBotPathFollowingComponent]` (`LinkEntryRadiusCm`, `LinkEntryImpulseRadiusCm`,
`PartialEndSlackCm`) in `DefaultGame.ini`; presets in `Content/Bots/DA_Bot_*`. Project-wide: `[/Script/Velox.VeloxBotSettings]`
(fight score `FightBaseScore` / `FightFullStrength` / `FightWeakWeaponScale`, maneuver constants `LinkStartAcceptanceCm`,
`JumpFastSpeedFraction`, `JumpFastRunUpCm`, `JumpFastRetries`, `RocketJumpImpulseTimeoutSeconds`; ADR 0005 D4).

Functional tests (console, like `FT_ItemPickup`; `quit` exits 0/1, `VeloxBotFt result=PASS|FAIL`):

```
UnrealEditor-Cmd Velox.uproject L_ItemTest -game -nullrhi -UseFixedTimeStep -FPS=60 \
    -ExecCmds="velox.bots.Seed 1, velox.bots.FtNavigation quit"         # FT_BotNavigation: pad + drop to Mega and RA
UnrealEditor-Cmd Velox.uproject L_MovementTest -game -nullrhi -UseFixedTimeStep -FPS=60 \
    -ExecCmds="velox.bots.Seed 1, velox.bots.FtStrafeCorridor quit"     # FT_BotStrafeCorridor: Hard strafe-jumps, Easy walks
```

All bot FTs in one go: `infra/ci/fts.sh [--port N]` (FtItemPickup, FtNavigation, FtStrafeCorridor, FtItemRouting,
FtOffMesh; `[fts] OK 5/5`), run for every bot task and before a merge to `main` (see `infra/ci/README.md`).

Unit rows: `Velox.Bots.ControllerRules` (seed, key presence, turn rate, stuck, names, `?Bots=` cap, link mapping) and
`Velox.Bots.FindNearestEnd` run in `build.sh`.
