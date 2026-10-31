# 04b — Soak runner: per-instance item coverage (`at=`), link-success definition by `reason=`, exemption scope

**Milestone:** M5 Map + characters
**Agent:** dev
**Status:** done
**Depends on:** 04 (runner), ADR 0006 E0.3 (a)–(c)

## Goal
The item-coverage invariant judges every pickup *instance* (DM1 has 2 YA, 6 shards, 5 × 25 HP, 8 × 5 HP), the link-success invariant counts `timeout`/`noimpulse`/`tooslow` as failures and excludes only re-plans, the minimum sample is 30 attempts per type, aborted outcomes are printed, and the coverage exemption is honoured for test maps only. `--reeval` of the stored D51 sets prints unchanged verdicts.

## Scope — files/modules the agent may touch
- `Source/Velox/Items/VeloxPickup.cpp` — only the three log lines `VeloxPickup`, `VeloxPickupReset`, `VeloxPickupRespawn`: append ` at=%s` with `GetActorLocation().ToCompactString()` (the format `VeloxPickupSpawn` already uses). No other change.
- `infra/ci/nettest.sh` (the embedded Python: link counting, `aborted=`, FLAG, `LINK_MIN_ATTEMPTS 30`), `infra/ci/ffa_summary.py` (parse `at=`, link reasons), `infra/ci/ffa_set.py` (coverage by instance, exemption scope, `INV_LINK_MIN_ATTEMPTS 30`), `infra/ci/lib.sh` (only if `ci_load_bands` must expose the map name), `infra/ci/bands/L_ItemTest.sh` (comment per exempt item), `infra/ci/README.md` and the script headers
- `docs/tasks/m5-map-characters/notes/04b-report.md`, this file

## Relevant ADRs / references
- ADR 0006 §7 and **E0.3 (a), (b), (c)**; ADR 0005 D28 (`Timeout`, `BelowStart`), D36 (set method, tail rule)
- `Source/Velox/Bots/VeloxBotController.cpp::NotifyLinkFinished` (`result=` is `ok|missed|aborted`; `reason=` is `LexToString(EVeloxManeuverResult)`: `missed noimpulse tooslow ineligible timeout below_start`, or `replaced`), `Source/Velox/Bots/VeloxBotManeuver.h` (the enum)
- `docs/adr/evidence/0005-D51-single-shot/` (the `--reeval` sets)

## Acceptance criteria
- [x] **`at=` on the pickup lines.** `VeloxPickup item=… take=<n> at=X,Y,Z` (trailing field), the same on `VeloxPickupReset` and `VeloxPickupRespawn`. Build green; `Velox.Items.*` tests and `fts.sh` (`FtItemPickup` parses nothing positional after `take=` — verify) pass.
- [x] **Coverage by instance.** `ffa_summary.py` records `(definition, at)` for resets and takes when `at=` is present; `ffa_set.py` judges per instance in that case and falls back to per definition when any run lacks `at=` (old logs). Output: `item_coverage <taken>/<present> pickups (<d> definitions)`; untaken instances listed as `<definition>@(X,Y,Z)`. `nettest.sh`'s pickup counters are unaffected.
- [x] **Exemption scope.** `BAND_FFA_COVERAGE_EXEMPT` is applied only when the map is `L_ItemTest`, `L_CombatTest` or `L_MovementTest`; on any other map it is ignored with a printed warning (`exemption ignored on <Map>: test maps only (ADR 0006 E0.3 a)`). The `L_ItemTest` band file comments name the two items and E0.3 (a)'s reasons.
- [x] **Link definition.** In both runners: per type, `ok` from `result=ok`; **failed when `reason` ∈ {missed, timeout, noimpulse, tooslow}**; **not an attempt when `reason` ∈ {replaced, below_start, ineligible}** (counted as `aborted=<n>` per type and printed); unknown reasons count as failed and are printed. Success = ok / (ok + failed). `LINK_MIN_ATTEMPTS` / `INV_LINK_MIN_ATTEMPTS` = 30. `FLAG` printed when `aborted ≥ ok` for a type over a set (report-only, no FAIL).
- [x] **Re-evaluation unchanged.** `nettest.sh --reeval <set>/nettest` for `botrun-medium`, `botduel`, `botrun-easy` and `ffa.sh --reeval <ffa dir>` of `docs/adr/evidence/0005-D51-single-shot`: every round and set verdict line byte-identical to the task-04 output; only the link lines gain `aborted=` and the sample note, and `item_coverage` keeps its per-definition figure (old logs have no `at=`). Paste the diff.
- [x] **Live.** Idle host, soak lock: `ffa.sh --repeat 1` on `L_ItemTest` shows `at=` in the log and the per-instance coverage line (`n/j`, 1 run); `nettest.sh --scripts BotRun` PASS with the new link line.
- [x] Header comments of both scripts and `infra/ci/README.md` state the definition (E0.3 b), the 30-attempt minimum, the per-instance rule and the exemption scope.

## Verification steps
1. `infra/ci/build.sh`; `infra/ci/fts.sh` → `OK 7/7`.
2. `shellcheck infra/ci/nettest.sh infra/ci/ffa.sh infra/ci/bands/L_ItemTest.sh` (or `bash -n`); `python3 -m py_compile infra/ci/ffa_summary.py infra/ci/ffa_set.py`.
3. The four `--reeval` commands; `diff` against the task-04 outputs (`Saved/CI` of task 04 if still present, else re-run `--reeval` on the pre-change commit first and keep its output).
4. `flock -o "$SOAK_LOCK" infra/ci/ffa.sh --repeat 1`; `flock -o "$SOAK_LOCK" infra/ci/nettest.sh --scripts BotRun`.

## Report back
Standard format plus the `--reeval` diffs and the live invariants table.

## Notes / outcome

Done 2026-10-31 (dev). `at=` added to the three pickup log lines (log-line change only). Runners judge links by `reason=` (failed: missed/timeout/noimpulse/tooslow/unknown; not an attempt: replaced/below_start/ineligible, printed as `aborted=<n>`, FLAG when aborted >= ok), minimum 30 attempts, per-instance item coverage with per-definition fallback on old logs, exemption honoured on L_ItemTest/L_CombatTest/L_MovementTest only (otherwise `WARNING: exemption ignored on <Map>: test maps only (ADR 0006 E0.3 a)`). `--reeval` of the D51 sets: all round/set verdict lines unchanged; only the link lines gain `aborted=` and the sample note. Build 107 tests OK, fts OK 7/7, live FFA x1 on L_ItemTest shows `at=` and `18/23 pickups (23 definitions)`, BotRun PASS. Deviation: the `item_coverage` row of the FFA table in instance mode reads `n/m pickups (d definitions)` and the link row gains `aborted=<n>`; unknown link reasons are reported in a `link unknown reasons` line (new). Report: none written (returned inline).
