# 07b — Runner host evidence, per-window classifier, Mega/camp/snap rules (ADR 0005 D46)

**Milestone:** M4 Bots
**Agent:** dev
**Status:** done
**Depends on:** 07 (tooling), ADR 0005 D36, D39, D40, D42, D46 (items 1, 3, 4, 5)

## Goal
The M4-07 acceptance reached no verdict because D40's classifier read only the bots' own wall clock. After this task the runner records host evidence per round (sidecar), classifies every `VeloxBotBudget` window as clean / contended / cost, judges the cost bands on the clean windows and the behaviour bands on every round, gates each round on the 1-min load average, re-specifies the Duel Mega band on the set, prints camp episodes and the possession snap, and can replay saved logs with `--reeval <dir>`.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotSubsystem.{h,cpp}` — `VeloxBotBudget` gains `frames=` (GFrameCounter delta) and `stalls=` (ticks of any kind > 10 ms); no behaviour change.
- `infra/ci/nettest.sh` — host sidecar, load gate, classifier, Mega/camp/snap rules, set evaluation, `--reeval`.
- `infra/ci/README.md` — document all of it.
- `docs/tasks/m4-bots/07b-runner-host-classifier.md`, `docs/tasks/m4-bots/README.md`.
(Not touched: the map script, `L_ItemTest`, `VeloxBotFunctionalTest.cpp` — another task.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` D46 items 1(a-g), 3, 4, 5; D36, D39, D40, D42.

## Acceptance criteria
- [x] `infra/ci/build.sh` CI OK.
- [x] `nettest.sh --reeval <dir>` on the three saved runs prints per-window classes and set verdicts; old Budget lines (no `frames=`/`stalls=`) and missing sidecars degrade to `n/a`.
- [x] One live `nettest.sh --port 7961 --scripts BotRun` round shows `frames=`/`stalls=`, the `BotRun-host.log` sidecar and the classification.
- [x] `bash -n infra/ci/nettest.sh` clean.

## Verification steps
1. `bash -n infra/ci/nettest.sh`; `infra/ci/build.sh`.
2. `infra/ci/nettest.sh --reeval <saved nettest dir>` for the BotRun set, the BotDuel set and the default-list run.
3. `flock <lock> env VELOX_LOAD_GATE=99 infra/ci/nettest.sh --port 7961 --scripts BotRun`.

## Report back
Standard dev format.

## Notes / outcome
See the report in the lead's session; key design points:
- Window classes: clean iff `over_share <= 5 %` and `frame_deficit <= 5 %`; `over_share > 5 %` is contended iff the host agrees (`rq_wait_pct > 2`, `steal_pct > 2`, `majflt > 20`, `psi_pct > 5`), a cost window when the sidecar exists and does not agree, and `unverified` when there is no host data (old logs): neither clean nor a cost FAIL. A frame-deficit-only window is contended (host agrees) or `frames`.
- Host figures per window are the sidecar's cumulative counters prorated over the window (5 s samples).
- Cost judged on clean windows when >= 50 % of a round's windows are clean; the set cost is NOT JUDGED when two or more rounds are not judged (D46 5), and the union of the clean windows is printed as information for the lead's fallback decision.
- Duel Mega: timed returns only (takes after a `VeloxPickupRespawn`); per round >= 1 <= 6 s; set: sum within 4 s >= 3 and >= 1 within 3 s; `first_after_live_s` reported.
- Camp: episode = consecutive `camp=1` route lines of one bot for one item (a `camp=0` line in between does not end it); share over `VeloxBotRoute bot= goal=` lines.
- Possession snap: corrections with t < 2 s are excluded from `max` and printed as `possession_snap_cm`.
