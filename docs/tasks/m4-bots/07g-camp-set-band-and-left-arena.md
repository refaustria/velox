# 07g — Camp band on the set, `left_arena` / `world_kill` round labels (ADR 0005 D49)

**Milestone:** M4 Bots
**Agent:** dev
**Status:** todo
**Depends on:** 07d (D48 runner), 07e (diagnosis), ADR 0005 D46 (1d, 4), D48, D49; independent of 07f (content), but the live acceptance of D49 (3) needs both

## Goal
Two runner calibrations from ADR 0005 D49, verified without UE by `--reeval` on stored logs. (1) The BotDuel band
"`camp=1` ≥ 1 per round" fails on a fact of the round about one round in 13 (21–40 % per three-round set); it becomes a
set band, Σ `camp_episodes` ≥ 2 over the three rounds, and the per-round line a printed FLAG. (2) A bot that leaves the
arena (knocked over the open-top wall, falls for the rest of the round — 2 of 15 rounds before task 07f) is labelled
`left_arena=<bot>@<t> z=<z>` and fails the round with a label that routes it to content, not to bot work; a pawn killed
by the world (`Damage.Environment`, the post-07f KillZ path) is labelled `world_kills=N` and fails the round the same
way. After this task a round like a07d48 round 3 reads as what it was, and a round like a07final round 2 (a bot gone for
the second half, 5 frags in the first) no longer passes.

## Scope — files/modules the agent may touch
- `infra/ci/nettest.sh` — `analyze_round` (bot branch only): the camp per-round FLAG; `left_arena` / `world_kills` /
  `fell_out_lines` detection, labels, detail line, `stats.json` keys; `bot_set_eval`: the camp set band and the set
  line fields. Constants `ARENA_FLOOR_Z`, `LEFT_ARENA_DZ` beside `DWELL_RADIUS`. Nothing else in the verdict logic.
- `infra/ci/README.md` — the BotRun/BotDuel paragraph: the "Camp (D46 4)" sentence and a new sentence for the two
  labels; the per-round band list (`camp=1` line → FLAG).
