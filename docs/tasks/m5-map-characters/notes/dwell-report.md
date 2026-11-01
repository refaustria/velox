# BotDuel dwell 6–7 s on L_ItemTest: diagnosis (2026-10-31)

Head `36164a6` (`m5-map-characters`). No code or measure changed. For the architect: ADR 0005 §7 / D16 / D39, ADR 0006 §6 / §7, E0.3 (d).

## Verdict

**This is legitimate Duel item timing, and the dwell measure counts it wrongly. It is not a bot defect.** The bot has an Item goal
on a major item or powerup that is not up yet. It reaches the spawn point and stands there (`arrived=1 speed=0 stuck=0`) until
the item respawns. Then it takes the item in the same frame and leaves. ADR 0005 §7 specifies exactly this: "skill 1 with
`MaxWaitSeconds 8` sends a duel bot to Mega/RA up to 8 s early and lets it wait there". D39 adds that the timing path "turns
the camp into an Item goal for the last 8 s, so the bot ends standing on the spawn point". The D34 dwell scan leaves out
`camp=1` samples. It does not leave out these Item-goal waits, because a timed wait is logged as `goal=Item camp=0`. With
the old 8 s cap a wait of at most about 8 s fit under the limit. The M5-04 cap of 5 s does not.

## Answers

1. **What the bot does.** It stands still on the spawn point of its goal item, or for Armor.Yellow / Quad it strafes inside
   150 cm while it fights, waiting for the respawn. Goal items for the excursions: Powerup.BattleSuit (2565,−1701),
   Powerup.Regen (2250,−1700), Powerup.Quad (500,0), Armor.Red (2100,1200), Armor.Yellow (ramp, ≈ −200,1200). On L_ItemTest
   the SE "quadrant" positions are grid slots 16/17 (Regen x 2250, BattleSuit x 2550, y −1700, 300 cm apart). (2259,−1699)
   is the Regen, (2539,−1715) and (2570,−1642) are the BattleSuit, and (475,−8) is the Quad.
2. **Why.** On purpose: item timing (§7 `Wait ≤ MaxWaitSeconds × ItemTimingSkill`, Hard 8 × 1.0, widened by the D16
   `CommittedScore` commit travel). Nothing is stuck: `stuck=0` in every excursion sample, there are no `VeloxBotStuck` or
   `VeloxBotRepath` lines in the window, the bot does not oscillate (one position, `speed=0`), and the nav mesh is not
   involved. The SE cluster comes from the map plus the powerup timers. The powerups' first spawn is random, 30–60 s
   (`InitialSpawnDelay`, Q3 45 ± 15). The bot takes Regen, and the BattleSuit 300 cm away is due 5–8 s later: it walks
   0.3 s and waits. Whether the two first spawns fall within 8 s of each other is random, which gives the "about one round
   in three".
3. **Defect or measure?** It is the measure. The wait never runs past the respawn: in all 61 rounds below, `over=0` timed
   waits lasted longer than `respawn_in` at the route line + 1 s. The longest `respawn_in − travel` at the route line was
   8.2 s, which is the Hard gate plus the commit widening. The cap change (8 → 5 s) uncovered the excursions. They were
   there before: stored D50 evidence has 7 s (`0005-D50-near-human/botduel/set-2/round-1`: Rapidus RA (2054,1188),
   Celer Quad (522,−39)) and 6–7 s (`calibration/base` r2, `tune2-iteration1` r1: Celer YA).

## Evidence

Key case (a07d48 r3, the BattleSuit excursion), from the server log:

```
t=67.51 VeloxPickup item=Powerup_Regen by=Rapidus
t=67.58 VeloxBotRoute bot=Rapidus goal=Item item=Powerup.BattleSuit camp=0 score=15.79 travel=0.32 available=0 respawn_in=6.0 lead=5.6
+0.4 s  VeloxBotGoal arrived goal=item
1 Hz    VeloxBot goal=item arrived=1 item=BattleSuit speed=0 stuck=0 pos=(2565.93,-1701.17) x 6 samples (identical position)
t=73.565 VeloxPickupRespawn BattleSuit -> VeloxPickup by=Rapidus (same frame) -> next route line
```

Each excursion ≥ 6 s, with `scratchpad/dwell/tools/dwellwhy.py` (route line in force, per-sample `arrived` / `stuck` / `speed`):

