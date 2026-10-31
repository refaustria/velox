# 03 — Near-human bots: Easy/Medium spray, more Duel camping, measured targets

**Milestone:** M4 follow-ups (human decisions q24 and q28, 2026-10-30)
**Agent:** dev-hard (the lead may split step 1 — log fields + runner — to `dev`)
**Status:** done (Medium near-human bands missed, reported; D51 open for the architect)
**Depends on:** ADR 0005 D50 (this task's ruling; also D14, D36, D39, D46 (4), D48, D49), STATUS questions 24 and 28, the M4 acceptance evidence `docs/adr/evidence/0005-D49-m4-final/`

## Goal
Easy bots spray and Medium bots spray mildly: a per-preset direct-hit gate (`DirectHitRadiusScale`), a trigger hysteresis (`FireReleaseConeScale`) and a hold-after-lost (`FireHoldAfterLostSeconds`) make them fire through their aim error and hold the trigger like a human, while Hard/Expert keep today's discipline. Duellers camp more (`DuelCampOverScore` 7 → 10). "Near-human" is not a feeling but five quantities the nettest runner prints per preset against the D50 (5) target table (hitscan accuracy, projectile accuracy, shots per kill, trigger share, held share) plus the camp trio (episodes, share, time share). The task measures the **baseline first**, tunes inside the D50 windows, then produces the ≥ 12 fresh live rounds per script from which the architect re-derives the D36/D48/D49 count bands (D51). Nothing in the aim model moves.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotTypes.h`, `VeloxBotDifficulty.h/.cpp` — the three new preset fields (POD, asset, cvar table)
- `Source/Velox/Bots/VeloxBotCombat.h/.cpp` — `FVeloxFireInput` (`bTriggerHeld`, `SecondsSinceTargetSeen`), `EVeloxFireVerdict::FireHeld`, `EvaluateFire`, the Act-phase glue (gate scale, aim-ray trace while held)
- `Source/Velox/Bots/VeloxBotItemRouting.h` — `DuelCampOverScore` default; `Config/DefaultGame.ini` — the explicit `[/Script/Velox.VeloxBotItemRouter]` key
- `Tools/editor/create_bot_content.py` — `PRESETS` rows for the three fields (idempotent, `verify_presets` extended)
- `Source/Velox/Net/VeloxLagCompensationSubsystem.cpp` (+ the request struct it logs from) — `VeloxShot … by= weapon=` fields; `Source/Velox/Weapons/VeloxProjectile.cpp` — `VeloxProjectileExplode … by=`
- `infra/ci/nettest.sh`, `infra/ci/README.md` — the D50 (4) quantities, `--bot-difficulty`, `calibration=` tagging, set-line pooling, `stats.json` keys
- `Source/Velox/Tests/VeloxBotTests.cpp` — `Velox.Bots.ShouldFire` rows (D50 (2) a–e), D39 `Decision` rows if one reads the data default
- `docs/adr/0005-bots.md` — only the "Notes / outcome" numbers go here via the architect (D51); the agent does not edit the ADR
- `docs/adr/evidence/0005-D50-near-human/` — the evidence directory (D48 (4)(ii) recipe)
(Anything else — the aim model, `DirectHitRadiusCm` itself, `FireConeDeg`, movement, perception — is out of scope: report back.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §8, §11, §14; D14 (the gate and the hook), D36 (set bands), D39 (camp rule and what-if), D46 (4) (camp episode unit), D48 (Mega band form), D49 (1) (re-derivation method), **D50**
- `docs/tasks/m4-bots/07g-camp-set-band-and-left-arena.md` (runner conventions, `--reeval`), `docs/tasks/m4-bots/README.md`
- Memory notes: soak only on an idle host under `flock soak.lock`; check for orphaned UE clients first

## Acceptance criteria
- [x] **Step 1 (measurement, no bot change):** `VeloxShot` carries `by=<label> weapon=<tag leaf>` and `VeloxProjectileExplode` carries `by=<label>`; the runner prints per bot and per round (Live only) `acc_hitscan_pct`, `acc_proj_pct`, `shots_per_kill`, `trigger_share_pct`, `held_share_pct`, `acc_by_weapon`, `camp_time_pct`, and pools them on the set line with the D50 (5) band and `IN`/`OUT` (report-only); `--bot-difficulty <preset>` works and tags rounds `calibration=<preset>` with no D36/D48/D49 band judged. `nettest.sh --reeval` on `docs/adr/evidence/0005-D49-m4-final/` prints the same verdicts as before (`acc_*` = `n/a` there). `infra/ci/build.sh` CI OK, `infra/ci/fts.sh` 7/7.
- [x] **Baseline table** (idle host): BotRun ×3 Medium, BotDuel ×3, BotRun ×3 `--bot-difficulty Easy`, BotRun ×3 `--bot-difficulty Expert` with the unchanged bots; the five quantities per preset recorded in the notes.
- [x] **Step 2 (rule):** the three fields exist in the POD, the asset, the cvar table and `create_bot_content.py` with the D50 (2) values; `EvaluateFire` implements gate scale, release cone and hold-after-lost with ammo/range/readiness/self-splash never relaxed; `FireHeld` verdict logged as `fire=held`; the glue traces the aim ray while held with a splash weapon. `Velox.Bots.ShouldFire` has the D50 (2) rows a–e and passes; `Velox.Bots.AimError`/`AimLead`/`AimTurnRate` unchanged and green.
- [x] `DuelCampOverScore` is 10 in the header default and `DefaultGame.ini`; `Velox.Bots.Decision` green (a row that read the old default is updated and noted).
- [x] (misses reported) **Step 3 (tuning):** after at most two tuning iterations inside the D50 (5) windows, the four calibration sets show Easy and Medium inside their five bands (or the miss is reported with numbers and the knob position), Hard/Expert within ±5 points of the baseline accuracy with `held_share` ≤ 15 / 0, BotDuel camp episodes per round mean 3–5 and `camp_time_pct` 3–8.
- [x] **Step 4 (acceptance rounds for D51):** BotRun ×12 Medium and BotDuel ×12 (four `--repeat 3` sets each, idle host, `left_arena=none`, no `INVALID`) plus the D36 ten-run FFA (120 s, 3 Medium, seeds 1–10); every round's `stats.json`, the set summaries and the per-round table go to `docs/adr/evidence/0005-D50-near-human/` by the D48 (4)(ii) recipe, with a committed derivation script that prints, for each lower band (BotRun Σ kills, BotDuel Σ frags, Σ camp episodes, FFA Σ kills), the largest k with P(Σ < k) ≤ 2.5 % under the empirical bootstrap and the Poisson model, and the re-estimated Mega p. Misses against the current D36/D48/D49 values are reported as numbers, not "fixed".
- [x] (two slow-throughout rounds, see notes) Budget: `VeloxBotBudget mean_us` ≤ 300 in every round; `max_fire_us` reported (FLAG above 2 ms stays a flag).

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`, 97+ tests incl. the new `ShouldFire` rows.
2. `infra/ci/fts.sh` → `OK 7/7`.
3. `infra/ci/nettest.sh --reeval <path to docs/adr/evidence/0005-D49-m4-final/...>` → verdicts unchanged, new fields printed.
4. Baseline (idle host, `flock soak.lock`): `infra/ci/nettest.sh --scripts BotRun --repeat 3 --keep-going`; `--scripts BotDuel --repeat 3 --keep-going`; `--scripts BotRun --repeat 3 --bot-difficulty Easy`; `--scripts BotRun --repeat 3 --bot-difficulty Expert`.
5. After tuning: the same four; then step 4's twelve-round sets and the ten-run FFA (`docs/adr/evidence/0005-D49-m4-final/ffa.sh` as the template — fix its hard-coded worktree path).
6. `infra/ci/nettest.sh --scripts FireRun,FireRunControl --repeat 1` → PASS (the extended `VeloxShot` line did not disturb the hitscan parsers).

## Report back
Standard format plus: the baseline and post-tuning tables (preset × five quantities, camp trio), the final knob values, the per-round table and the derivation script's k's for D51, the evidence path. Note explicitly which engine-side assumptions were verified. A Hard/Expert drift beyond ±5 points or a knob outside its window is a stop-and-report, not a wider window.

## Notes / outcome
(Filled by the agent; the architect's D51 records the re-derived bands.)

**Commits:** ef06b13 (step 1: `by=`/`weapon=`, runner quantities, `--bot-difficulty`), 57f4b59 (step 2: rule, unit rows, D50
values, `DuelCampOverScore` 10), 47fea67 (tuning: final knobs, `DuelCampOverScore` 11). Evidence:
`docs/adr/evidence/0005-D50-near-human/` (README there). Build CI OK 97 tests (incl. `Velox.Bots.ShouldFire` D50 rows a-e),
fts OK 7/7 (after step 1 and step 2), `--reeval` of `0005-D49-m4-final/` unchanged verdicts (`acc_*` n/a), FireRun +
FireRunControl PASS.

**Quantities (set-pooled, Live; hitscan / projectile accuracy %, shots per kill, trigger share %, held share %):**

| Preset | Baseline (unchanged bots) | D50 values | Iteration 1 (final knobs) | Iteration 2 | Acceptance x12 (final) | D50 (5) target |
|---|---|---|---|---|---|---|
| Easy | 51.7 / 12.7 / 47.5 / 18.1 / 0 | 7.4 / 12.1 / 162 / 57.8 / 27.4 | **19.2 / 15.6** / 63.4 / 25.6 / 45.6 | 42.2 / 12.6 / 40.0 / 21.5 / 27.9 | - | 10-22 / 15-35 / 25-60 / >=50 / 20-45 |
| Medium | 56.7 / 27.8 / 19.4 / 13.3 / 0 | 19.1 / 25.7 / 65.1 / 43.7 / 50.4 | 21.2 / 23.2 / 75.6 / 39.3 / 36.8 | 18.5 / 17.7 / 73.8 / 42.9 / 31.9 | 18.2 / 23.8 / 74.8 / 41.9 / 35.6 | 22-35 / 30-50 / 12-30 / >=45 / 10-30 |
| Hard (BotDuel) | 56.3 / 24.7 / 29.1 / 23.7 / 0 | **33.4** / 24.1 / 69.2 / 45.1 / **49.8** | 53.6 / 21.0 / 24.8 / 20.2 / 0 | - | 55.2 / 25.4 / 30.6 / 23.3 / 0 | +-5 of baseline, held <= 15 |
| Expert | 54.2 / 22.2 / 32.7 / 54.3 / 0 | 54.4 / 24.8 / 31.4 / 55.1 / 0 | 51.8 / 22.7 / 31.0 / 54.1 / 0 | - | - | +-5 of baseline, held 0 |

Camp trio (BotDuel, episodes per round mean / camp_share_pct / camp_time_pct): baseline (7) 1.33 / 2.1 / 1.1; D50 value 10:
2.67 / 4.2 / 2.0; 11: 3.67 / 5.3 / 5.4; acceptance x12 at 11: 4.67 (sets 4.33/4.67/5.67/4.00) / 5.8-8.9 / 2.8-4.0;
`camp_max_lead_s` <= 18.1.

**Final knobs** (all inside the D50 (5) windows): Easy `DirectHitRadiusScale` 0.5, `FireReleaseConeScale` 1.3,
`FireHoldAfterLostSeconds` 0.25; Medium 2.0 / 1.2 / 0.15; Hard 1.0 / **1.0** / 0.08; Expert 1.0 / 1.0 / 0;
`DuelCampOverScore` **11** (header default and `DefaultGame.ini`). The POD / UPROPERTY defaults are the tuned Medium row.

**Findings for the architect.**
1. *Hard drift at the D50 values* (stop-and-report): `FireReleaseConeScale` 1.2 dropped Hard's hitscan accuracy 56.3 -> 33.4
   (-23) with `held_share` 49.8: the held trigger drops the direct-hit gate, and with the MG's 10 shots/s the held phase
   dominates the shot count at L_ItemTest's long sight lines. Hard release 1.0 (inside the window; = no hysteresis in
   this implementation) restores 53.6-55.2 with held 0. D50's §11 Hard row (1.2) therefore differs from the asset.
2. *Medium cannot meet its band inside the windows*: every tested point lies on one trade-off line - accuracy 18-21 %
   with trigger share 39-44 % and held share 32-50 %; the gate scale moves the first shot only, the release cone sets the
   held phase. `shots_per_kill` 60-96 (target 12-30) is MG-driven (hitscan lines are 88 % of the Medium shots, MG alone 4848 of 7556 at 15.7 %) and
   conflicts with `trigger_share` >= 45 for an automatic weapon. Easy reaches its accuracy bands only at the cost of the
   trigger share (25.6 vs >= 50); with scale 0 (D50 value) it sprays at 7 % accuracy and 162 shots per kill.
3. *`acc_proj_pct`* is 16-28 % at every preset, baseline included (Expert 22 vs target 60-80): the gate never applied to
   splash weapons, so no D50 knob moves it; the projectile bands of (5) look miscalibrated for this map / predicate.
4. `FireReleaseConeScale` <= 1 is implemented as "no hysteresis" (the full rule, gate included) so that Expert's "1.0 = no
   hysteresis" holds; D50's literal wording (held: cone x scale *without* the gate) would give Expert gate-free held fire.
5. Hold-after-lost fires without an aim test (the aim model turns toward the last seen location); a corpse or a
   forgotten target gives no hold. The rocket-jump suppression releases the trigger; maneuver fire does not hold it.

**D51 inputs** (`bands.out`): lower bands, largest k with P(sum < k | unchanged) <= 2.5 % (bootstrap / Poisson ->
proposed min): BotRun Σ kills 24 / 20 -> 20 (current 12; per-round mean 10.08); BotDuel Σ frags 24 / 20 -> 20 (current
9; mean 10.0); BotDuel Σ camp episodes 9 / 7 -> 7 (current 2; mean 4.67); FFA Σ kills 86 / 74 -> 74 (current 70; sum
92, mean 9.2). Mega (D48 b): 26 timed returns, p(<= 6 s) 0.69, p(<= 3 s) 0.62 -> P(set fails | unchanged) 8.0 %
(binomial, 6 per set) / 12.1 % (bootstrap): **above 2.5 %**; BotDuel acceptance set 3 failed it (2 < 3), and tune2's
BotDuel set too. Upper bands re-checked: Σ stuck per set 3/0/0/0 and 2/1/3/0, FFA 3; per-round stuck max 2, bursts 0,
pocket 0 (baseline BotDuel had 1 pocket strike FLAG, before any change), `max_dwell_s` <= 7, link `dt` <= 1.83; no
`left_arena`, no world kill, no INVALID round. Report-only: clean `mean_us` 74.5-99.0 per round; `max_fire_us` over
2 ms in 13 % (BotRun) / 6 % (BotDuel) of the clean windows (D14 FLAG threshold a quarter: not raised); camp share
3.3-10.0 %.

**Budget.** Two acceptance rounds ran in the VM's slow state: BotDuel set 1 round 1 throughout (`frame_other_ref` 2390
vs ~670 in the other rounds; every category incl. the non-bot frame x3-5, rr ~1; 24 windows printed as cost because the
D47 reference is taken inside the slow state; the set excluded it as "slow throughout") and BotDuel set 2 round 3 (23 of
26 windows process-slow P, cost not judged by the clean-share rule). Their all-window `mean_us` is 397.5 / 412; not bot
cost. Every cost-judged round has clean `mean_us` <= 99 (all <= 300).

**Engine-side assumptions verified:** `confirm_victims` counts every non-owner victim, direct or splash (read in
`VeloxProjectile.cpp` `ApplyDamage`); `VeloxPlayerLabel` is the PlayerState name the runner already keys on; the
`velox.bots.*` cvars are generated from the field table (three new entries); unsaved UPROPERTYs take the C++ default
(the Medium asset stored no D50 values until tuned away from the default). Scope note: one line in
`Weapons/VeloxGameplayAbility_FireHitscan.cpp` (`Request.WeaponTag = Definition.WeaponTag;`) fills the request field the
subsystem logs.
