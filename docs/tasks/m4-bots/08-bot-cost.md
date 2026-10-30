# 08 — Bot CPU cost on a quiet host

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** done (diagnosis and report-only counters; the cost FAIL is not bot work, see outcome)
**Depends on:** 07b, the M4-07 acceptance rerun on m4-bots 7ff5061 (D46 1c cost FAIL); ADR 0005 D14, D36, D40, D46

## Goal
The idle-host M4-07 rerun passed every behaviour band but failed the cost band: BotRun had 2–6 cost windows per round
(clean means 227/127/147 us, D14 flag: `max_path_us` 36 ms, `max_fire_us` 18 ms), and BotDuel had cost windows from
about window 10 to the end of rounds 1 and 3 (`mean_us` 380–577, `over_share` 14–30 %), with no host cause (rq_wait ≈ 0,
steal 0, PSI < 0.5 %). Find where the time goes per bot per tick by category, compare with a pre-04g head (09a5aec), and
fix the cost with the smallest correct change that leaves decisions, tunables and the D36/D46 bands untouched. If the
fix needs a design change, report it to the architect.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/*` — report-only cost counters; a cost fix if it is in bot code
- `Source/Velox/Tests/*` — tests for the counters
- `infra/ci/nettest.sh` — report fields only (no verdict change)
- `docs/tasks/m4-bots/08-bot-cost.md`, `docs/tasks/m4-bots/README.md`

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §13, D14, D36, D40, D46
- `infra/ci/README.md` (runner, window classes)

## Acceptance criteria
- [x] Per-category breakdown of the bot tick (perception traces, path queries / router, segment check, aim/fire,
      follower, overlay, off-mesh recovery) and of the server frame, before/after, clean vs cost windows
- [x] Regression comparison against 09a5aec with the same runner
- [x] `infra/ci/build.sh` CI OK; `-DisableAdaptiveUnity` build OK; `fts.sh` OK 7/7
- [ ] BotRun ×3 and BotDuel ×3: set behaviour PASS and set cost PASS — **not reachable by a bot change**: the cost
      windows are the whole game thread running 4–5× slower, bots and engine alike (outcome)
- [x] Ten-run FFA set unchanged within D36 (the change is report-only)

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`; `Build.sh VeloxEditor Linux Development -DisableAdaptiveUnity` → success.
2. `infra/ci/fts.sh --port 7993` → `[fts] OK 7/7`.
3. `nettest.sh --port 7991 --scripts BotRun --repeat 3 --keep-going`, same for `BotDuel`; read the new
   `. cost breakdown` line per round.
4. Ten-run FFA set (120 s, 3 Medium, seeds 1–10) with the 04i-B runner (`ffa.sh`, `summary.py`, `set.py`).

## Report back
Standard dev-hard report.

## Notes / outcome
**New report-only counters.**
- `VeloxBotCost` line after every `VeloxBotBudget` line. It gives the mean wall clock per bot tick and the window
  maximum for twelve categories, in microseconds. The top-level scopes partition the controller tick: `super`
  (`AAIController::Tick`), `perceive`, `decide` (item router), `move` (`EnsureMove`), `follow` (`Navigate`, i.e. the
  path follower and the stuck rules), `overlay` (combat strafe and dodge), `act` (aim, weapon choice, fire rule) and
  `apply` (`ApplyInput`). Four further scopes are nested in their parent: `traces` (LOS traces, in `perceive`),
  `offmesh` (`UpdateOffMeshRecovery`, in `move`), `segment` (the D35 check, in `follow`) and `fire`
  (`ProcessAbilityInput`, in `apply`).
- The same line carries the server frame's phases per frame: `frame_us`, `frame_dispatch_us`, `frame_actors_us`,
  `frame_flush_us`, `frame_bots_us`, and `frame_other_us` (= frame − bots). It also carries fixed calibration work timed
  once per window: `calib_alu_us`, `calib_mem_us`, `calib_alloc_us`, `calib_clock_us` (about 0.2 ms per 5 s, outside
  the bot ticks).
- `nettest.sh` prints `. cost breakdown` per round: each field as clean|other window means, and the slowdown factor of
  the bots and of `frame_other` (no verdict change).
- `Velox.Bots.CostCategories` tests the field names.

**Breakdown (head ec20a6e + counters, idle host).** Units are µs per bot tick; clean window → slow window.

BotDuel, 5 rounds:

| Category | Clean | Slow |
|---|---|---|
| super | 2.7 | 26 |
| perceive | 3.6 | 25 |
| traces (in perceive) | 1.9 | 10 |
| decide | 10 | 71 |
| move | 3.2 | 14 |
| offmesh (in move) | 0.6 | 3 |
| follow | 7.5 | 48 |
| segment (in follow) | 1.5 | 10 |
| overlay | 1.8 | 7.5 |
| act | 11 | 92 |
| apply | 48 | 150 |
| fire (in apply) | 23 | 67 |
| **bot tick total** | **≈ 90** | **≈ 430** |

The rest of the server frame in the same windows, in µs per frame (clean → slow):

| Phase | Clean | Slow |
|---|---|---|
| dispatch | 80 | 400 |
| actors | 550 | 2000 |
| flush | 60–200 | 1000 |
| `frame_other` slowdown | | ×3.9–4.5 |
| bots slowdown | | ×4.1–5.2 |

BotRun (3 Medium): the first windows run at 54–78 µs. After the step they run at 175–250 µs, with every category ×3–4.
These windows still count as *clean*, because `over_share` stays under 5 % at that load. So BotRun's "clean mean" of
145–165 µs already includes the slow state.

**Finding.** The step is not bot work:
- **The step itself.** In each round, after a random 10–60 s, every bot category rises 4–9× at once, and so does every
  engine phase. This includes AAIController::Tick, the CSV profiler's own bookkeeping, char movement, physics and
  ServerReplicateActors (seen in `csvprofile`).
- **The volume of work stays flat.** Paths/s, traces/s, navrays/s, actor counts and ASC ability, effect and tag counts
  do not change across the step.
- **The CPU time is real.** The game thread's user CPU goes from 7 % to 25–30 % of a core. Run-queue wait stays at 0,
  and syscalls, minor/major faults, context switches, TLB/IPI interrupts and steal do not change.
- **Calibration stays flat.** In-process calibration (ALU, random reads in 1 MB and 64 MB buffers, old vs fresh,
  malloc/free, clock reads) is unchanged. Probes pinned to the game thread's current CPU, and a long-lived 512 MB probe
  process, are also unchanged.
- **Hypotheses excluded:**
  - GC (no correlation);
  - the trace in-memory cache (`-traceautostart=0` still steps);
  - denormals (FTZ/DAZ on the game thread still steps);
  - the GAS state of one bot (both bots step together, and their ratio stays constant);
  - host SMT siblings (none visible in the guest).

**Regression check.** 09a5aec, same runner, same idle host:
- BotDuel: 17/19/19 cost windows out of 27, clean mean 77.5 µs.
- BotRun: 5/4/4 cost windows, clean mean 146 µs.

The head is the same (BotDuel 25/17/17 cost windows of 26, clean union 91 µs; BotRun 3/1/0, 155 µs). This was always
there; 04g, 04h and 04i-B did not cause it.

**Open: needs the human or the architect.**
- **Root cause without perf.** perf is not installed and `perf_event_paranoid` is 3. Candidates that fit the evidence
  are instruction-side or physical-placement effects specific to this process. kcompactd runs every ≈ 30 s on this VM:
  `pgmigrate_success` is 4.5e8, swap is 100 % full, and the CPU model is "QEMU Virtual CPU" with PTI and no PCID.
  Testing this needs root:
  - install linux-perf, set `perf_event_paranoid ≤ 1`, and `perf record` the server in both states; or
  - try `vm.compaction_proactiveness=0` or THP `never`;
  - check the hypervisor's KSM/THP settings.
- **D46 1c classifier: for the architect.** A cost window is today "bots' clock rose, host did not". The new
  `frame_other_us` slowdown factor is bot-independent evidence. When it rises by about the same factor as the bots,
  the window is a process slowdown, not bot cost.
- **D14 flag:** the 36 ms / 18 ms single ticks are stalls of the same thread, not path or fire cost.

**Verification (final head of this task, idle host, 2026-10-30/31).**
- `infra/ci/build.sh`: `[ci] CI OK`, 97 tests, 0 not successful (includes `Velox.Bots.CostCategories`).
- `-DisableAdaptiveUnity` VeloxEditor build: `Result: Succeeded`. `fts.sh --port 7993`: `[fts] OK 7/7`.
- BotRun ×3:
  - Set behaviour PASS, cost FAIL.
  - Cost windows 11/7/8.
  - Slowdown of other windows against clean: bots ×2.5–3.7, `frame_other` ×2.0–2.8.
- BotDuel ×3:
  - Set behaviour PASS, cost FAIL.
  - Cost windows 17/17/17 of 26; the union of the clean windows has a mean of 88 µs.
  - Slowdown: bots ×5.2–6.8, `frame_other` ×3.7–4.3.
- Ten-run FFA set: PASS. Σ stuck 3, at most 1 per run, bursts 0, pocket 0, `max_dwell` 6 (1 run ≥ 6),
  link `dt` ≤ 1.36 s, Σ kills 105, at least 5 per run.
