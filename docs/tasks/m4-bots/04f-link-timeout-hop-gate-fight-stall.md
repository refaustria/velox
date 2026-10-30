# 04f — Link timeout, hop heading gate, fight-stall rule, bounded progress pause, dodge-jump headroom

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** todo
**Depends on:** 04c-b and 04d merged on `m4-bots`; ADR 0005 D27–D34 (the 2026-10-30 ruling pass). Runs in parallel with 04e (disjoint files; the two FFA soaks must not overlap on the machine).

## Goal
No bot stalls for longer than a few seconds anywhere on `L_ItemTest`: a link traversal that cannot complete is abandoned within `LinkTimeoutSeconds` (the 57 s Drop-link hang of 04c-b), a bot that lands with its velocity away from the target walks instead of re-hopping into a wall (the dominant progress-strike class), a pathless fight pressed against a wall gets a strike and routes away for a few seconds (the 5–14 s dwells of the G-series), the progress clock's pause under the combat overlay is bounded, and the dodge jump respects the hop headroom sweep. Verified with ten FFA runs against strike **and** dwell bands.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotManeuver.{h,cpp}` — `FVeloxManeuverParams::TimeoutSeconds`, `BelowStartAbortCm`; `FVeloxManeuverInput::HeightBelowStartCm`; `EVeloxManeuverResult::Timeout`, `BelowStart` (+ `LexToString`); the two rules of D28 (a) and (b) in `Tick`
- `Source/Velox/Bots/VeloxBotPathFollowingComponent.{h,cpp}` — fill `HeightBelowStartCm` (link start z − feet z) and the two params from `UVeloxBotSettings`; the `bLinkPending` wait aborts with `Timeout` when `Now − LinkSegmentStartTime ≥ LinkTimeoutSeconds`; `HasHopHeadroom` becomes public (D31)
- `Source/Velox/Bots/VeloxBotSteering.{h,cpp}` — `FVeloxSteerParams::MaxHopHeadingDeg`, `HopHeadingMinSpeedCmS`; the ground hop gate (D29); `MakeParams` reads both from `UVeloxBotSettings`
- `Source/Velox/Bots/VeloxBotController.{h,cpp}` — `UpdateStuckProgress` pause cap (`PausedSeconds`, `MaxPauseSeconds`; D27); `StuckProgressMaxPauseSeconds` and `FightStallCooldownSeconds` config properties; the fight-stall predicate and `rule=fight_stall` strike, `State.FightSuppressedUntil` (D30); `NotifyLinkFinished` counts a `Timeout` as a strike with `rule=link_timeout` (D28); the debug line gains `supp=1|0` (fight suppressed) so the soak can read it
- `Source/Velox/Bots/VeloxBotItemRouting.cpp` — `FillEnemyInput` only: `bEnemyVisible = false` while `Bot` is fight-suppressed (D30); nothing else in the router
- `Source/Velox/Bots/VeloxBotCombat.cpp` — `Overlay` only: the dodge-jump OR gated by `HasHopHeadroom(Now)` (D31)
- `Source/Velox/Bots/VeloxBotSettings.{h,cpp}`, `Config/DefaultGame.ini` — `LinkTimeoutSeconds=6.0`, `LinkBelowStartCm=100.0`, `MaxHopHeadingDeg=45.0`, `HopHeadingMinSpeedCmS=100.0` under `[/Script/Velox.VeloxBotSettings]`; `StuckProgressMaxPauseSeconds=4.0`, `FightStallCooldownSeconds=4.0` under `[/Script/Velox.VeloxBotController]`
- `Source/Velox/Tests/VeloxBotTests.cpp` (`Velox.Bots.Maneuver`, `Velox.Bots.StrafeSteering`, `Velox.Bots.Decision` rows), `Source/Velox/Tests/VeloxBotFunctionalTest.cpp` (`Velox.Bots.ControllerRules` rows; `FtOffMesh` cases unchanged)
- The FFA runner and summary: copy `scratchpad/a04d/ffa.sh` + `summary.py` into your own scratchpad, point `W=` at your worktree, add the dwell scan of `scratchpad/stall.py` (max dwell per run after `VeloxMatch phase=Live`, `camp=1` excluded), the longest `VeloxBotLink dt=`, the `reason=` and `rule=` histograms, and `ramp.py`'s `max_dwell_s` (fights included). Stays in the scratchpad (07 brings the soak into `nettest.sh`).
(Anything not listed is out of scope: no change to `FightBaseScore`, `FightFullStrength`, `FightWeakWeaponScale`, `UnownedWeaponNeedFloor`, `FightPathWeight`, the bias/hysteresis tables, `StuckSeconds`, `StuckSpeedCmS`, `StuckProgressCm`, `AgentHeight`/`AgentRadius`, the off-mesh recovery, `Tools/editor/*`, any map — 04e owns the maps.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` **D27–D34** (the spec and the values; the evidence paragraph names the logs), D18 (headroom guard, progress rule), D6 (link completion, `bLinkPending`), D12 (Fight goals and paths), D17 (off-mesh recovery — the strip case ends up there after a `below_start`/`timeout`), D20 (bands), §4 maneuvers, §5 steering
- `docs/tasks/m4-bots/04c-b-hop-headroom-and-progress-stuck.md` notes (the five items this task closes)
- Logs: `scratchpad/runs/p-s3.log` (Celer, Drop `VeloxNavLink_28`, 57.18 s; `python3 -I scratchpad/trace.py <log> Celer 60 120`), `scratchpad/runs/G-s3.log` (five fight dwells against y = 1462.5; `python3 -I scratchpad/stall.py 'scratchpad/runs/G-s*.log'`)
- Code: `VeloxBotManeuver.cpp` `Tick` (`Approach` cases), `VeloxBotPathFollowingComponent.cpp` `TickManeuver` (`bLinkPending` branch, `BeginManeuver`), `VeloxBotSteering.cpp` l.56–57 (the corner rule the heading gate joins), `VeloxBotController.cpp` `UpdateStuck`/`UpdateStuckProgress` (pure rules), the stuck block after `Follow->StepFollow`, `NotifyLinkFinished`, `UpdateMove` (`IsOnLink()` return before `UpdateOffMeshRecovery` — leave it), `VeloxBotItemRouting.cpp` `FillEnemyInput`, `VeloxBotCombat.cpp` `Overlay` (`Pending.bJumpHeld |= …`)

## Acceptance criteria
- [ ] `Velox.Bots.Maneuver`: Drop on the ground in `Approach` for 6.1 s → `bAbort`, `Reason = Timeout`; Drop in `Approach` with `HeightBelowStartCm 150` → `BelowStart` that tick, with 50 → no abort; JumpPad `Airborne` for 6.1 s → `Timeout`; RocketJump `Launched` at 0.31 s without impulse → `NoImpulse` (unchanged, the shorter bound wins); all existing rows pass.
- [ ] `Velox.Bots.StrafeSteering`: ground, bunny-hop preset, V = (800, 0), target at 120° → `bJumpHeld` false; target at 30° → true; V = (50, 0), target at 180° → true (below `HopHeadingMinSpeedCmS`); the 125-tick corridor row (speed ≥ 1.25 × `MaxGroundSpeed`, heading ≤ 10°) and the D3 air-turn row unchanged.
- [ ] `Velox.Bots.ControllerRules`: `UpdateStuckProgress` paused from t = 0 with no progress → exactly one strike, at 6.0 ± 0.1 s; paused 1 s then unpaused with 100 cm progress → none; the fight-stall predicate (`Goal == Fight`, `!bMoveActive`, enemy visible, on ground) with speed 10 → strike at 2 s, and no strike when the enemy is not visible (plain `Fight` without sight is a chase with a path and stays under the old predicate).
- [ ] `Velox.Bots.Decision`: with an item available and `bEnemyVisible` false (suppressed) → Item; the four D13 and the three D22 worked cases still pass.
- [ ] Log lines: `VeloxBotLink … result=aborted reason=timeout|below_start`; `VeloxBotStuck … rule=speed|progress|fight_stall|link_timeout`; `VeloxBot` debug line has `supp=`.
- [ ] `infra/ci/build.sh` → `[ci] CI OK`; `infra/ci/fts.sh` → `[fts] OK 6/6` twice (`FtOffMesh ramp_strip_wall` still passes — the strip bot now also leaves any Drop link it is pulled into); `-DisableAdaptiveUnity` build succeeds.
- [ ] FFA, 3 Medium, dedicated, 120 s, seeds 1–10 (your runner, `SEED0=0`), on the idle machine, on the merged head (04c-b + 04d + this task; 04e too if merged). **Bands:** `VeloxBotStuck` ≤ 3 in at least 9 of 10 runs and ≤ 5 in every run; `max_dwell_s` ≤ 6 in every run (D34 definition: samples after `phase=Live`, 150 cm, `camp=1` excluded); every `VeloxBotLink dt=` ≤ 8 s; `ramp.py max_dwell_s` (fights included) ≤ 5 s; the D20 bands still hold in every run: pickups by bots ≥ 20, `DA_Item_Health_Mega` by a bot ≥ 1, kills ≥ 5, `refused` 0, `giveup` 0, `Error:`/`ensure` 0; `VeloxBotBudget mean_us` ≤ 300. **Reported, not judged:** per run the `rule=` histogram of the strikes, the `reason=` histogram of failed links, the longest link `dt=`, the `supp=1` share of debug lines (expected < 5 %), the Fight share (D33: 25–45 %), the armed fight share (≥ 40 %), the `start=air` all-unreachable count (0 if 04e is merged, else the 04d-level 2–6).
- [ ] If a band is missed: report the ten-run numbers and stop. Do not move any value listed under "out of scope"; `LinkTimeoutSeconds`, `MaxHopHeadingDeg`, `StuckProgressMaxPauseSeconds` and `FightStallCooldownSeconds` may each be moved **once** by at most ±50 % if the report names which run and which bot the change is for — otherwise they return to the architect with the numbers.

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`; run `Velox.Bots.Maneuver`, `Velox.Bots.StrafeSteering`, `Velox.Bots.ControllerRules`, `Velox.Bots.Decision` and paste the new rows' output.
2. `infra/ci/fts.sh --port <N>` twice → `[fts] OK 6/6`.
3. `ffa.sh <port> 120 10 <outdir>` → ten summary lines (bands + reported fields); paste them. Then `python3 -I stall.py '<outdir>/ffa-*.log'` and `python3 -I ramp.py <outdir>/ffa-*.log` tails.
4. Reproduce the strip case once: `velox.bots.Goal <bot> Velox.Item.Armor.Yellow` from a start that walks the ramp (or the `FtOffMesh ramp_strip_wall` position with a forced Item goal south of the platform) and show a `reason=below_start` or `reason=timeout` line followed by `VeloxBotOffMesh … recovered` or a normal path within 8 s.

## Report back
Standard dev-hard format: result, what changed (files, the six new settings and their values, the new log fields), verification (commands + output tails, the unit rows, the ten summary lines, the stall/ramp scans), open issues. Flag for the lead: any band missed (with numbers), any value moved under the ±50 % rule, `supp=1` share above 5 %, any run with a `VeloxBotStuck` strike burst (≥ 3 strikes of one bot within 10 s), any `VeloxBotLink dt=` above 6 s that did **not** end in `reason=timeout`.

## Notes / outcome
(Filled by the agent or the lead after completion.)
