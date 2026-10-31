# 03 — Near-human bots: Easy/Medium spray, more Duel camping, measured targets

**Milestone:** M4 follow-ups (human decisions q24 and q28, 2026-10-30)
**Agent:** dev-hard (the lead may split step 1 — log fields + runner — to `dev`)
**Status:** todo
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
- [ ] **Step 1 (measurement, no bot change):** `VeloxShot` carries `by=<label> weapon=<tag leaf>` and `VeloxProjectileExplode` carries `by=<label>`; the runner prints per bot and per round (Live only) `acc_hitscan_pct`, `acc_proj_pct`, `shots_per_kill`, `trigger_share_pct`, `held_share_pct`, `acc_by_weapon`, `camp_time_pct`, and pools them on the set line with the D50 (5) band and `IN`/`OUT` (report-only); `--bot-difficulty <preset>` works and tags rounds `calibration=<preset>` with no D36/D48/D49 band judged. `nettest.sh --reeval` on `docs/adr/evidence/0005-D49-m4-final/` prints the same verdicts as before (`acc_*` = `n/a` there). `infra/ci/build.sh` CI OK, `infra/ci/fts.sh` 7/7.
- [ ] **Baseline table** (idle host): BotRun ×3 Medium, BotDuel ×3, BotRun ×3 `--bot-difficulty Easy`, BotRun ×3 `--bot-difficulty Expert` with the unchanged bots; the five quantities per preset recorded in the notes.
- [ ] **Step 2 (rule):** the three fields exist in the POD, the asset, the cvar table and `create_bot_content.py` with the D50 (2) values; `EvaluateFire` implements gate scale, release cone and hold-after-lost with ammo/range/readiness/self-splash never relaxed; `FireHeld` verdict logged as `fire=held`; the glue traces the aim ray while held with a splash weapon. `Velox.Bots.ShouldFire` has the D50 (2) rows a–e and passes; `Velox.Bots.AimError`/`AimLead`/`AimTurnRate` unchanged and green.
- [ ] `DuelCampOverScore` is 10 in the header default and `DefaultGame.ini`; `Velox.Bots.Decision` green (a row that read the old default is updated and noted).
- [ ] **Step 3 (tuning):** after at most two tuning iterations inside the D50 (5) windows, the four calibration sets show Easy and Medium inside their five bands (or the miss is reported with numbers and the knob position), Hard/Expert within ±5 points of the baseline accuracy with `held_share` ≤ 15 / 0, BotDuel camp episodes per round mean 3–5 and `camp_time_pct` 3–8.
- [ ] **Step 4 (acceptance rounds for D51):** BotRun ×12 Medium and BotDuel ×12 (four `--repeat 3` sets each, idle host, `left_arena=none`, no `INVALID`) plus the D36 ten-run FFA (120 s, 3 Medium, seeds 1–10); every round's `stats.json`, the set summaries and the per-round table go to `docs/adr/evidence/0005-D50-near-human/` by the D48 (4)(ii) recipe, with a committed derivation script that prints, for each lower band (BotRun Σ kills, BotDuel Σ frags, Σ camp episodes, FFA Σ kills), the largest k with P(Σ < k) ≤ 2.5 % under the empirical bootstrap and the Poisson model, and the re-estimated Mega p. Misses against the current D36/D48/D49 values are reported as numbers, not "fixed".
- [ ] Budget: `VeloxBotBudget mean_us` ≤ 300 in every round; `max_fire_us` reported (FLAG above 2 ms stays a flag).

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
