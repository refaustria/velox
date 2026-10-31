# 07d — Transition window class, Mega set band on ≤ 6 s, durable M4 acceptance evidence (ADR 0005 D48)

**Milestone:** M4 Bots
**Agent:** dev
**Status:** todo
**Depends on:** 07c (D47 classifier, `--reeval`), the final M4 acceptance run on 046dabb (lead's scratchpad `a07d47/`), ADR 0005 D46 (3), D47, D48

## Goal
The final M4 acceptance (046dabb, idle host) passed every band except two that D48 rules to be runner calibration:
a *transition* window (the thread's slow state began inside window 10 of BotDuel round 3) was classed `cost`, and the
Mega set band "Σ within 4 s ≥ 3" fails about one set in five with the bots unchanged (32 observed returns, p(≤ 4 s) =
0.59). After this task the runner (1) classes a transition window as process-slow, (2) judges the Mega band as
"Σ timed returns ≤ 6 s ≥ 3 and ≥ 1 ≤ 3 s in the set" with the per-round 6 s rule as a flag, (3) prints the cycle index
of every timed take, and (4) the a07d47 acceptance logs live in the repo as `docs/adr/evidence/0005-D48-a07d47/`, so
that `--reeval` on them is the record that closes M4. No engine code; no UE run.

## Scope — files/modules the agent may touch
- `infra/ci/nettest.sh` — `analyze_round` (window classes: D48 (a) branch; Mega per-round flag; `cycle=` on the timed
  take lines; `transition` flag on the window detail line and in `stats.json`), `bot_set_eval` (Mega set band D48 (b);
  `within_6s`, `first_cycle` / `later_cycle` print). Nothing else in the verdict logic changes.
- `infra/ci/README.md` — the "Host evidence and classification" paragraph (transition class) and the BotDuel Mega band
  text.
- `docs/adr/evidence/0005-D48-a07d47/` — new: the acceptance evidence (see step 1) with a `README.md` in the style of
  `0005-D47-pre09a5aec/README.md`.
- `docs/tasks/m4-bots/07d-transition-window-and-mega-band.md`, `docs/tasks/m4-bots/README.md`.
(Do not touch `docs/adr/evidence/0005-D47-pre09a5aec/d47_ref.py`: the architect already added the D48 (a) branch; port
its arithmetic, do not call it. No file under `Source/`.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` D48 (the rules and the expected numbers), D47 (a)–(f), D46 (3) (the Mega band this amends),
  D36 (set bands, tail rule).
- `docs/adr/evidence/0005-D47-pre09a5aec/d47_ref.py` — reference implementation of D47 (a)–(c) with the D48 (a)
  transition branch (`cls == "T"` printed as `P`); `README.md` there for the fixture numbers.
- `docs/tasks/m4-bots/07c-process-slow-class.md` — where the D47 code lives in `nettest.sh` (`analyze_round` l. ≈ 1470–1510,
  `bot_set_eval` l. ≈ 1765–1810 at 046dabb).

## Acceptance criteria
- [ ] **Evidence copied first.** `docs/adr/evidence/0005-D48-a07d47/{botrun,botduel,default}/` hold the three a07d47
      nettest directories (`20261031-075112-2982110`, `20261031-075729-2992329`, `20261031-080448-3004370`), each with
      its `round-k/` structure (the default list has none): server logs of the eight bot rounds filtered to lines matching
      `LogVelox|Error|ensure|Warning`, their client logs filtered to `Velox|Error|ensure`; every `.result`, `-gate.txt`,
      `-host.log`, `.stats.json` and the non-bot scripts' logs copied as they are only if < 50 KB each (otherwise filtered
      the same way); `all.out` at the top. `--reeval` on each copy prints the same summary lines as on the original
      directory (`diff` ignoring the `Logs:` line); if a filtered log changes a printed number, keep that round's full
      log and say so in the README.
- [ ] **D48 (a).** In `analyze_round`, for a window with `over_share > 5 %`, `agree` false, `R_other < R_OTHER_SLOW`:
      class `pslow` with `transition=1` iff `rr <= RR_WINDOW_MAX` and a neighbouring window (k−1 or k+1, same round)
      has `R_other >= R_OTHER_SLOW`; otherwise `cost` as before. Class char stays `P`; the window detail line carries
      `transition=1`; `stats.json` gains `transition_windows`. All other D47 branches unchanged.
- [ ] **D48 (b).** In `bot_set_eval` for `BotDuel`: `within6 = count(lag <= 6.0)`; FAIL iff `within6 < 3`
      ("Mega timed returns within 6 s: %d < 3") or `within3 < 1` (unchanged); the `within4 < 3` FAIL is removed and
      `within_4s` stays printed. The set line prints `within_6s=`, `first_cycle=[...]`, `later_cycle=[...]`. In
      `analyze_round` the per-round "no timed Mega return within 6 s" moves from `fails` to a printed `FLAG` line (the
      round verdict no longer fails on it).
- [ ] **Cycle index.** Each `mega timed take` detail line gains `cycle=<n>` (1 = the first `VeloxPickupRespawn` of the
      Mega after Live, counting respawns, not takes); the per-round `timed` entries carry the index so the set line can
      split them.
- [ ] **Re-evaluation of the copied evidence** (`VELOX_OUT=<scratch> infra/ci/nettest.sh --reeval <copy>`):
      - botrun: set behaviour PASS, cost PASS, cost windows [0, 0, 0], clean 5/10/10, P 15/10/10, `rr_round`
        1.37/1.31/1.18 (unchanged from `all.out`).
      - botduel: round 3 classes `[.........PPPPPPPPPPPPPPPPP]`, clean 9/26, P 17, cost 0, `transition=1` on window 10,
        `p_norm_mean_us` 126.6 ± 2, `rr_round` 1.43 ± 0.05; rounds 1–2 unchanged (P 17/17, `rr_round` 1.25/1.36); set
        `cost windows [0, 0, 0]`, `within_6s=4 within_3s=2 within_4s=2`, `first_cycle=[8.2,2.3,26.0]`,
        `later_cycle=[4.5,5.2,0.0]`, **set behaviour PASS, cost PASS, set PASS**.
      - default: all 13 scripts PASS (BotRun and BotDuel PASS, cost PASS), as in `all.out`.
- [ ] **No regression on old evidence.** `--reeval docs/adr/evidence/0005-D47-pre09a5aec --scripts BotRun,BotDuel`
      prints exactly the D47 (f) numbers (cost windows 0, clean 10/8/8 and 9/8/8, P 17/19/19 and 12/13/13, the same
      means and `rr_round`); the ten FFA logs (`a08/ffa/soak/ffa-1..10.log` in the lead's scratchpad, if still present)
      show no `P`, `X` or `transition` window.
- [ ] `bash -n infra/ci/nettest.sh` clean; `infra/ci/build.sh` → `[ci] CI OK` (standing rule; nothing compiled changes).

## Verification steps
1. Copy the evidence (acceptance criterion 1) and run `--reeval` on the copy and on the original; `diff` the summary
   output ignoring the `Logs:` line. Do this **before** editing `nettest.sh`, with the 046dabb runner, so the copy is
   validated against the published verdicts.
2. Edit `nettest.sh`; `bash -n infra/ci/nettest.sh`.
3. `VELOX_OUT=/tmp/<scratch>/d48 infra/ci/nettest.sh --reeval docs/adr/evidence/0005-D48-a07d47/botduel` → the numbers
   above; the same for `botrun` and `default`.
4. `python3 -I docs/adr/evidence/0005-D47-pre09a5aec/d47_ref.py 1 docs/adr/evidence/0005-D48-a07d47/botduel/nettest/round-3/BotDuel-server.log`
   → `classes [.........PPPPPPPPPPPPPPPPP] clean=9 P=17 X=0 … p_norm_mean_us=126.6 rr_round=1.43 cost=PASS`; compare
   with the runner's round-3 lines.
5. `--reeval` on the D47 fixture → D47 (f) numbers unchanged.
6. `infra/ci/build.sh` → `[ci] CI OK`.
7. Do not run live rounds: D48 (4) closes M4 on the re-evaluation. If step 3 prints anything outside the expectations,
   stop and report (the lead schedules D48's fallback, one live `BotDuel --repeat 3`).

## Report back
Standard dev format: result; what changed (functions touched, the new output fields); verification (commands + output
tail: the three set lines, round 3's class string and reference line, the fixture's six class strings, the `diff` of
step 1); the size of the evidence directory; open issues. State explicitly if Unreal was unavailable for step 6.

## Notes / outcome
(Filled by the agent or the lead after completion.)
