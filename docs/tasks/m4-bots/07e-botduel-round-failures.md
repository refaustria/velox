# 07e — Root cause of two per-round BotDuel failures (fresh live x3 on 421548c)

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** done (diagnosis; no code change — cause is outside `Source/Velox/Bots` and `infra/ci/nettest.sh`, needs a split)
**Depends on:** 07d, ADR 0005 D36, D39, D46–D48

## Goal
A live BotDuel x3 on `m4-bots` 421548c (bot code unchanged since 04j; lead's scratchpad
`a07d48/20261031-084210-3063489`, `live.out`) failed two rounds on their own while the set passed:
round 3 `- no frag in 120 s` (kills=0) and round 1 `- no VeloxBotRoute camp=1 line` (kills=5). Find out whether each
is a bot defect, a runner/start defect, a defect elsewhere, or a band issue, and fix it if it is in scope.

## Scope
Read-only diagnosis of the logs; fixes allowed in `Source/Velox/Bots` (+ tests) or `infra/ci/nettest.sh` only.

## Findings

### Round 3 — "no frag in 120 s": a bot was knocked out of the arena and fell for 95 s (map defect)
- The match went Live normally (`phase=Live` 07:47:57, `reset_level spawned=2/2`), both bots spawned, saw each other
  (`vis=1 known=1`), picked Fight/Item goals and fired: `VeloxShot`/`VeloxDamage` lines from both (e.g. Celer MG hits on
  Rapidus 86->65, Rapidus rocket splash on Celer 61->35 at 07:48:13.420).
- That rocket splash gave Celer `knockback=V(X=-413, Y=-258, Z=816)` (950 cm/s) while Celer was already moving at
  ~800 cm/s near (2134, -1252). One second later Celer was at (1346, -2268, 532) — *outside* the south perimeter wall
  (y = -2000, 400 cm high, open top) and above it — and from then on airborne with `path=0`, Z -1004 -> -4572 -> ...
  -905 340 at round end (07:49:47). It never died: `L_ItemTest` has no KillZ of its own, so `AVeloxCharacter::FellOutOfWorld`
  (which works) only fires at the engine default `WorldSettings.KillZ` (about -1 048 575), ~105 s of fall.
- With the opponent gone, Rapidus spent the remaining ~95 s collecting items with `enemy=- known=0`; the P-window
  `max_fire_us=0` is that, not a firing defect.
- **Not a bot, runner or GAS defect.** The arena lets Q3-strength knockback (950–1875 cm/s) throw a pawn over the
  400 cm open-top walls, and there is no out-of-bounds kill. A human hit the same way would fall for 100 s too.

### Round 1 — "no camp=1 line": fact of the round (band issue, not a defect)
- `DuelCampOverScore 7` is an *item* score bar (D39), not the match score: a camp is chosen only when a major is
  unavailable within `DuelCampMaxLeadSeconds 20` *and* the best item scores < 7.
- Round 1, Live: 46 `VeloxBotRoute` lines, no chosen Item scored < 7 (`item_lt7=0`); in every Item decision whose
  candidates showed an unavailable major with lead 0–20 s, the chosen item scored 9.9–38. Highest fight share of the set
  (15.4 %, 5 kills, Celer died 4 times — each death resets the arsenal so weapons score high). The rule did what D39
  says; the round simply never offered a camp.

### Rate over older BotDuel rounds (15 distinct rounds: a07d48 x3, a07d47 x4, a07final x4, a07acc x4;
`docs/adr/evidence/0005-D48-a07d47` duplicates a07d47; the D47-pre09a5aec logs are filtered, no Live)
- **Out-of-arena fall:** 2/15 — this round 3, and `a07final/20261030-214422-2129972/round-2` (Celer, BattleSuit, hit
  by a Quad rocket, 1875 cm/s, Z +1695, at (2029, -1004) at 20:48:04; out over the north-east, fell to -661 000;
  the round still had 5 kills before it, so it passed).
- **camp=0 round:** 1/15 (this round 1). camp=1 counts elsewhere: 1–7 per round.
- Script: lead's scratchpad `a07e/camp.py` (per log: Live route lines, camp=1, Item < 7, kills, first Z < -1000).

## Recommended split (not done here — out of scope)
1. **Content (dev):** `Tools/editor/create_item_test_map.py` + regenerate `L_ItemTest`: (a) set
   `WorldSettings.KillZ` to about -1000 so a pawn that leaves the arena dies at once through the existing
   `FellOutOfWorld` -> `Velox.Damage.Environment` path; (b) better, also seal the arena (Q3-style): invisible
   `BlockAll` clip walls / a clip ceiling over the perimeter at about 1500–2000 cm, with no nav influence
   (`CanEverAffectNavigation=false`), so knockback cannot carry anyone out. Check the other test maps
   (`L_MovementTest`, `L_CombatTest`) for the same gap.
2. **Runner (dev, optional):** `nettest.sh` prints `left_arena=<bot>@t` when a bot status line shows Z below the floor
   by > 1000 cm, so a round like this is labelled instead of looking like a firing defect.
3. **Architect:** the D39 band "camp=1 >= 1 per round" fails ~1 round in 15 on a pure fact of the round; rule
   whether it becomes a set band (like the Mega band in D48) or stays per-round.

## Outcome
No code changed. Two causes: a map defect (open-top arena, no KillZ) that needs a content task, and a camp band
that misses a fact-of-the-round about 1/15 rounds, for the architect.
