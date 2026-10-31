# 04 — Soak runner: `--map` override, per-map band files, map-independent invariants

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0006 §7; ADR 0005 D36, D47–D49, D51 (the current bands and labels)

## Goal
The bot soaks can run on any map. Bands live in per-map files; a map without a file runs report-only; the map-independent invariants (left_arena, world_kill, no_nav_point, giveup, stuck/burst/dwell, nav build time, link success, item coverage) are judged on every map. The `L_ItemTest` results do not change.

## Scope — files/modules the agent may touch
- `infra/ci/nettest.sh`, `infra/ci/ffa.sh`, `infra/ci/bands/L_ItemTest.sh` (new), `infra/ci/lib.sh`, `infra/ci/README*` or the header comments that document the runner
- `docs/tasks/m5-map-characters/notes/04-report.md`

## Relevant ADRs / references
- ADR 0006 §7 (what is hard everywhere, what is per map), ADR 0005 D36 (FFA set), D49 (`left_arena`/`world_kill`), D51 (band table), `docs/adr/evidence/0005-D51-single-shot/` (the current numbers)
- `infra/ci/nettest.sh` lines ~214–240 (map selection per script), `infra/ci/ffa.sh` line 18 (map URL)

## Acceptance criteria
- [ ] `nettest.sh --map <MapName>` and `ffa.sh --map <MapName>` replace the map part of the URL for `BotRun`, `BotDuel` and the FFA set while keeping the `?game=…?Bots=…` options; other scripts ignore the flag (logged once). Default behaviour unchanged.
- [ ] `infra/ci/bands/L_ItemTest.sh` holds the D51 set bands (BotRun Σ kills ≥ 20, BotDuel Σ frags ≥ 20 and Σ camp episodes ≥ 7, FFA Σ kills ≥ 74, Mega returns ≥ 1 within 3 s and ≥ 2 within 10 s, plus the D48 ≤ 6 s band) as shell variables; the runner sources `bands/<Map>.sh` when present, else prints `band=none (report-only)` and still prints every metric.
- [ ] Map-independent invariants judged on every map (hard FAIL regardless of band file): `left_arena 0`, `world_kill 0`, `no_nav_point 0`, `giveup 0`, FFA set Σ stuck ≤ 5 and bursts 0, max dwell ≤ 5 s, `VeloxNav built … seconds=` ≤ 2.0, link traversal success ≥ 90 % per link type (from the existing `link=` / timeout lines; if a success counter is missing in the logs, add the log line in `Bots/` — report it, do not guess), and **item coverage**: every `AVeloxPickup` present at Live start (`VeloxPickupReset` lines) has ≥ 1 take over the FFA ×10 set; untaken items are listed by name and location.
- [ ] Regression: `nettest.sh` default list unchanged (`BotRun`, `BotDuel` PASS with the `L_ItemTest` band file on an idle host), `ffa.sh` ×3 (not ×10 — time) prints the same metrics as before plus the new invariants.
- [ ] Header comments of both scripts document the flag and the band-file convention.

## Verification steps
1. `shellcheck infra/ci/nettest.sh infra/ci/ffa.sh infra/ci/bands/L_ItemTest.sh` (or `bash -n` if shellcheck is absent).
2. Idle host, under the soak lock: `nettest.sh --scripts "BotRun BotDuel"` → PASS, output includes `bands=L_ItemTest`.
3. `nettest.sh --map L_CombatTest --scripts "BotRun"` → runs on `L_CombatTest`, prints `band=none (report-only)`, judges the invariants (expect item coverage trivially empty there).
4. `ffa.sh --repeat 3` → metrics plus the invariants table.

## Report back
Standard format plus the two band lines and the invariants table from step 4.

## Notes / outcome