- `docs/tasks/m4-bots/07g-camp-set-band-and-left-arena.md`, `docs/tasks/m4-bots/README.md` (status).
(No file under `Source/`, `Content/` or `Tools/`. The KillZ/clip-wall content change is task 07f.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` D49 (rules (a), (b) and the expected numbers), D46 (1d) (every valid round is judged;
  geometric-trail misses need no reproduce-first), D46 (4) (the camp episode as the unit; report-only fields), D48 (b)
  (the Mega per-round FLAG this mirrors), D36 (set bands, tail rule).
- `docs/tasks/m4-bots/07e-botduel-round-failures.md` — the two rounds, the fall trace, `camp.py`.
- Current code: `analyze_round` camp parsing at `nettest.sh` l. ≈ 1354–1370 (`camp_lines`, `camp_episodes`,
  `camp_open`, `camp_max_lead`), the Duel fail block l. ≈ 1614–1620 (`if camp_lines < 1: fails.append(...)`), the
  `VeloxBot` sample regex `POSR` l. ≈ 1219, the `VeloxKill` branch l. ≈ 1290, `stats` dict l. ≈ 1680, `bot_set_eval`
  l. ≈ 1742–1830 (the Mega set band is the pattern to copy).
- Log formats: `VeloxBot name=<bot> … pos=V(X=…, Y=…, Z=…)` (1 Hz, server); `VeloxKill victim=<label> killer=<label>
  suicide=<0|1> tags=[<comma list>] …` (`Source/Velox/GameModes/VeloxGameMode.cpp` l. 80; the world kill carries
  `Damage.Environment`); `VeloxDeath <pawn> fell out of the world at V(…)` (`Source/Velox/Character/VeloxCharacter.cpp`
  `FellOutOfWorld`).

## Acceptance criteria
- [ ] **Camp FLAG (D49 a).** In `analyze_round`, BotDuel: the `fails.append("no VeloxBotRoute camp=1 line")` is removed;
      when `camp_episodes == 0` the round prints `FLAG no camp=1 route line in this round (fact of the round, D49; the
      set band judges)` as a `.` detail line. `stats.json` gains `camp_episodes`, `camp_lines`, `goal_changes`,
      `camp_max_lead` (floats/ints as computed today).
- [ ] **Camp set band (D49 a).** In `bot_set_eval`, BotDuel, judged for `--repeat 3`: `sum(camp_episodes) < 2` →
      `- Duel camp episodes over the set: %d < 2 (D49)`; the set prints a line
      `camp: episodes=[a,b,c] sum=N lines=[…] share_pct=[…] max_lead_s=[…]` before the Mega line.
- [ ] **`left_arena` (D49 b i).** Constants `ARENA_FLOOR_Z = 0.0`, `LEFT_ARENA_DZ = 1500.0` beside `DWELL_RADIUS`, with a
      comment naming D49 and the ≥ 300 cm margin below the map's `KillZ` (check the value 07f wrote in
      `Tools/editor/create_item_test_map.py` if 07f has landed; if its KillZ is below −1200, raise `LEFT_ARENA_DZ` so
      the margin holds and say so in the report). For BotRun and BotDuel: the first `VeloxBot` sample (any phase) with
      `Z < ARENA_FLOOR_Z − LEFT_ARENA_DZ` sets `left_arena = (bot, stamp hh.mm.ss, z)`; the round FAILs with the label
      `bot <bot> alive below the arena at <stamp> (z <z>): KillZ/FellOutOfWorld not working — content/engine, not bot
      work; the round's other misses are consequences`, inserted as the **first** entry of `fails`; the detail line
      `left_arena=<bot>@<stamp> z=<z>` or `left_arena=none`; `stats.json` `left_arena` (bot name or null).
- [ ] **`world_kills` (D49 b ii).** `VeloxKill victim=` lines (not `ignored`) whose `tags=[…]` contains
      `Damage.Environment` are counted as `world_kills` with their victims; `VeloxDeath … fell out of the world` lines
      are counted as `fell_out_lines`. `world_kills > 0` FAILs the round with the label `<N> pawn(s) killed by the world
      (<victims>): left the arena through the 07f clip — content defect, not bot work`, placed right after a `left_arena`
      label (or first). Detail `world_kills=N [victims] fell_out_lines=M`; `stats.json` `world_kills`.
- [ ] **Set line.** `bot_set_eval` prints `left_arena=[…] world_kills=[…]` per round (both scripts). No set band on
      them: a failed round already fails the run; the set sums still include the round (D46 1d).
- [ ] **Re-evaluation of the stored logs** (`VELOX_OUT=<scratch> infra/ci/nettest.sh --reeval <dir>`; the scratchpad
      paths are the lead's, `/tmp/claude-1001/-home-bojan-velox/2d67b3e8-90f7-4bfb-b88f-343439b061f3/scratchpad/`):
      - `docs/adr/evidence/0005-D48-a07d47/botduel`: every round PASS as before, no FLAG for camp; set
        `camp: episodes=[3,3,2] sum=8`, `left_arena=[none,none,none] world_kills=[0,0,0]`, set behaviour PASS, cost PASS,
        all D48 numbers unchanged (`within_6s=4 within_3s=2`, cost windows `[0, 0, 0]`, `rr_round` 1.25/1.36/1.43).
      - `docs/adr/evidence/0005-D48-a07d47/botrun` and `/default`: verdicts and numbers unchanged; `left_arena=none`,
        `world_kills=0` in every round.
      - `a07d48/20261031-084210-3063489`: round 1 PASS with the camp FLAG (was FAIL); round 2 PASS; round 3 FAIL with
        `left_arena=Celer@07.48.16 z=-4572` as the first `- ` line and `no frag in 120 s` after it; set
        `camp: episodes=[0,1,2] sum=3` (band PASS); the runner's exit code is 1 (a round failed).
      - `a07final/20261030-214422-2129972`: round 2 FAIL with `left_arena=Celer@20.48.07 z=-3568` (was PASS); rounds 1
        and 3 unchanged; set `camp: episodes=[3,2,3] sum=8`.
      - `a07acc/logs/20261030-143204-1266558`: set `camp: episodes=[1,2,3] sum=6`; `left_arena=none` everywhere; the
        contended-era verdicts otherwise unchanged.
      - `world_kills=0` and `fell_out_lines=0` in every stored round (no KillZ before 07f).
- [ ] **No regression on the D47 fixture.** `--reeval docs/adr/evidence/0005-D47-pre09a5aec --scripts BotRun,BotDuel`
      prints exactly the D47 (f) cost numbers; its behaviour FAILs are by construction (no route lines) and may now
      include the camp FLAG instead of the old camp FAIL.
- [ ] `bash -n infra/ci/nettest.sh` clean; `python3 -I` stays the interpreter of both embedded scripts;
      `infra/ci/build.sh` → `[ci] CI OK` (standing rule; nothing compiled changes).

## Verification steps
1. Before editing: `--reeval` on the five directories above with the current runner and keep the output (the diff
   baseline).
2. Edit `nettest.sh`; `bash -n infra/ci/nettest.sh`.
3. `--reeval` on the same five directories; `diff` against step 1 — the only changes are the ones listed in the
   acceptance criteria (camp FLAG/band lines, the two labels, the new detail and set fields).
4. `--reeval` on the D47 fixture: D47 (f) numbers unchanged.
5. `infra/ci/build.sh` → `[ci] CI OK`.
6. Do not run live rounds: D49 (3) schedules the live acceptance after 07f and this task have both landed.

## Report back
Standard dev format: result; what changed (functions, new fields, the constants and the KillZ value you checked);
verification (the five `--reeval` set lines, round 3 of a07d48 and round 2 of a07final in full, the fixture's cost
lines, the `diff` summary of step 3); open issues. State explicitly if Unreal was unavailable for step 5.

## Notes / outcome
(Filled by the agent or the lead after completion.)