| round | bot, place | item | route line | samples | end of run |
|---|---|---|---|---|---|
| a07d48 r3 | Rapidus (2566,−1701) | BattleSuit | avail=0 resp 6.0 travel 0.32 | arrived 111111, speed 0×6 | respawn + take |
| a07d48 r3 | Rapidus (2077,1187) | Armor.Red | avail=0 resp 6.1 travel 0.12 | arrived 111111 | respawn + take |
| a07acc r3 | Celer (2531,−1701) | BattleSuit | avail=0 resp 5.8 travel 0.30 | arrived 111111, speed 0×6 | respawn + take |
| D50 set-2 r1 | Rapidus (2054,1188) | Armor.Red | avail=0 resp 9.3 travel 1.65 | arrived 0101111 | respawn |
| D50 set-2 r1 | Celer (522,−39) | Quad | avail=0 resp 6.3 travel 1.46 | moving, vis=1 (fight) | respawn + take |
| D50 base r2 | Celer (−218,1208) | Armor.Yellow | avail=0 resp 8.0 travel 0.09 | moving, vis=1 | respawn + take |
| D50 tune2 r1 | Celer (−102,1121) | Armor.Yellow | avail=0 resp 8.7 travel 0.19 | moving, vis=1 | respawn + take |

The 03b set logs for the four quoted cases are gone (that worktree's `Saved/CI`). Their positions and labels (`Rapidus`,
`goal=item`, the Regen/BS/Quad spawns) match the rows above exactly.

**Fresh sets on 36164a6** (`nettest.sh --scripts BotDuel --repeat 3 --keep-going`, ports 8131/8132, load 1.4–2.6, no
foreign UE process during the rounds): set 1 max dwell 3/4/2, set 2 max dwell 3/5/3, both `set PASS`. These 6 rounds have
no excursion, but every round has timed waits of 6.9–9.6 s (longest per round: RA 9.6, RA 9.6, Regen 8.2, Quad 6.9, Mega
8.1, RA 7.0). They escape the dwell scan only when the bot drifts more than 150 cm while it waits (fighting, dodging).
Logs: `scratchpad/dwell/set{1,2}/`.

## Proposed measure (for the architect; not implemented)

**Rule.** A 1 Hz `VeloxBot` sample is a *timed-wait sample* when two conditions hold. First, the bot's last
`VeloxBotRoute bot=<n> goal=…` line chose `goal=Item item=X available=0`, and no `VeloxPickupRespawn item=X` has come since.
Second, the sample still reads `goal=item item=X`. Timed-wait samples are left out of the D34 scan exactly like `camp=1`
samples: the run is dropped and restarts after them. The wait is then judged on its own:
- (a) `wait_s` is the time from the route line to X's respawn. It must be ≤ `respawn_in` + 1 s, so a bot never stands past
  the respawn.
- (b) `respawn_in − travel` at the route line must be ≤ `MaxWaitSeconds × ItemTimingSkill + MaxCommitTravelSeconds /
  ItemTimingSkill`, the §7/D16 gate.

Both checks use only existing log fields. No bot change is needed.

**Retrospective, 61 BotDuel rounds** (55 stored rounds from 30–31 Oct plus the 6 fresh ones,
`scratchpad/dwell/tools/dwellprop.py`):

| quantity | current D34 | proposed |
|---|---|---|
| rounds with max dwell > 5 s | 10 | 2 |
| largest value, 31 Oct heads | 7 | 5 |
| timed waits failing (a) | – | 0 |

The 2 rounds that stay above 5 s under the proposal are real defects, and both are still caught: a07 (30 Oct 09:11, Celer
18 s `goal=roam` on the YA ramp with stuck strikes 1-2-2) and a07acc (30 Oct 14:40, Rapidus 9 s `goal=roam` with stuck
strikes). Both are fixed now, but they show that the proposal does not hide stuck bots.

*Alternative:* put `wait=1` on the `VeloxBot` debug line (goal item unavailable), the way `camp` is carried. That is
cleaner, but it means a router/controller change and a log-format change.

*Not proposed:* raising the cap back to 8 s (it would also let a stuck 6–8 s pass), or lowering Hard `MaxWaitSeconds`
(that is a behaviour change to D39/§7).

## Test numbers

- Build: `infra/ci/build.sh --targets VeloxEditor --no-tests`, Succeeded.
- BotDuel set 1: kills 12/11/9, pickups 53/44/43, stuck 0/2/2, max dwell 3/4/2 s, set PASS (behaviour PASS, cost PASS).
- BotDuel set 2: kills 12/12/10, pickups 43/51/45, stuck 0/2/2, max dwell 3/5/3 s, set PASS (behaviour PASS, cost PASS).
- BotRun / FFA: not run, because no code changed.
