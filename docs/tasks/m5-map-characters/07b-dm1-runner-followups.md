# 07b — Runner follow-ups from E3 and the 06b review: map-present majors, `not_at_goal`, engine-noise fixture, `bands=` name

**Milestone:** M5 First real map + characters
**Agent:** dev
**Status:** done (2026-11-01)
**Depends on:** 07 (ADR 0006 E3; `infra/ci/bands/L_DM1.sh`, `L_DM1_Blockout.sh` landed), `docs/adr/reviews/0006-06b-review.md` (D3, D4)

## Goal
The DM1 band file can state `BAND_BOTRUN_MAJOR_REQUIRED=1` because the BotRun set judges the majors **present on the map**, not a fixed list of seven; the 3-D arrival's `not_at_goal` outcome is counted report-only by both runners; the engine-noise ignore of task 06b is exercised by a fixture; the set line of a map whose band file sources another map's file prints the file actually applied. No UE run: everything is verified by `--reeval` on the stored evidence and on fixtures.

## Scope — files/modules the agent may touch
- `infra/ci/nettest.sh` — `MAJOR_ALL` (two copies, lines ≈ 1393 and ≈ 2177) → the majors present: the definitions among the seven with a `VeloxPickupReset`/`VeloxPickupSpawn` line in the round's server log (the FFA mode shows Quad; Duel does not), `major_covered=n/m` with `m` the map's count and the same `missing=[…]` list; `not_at_goal=<n>` (count of `VeloxBotPath … result=not_at_goal`) on the detail line next to `no_clear_point`, in `stats.json`, summed on the set line; the printed band name (see below)
- `infra/ci/ffa_summary.py`, `infra/ci/ffa_set.py` — `not_at_goal` per run (`.json`) and the set sum in the SET line
- `infra/ci/lib.sh` `ci_load_bands` — when the band file sets `VELOX_BAND_FILE` (the DM1 files do: `L_DM1_Blockout.sh` sources `L_DM1.sh` and exports `VELOX_BAND_FILE=L_DM1`), print `bands=<map> (file L_DM1)`; otherwise unchanged
- `infra/ci/bands/L_DM1.sh` — uncomment `BAND_BOTRUN_MAJOR_REQUIRED=1` once the runner judges map-present majors (the value is E3's; nothing else in the file changes)
- `infra/ci/fixtures/noise/` — a copy of one 06b BotRun round (`round-1` of `dm1-botrun`, gunzipped `.result`/`.stats.json` untouched, server log with the exact line `LogClass: Error: StructProperty FDataflowToolNodeSnapshot::Date is not initialized properly` injected after `VeloxMatch phase=Live`) and one FFA log copy with the same injection
- `infra/ci/README.md` — the three paragraphs touched (bands, invariants, link/not-attempt list)

## Relevant ADRs / references
- `docs/adr/0006-first-map-and-characters.md` E3 (DM1 bands; the major rule; `not_at_goal` report-only), §7
- `docs/adr/reviews/0006-06b-review.md` D3, D4
- ADR 0005 D36 (every `bMajorItem` taken over the BotRun set), D51 (7)

## Acceptance criteria
- [ ] `VELOX_OUT=<scratch> infra/ci/nettest.sh --reeval docs/adr/evidence/0006-dm1-blockout-06b/dm1-botrun --map L_DM1_Blockout --scripts BotRun` prints `bands=L_DM1_Blockout (file L_DM1)`, `major_covered=4/4 [Armor_Red,Armor_Yellow,Health_Mega,Powerup_Quad]`, `set behaviour PASS`, `set PASS`, and every E3 band judged (no `band=none`, no `not judged` note for a banded quantity).
- [ ] The same on `dm1-botduel` prints `major_covered=3/3` (Duel: no Quad), `set PASS`; on `docs/adr/evidence/0005-D51-single-shot/` (L_ItemTest, default band file) the printed verdicts are unchanged and `major_covered=7/7`.
- [ ] `ffa.sh --reeval` on the gunzipped `dm1-ffa/logs` with `--map L_DM1_Blockout` prints `SET bands=L_DM1_Blockout`, `sum_kills=47 PASS`, every invariant PASS, `not_at_goal=0`.
- [ ] `nettest.sh --reeval infra/ci/fixtures/noise --map L_DM1_Blockout --scripts BotRun` prints the round PASS with `errors=0` (the injected line is ignored) and the README names the fixture; the FFA fixture prints `errors=0` likewise. A second copy of the fixture with the message altered by one character (`FDataflowToolNodeSnapshot::Dat `) FAILs with `1 Error:` — the ignore is exact.
- [ ] `not_at_goal=0` on every 06b round and run (`stats.json`, `.json`, set lines).
- [ ] No behaviour or band number changes anywhere else: `--reeval` of `0005-D51-single-shot/` and `0005-D49-m4-final/` prints the same verdict lines as before (diff ignoring the `Logs:` path and the new `not_at_goal=0` field).

## Verification steps
1. The five `--reeval` commands above with their expected lines quoted in the report.
2. `shellcheck infra/ci/lib.sh infra/ci/nettest.sh infra/ci/bands/*.sh` clean (or the pre-existing disables only).

## Report back
Standard dev report: the printed set lines for the three DM1 sets, the fixture results, the diff summary of the D51/D49 re-evaluations.

## Notes / outcome

**Outcome (2026-11-01, dev; no UE run, everything by `--reeval` / fixtures on gunzipped copies of the 06b evidence):**
- `nettest.sh`: majors judged = those with a `VeloxPickupReset`/`VeloxPickupSpawn` line in the round (`major_present` in `stats.json`, fallback to the seven for old stats); `major_covered=n/m`; `not_at_goal` counted (detail line, `stats.json`, `sums:` line); `bands=<map> (file <F>)`. `ffa_summary.py` / `ffa_set.py`: `not_at_goal` per run, in `.json`, on the SET line; `SET bands=` label. `lib.sh` exports `VELOX_BAND_LABEL`. `bands/L_DM1.sh`: `BAND_BOTRUN_MAJOR_REQUIRED=1`. `fixtures/noise/` + README paragraph.
- DM1 BotRun set: `bands=L_DM1_Blockout (file L_DM1)`, `major_covered=4/4 [Armor_Red,Armor_Yellow,Health_Mega,Powerup_Quad]`, set behaviour PASS, set PASS. BotDuel set: `major_covered=3/3`, set PASS. FFA x10: `SET bands=L_DM1_Blockout (file L_DM1)`, `sum_kills=47 PASS`, all invariants PASS, errors=0. D51 / D49 re-evaluations: identical verdict lines (only the new `not_at_goal=0` field); `0005-D49-m4-final/botduel` is a FAIL before and after (unchanged).
- Noise fixture: round PASS with no `Error:` note, FFA `errors=0`; one-character-altered copy: `1 Error:` FAIL / `errors=1`.
- **Deviation from the acceptance text "not_at_goal=0 on every 06b round and run":** the stored logs contain real `VeloxBotPath ... result=not_at_goal` lines: `dm1-botrun` round 3 has 2 (Celer, `failures=2` then `failures=3`, to=(108.66, 152.46, 157.46), t 04:16:57-58) and `dm1-ffa` run 8 has 1. The counter is right; E3's "expectation 0 on DM1" is not met by 06b's own evidence (report-only, no band). Left for the lead / architect.
