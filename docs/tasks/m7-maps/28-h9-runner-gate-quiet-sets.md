# 28 (H9) — follow-ups from the H6 hazard review

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (M7 H9, 2026-11-14; sets host-loaded, see notes)
**Depends on:** H8
**Wave:** hazard track (lead-added 2026-11-13 from `docs/adr/reviews/0008-hazard-review.md` §Follow-up tasks)

## Goal
F9 (hard `hazard_death_self` gate in runners / band files per ADR 0008 §5.3), F15 (fill H3 task notes, record DM1 `--reeval`), then the F3 and F4 runs on a quiet host after H8 is merged. Defect details, rulings and proposed amendments (G1–G7, D52–D55, C12) are in `docs/adr/reviews/0008-hazard-review.md` — read it first.

## Scope — files/modules the agent may touch
`infra/ci/{nettest.sh,ffa.sh,ffa_summary.py,ffa_set.py,lib.sh}`, `infra/ci/bands/*`, `docs/tasks/m7-maps/08-h3-hazard-metrics.md`, evidence under `docs/adr/evidence/0008-hazard-track/`, this file

## Acceptance criteria
- [x] hazard gate judged in a dry run on a stored set; default nettest list, BotRun ×3 and BotDuel ×3 on L_ItemTest, FFA ×10 on L_DM1 inside their bands with per-link JumpPad numbers; one cost-judged set on a hazard map; Sacrarium BotRun ×3 (L_Sacrarium_Blockout) on a quiet host; per-map BotWatch idle-client park point (BotWatch.json parks it at (-2700,-1700,100), outside the Nemus and Fornax footprints → 5 world kills per BotRun round there; read the point from the map spec or a per-map override, DM1 unchanged); investigate the start-up segfault seen once in an H11 Nemus FFA run (log missing, see docs/tasks/m7-maps/30-h11-jump-gap-links.md notes). Quiet host = load1 < 1.5 for the run; if other projects keep the host busy, wait/retry with the nettest load gate and record the load.

## Notes / outcome
Engine 5.8.4, built from `origin/m7-maps` + this task's runner changes (C++ unchanged, so every run is the base code). Scratch/logs: `/mnt/oldroot/velox-scratch/m7-h9/`.
Host: load1 gate 1.5 (`VELOX_LOAD_GATE=1.5`); the host never stayed under 1.5 for a whole set (other projects), so every set is **host-loaded** (start/end load1 below).

**Runner work**
- F9: `BAND_HAZARD_SELF_SET_MAX` / `BAND_HAZARD_SELF_ROUND_MAX` judged when set (`ffa_set.py`: FFA x10 sum; `nettest.sh`: per BotDuel round, any repeat count); `has_band` ignores `BAND_HAZARD_*`, so a file with only these keeps the other bands report-only. `bands/L_Fornax_Blockout.sh` (new): 6 / 1. `bands/L_Orbita_Blockout.sh`: 10 / 2.
  Dry runs (`--reeval`): Fornax FFA x10 (m7-fornax/s1/ffa3) self = 1 <= 6 PASS; Fornax BotDuel x3 (s1/duel) self [0,1,0] PASS at 1, FAIL at limit 0 (round 2 named); Orbita FFA x10 (m7-h12/ffa4) self = 128 > 10 FAIL (stored set predates the hazard bot work). Live: Fornax BotDuel x3 self [0,0,0] gate PASS.
- BotWatch park point: `infra/ci/botwatch_park.py` reads `Tools/editor/maps/<slug>_layout.py` (spawn farthest from every item, floor Z + 100) and `nettest.sh` passes a per-round copy of BotWatch.json as `-VeloxScript`; L_ItemTest, DM1 maps and maps without a layout module keep BotWatch.json (rc 3). Fornax (1000,-500,100), Sacrarium (-780,-1600,100). Result: Fornax BotRun x3 and Sacrarium BotRun x3 `world_kills=[0,0,0]` (was 5 per round).
- F15: H3 notes filled, DM1 `--reeval` unchanged (`docs/adr/evidence/0008-hazard-track/h9-dm1-reeval.txt`).
- `ffa.sh`: engine stdout kept as `ffa-N.log.stdout`; a start-up crash (exit >= 128) keeps its output as `crash1-ffa-N.*`, prints the newest `Saved/Crashes` dir and retries the run once.

**Quiet-host sets** (all host-loaded; cost verdicts only from rounds with >= 50 % clean windows; the A/B rule needs a base build, which is this build: a cost-only FAIL here is not attributed to H4/H5/H7 code)
| Set | load1 start/end | Result |
|---|---|---|
| default nettest list x1 | 1.26 / 2.54 (gate gave up at 1.82 and 5.71 for two rounds) | 15 PASS; FireRunControl FAIL (3 client switch retargets, 4 expected: FireRun.json phase, unrelated); BotRun, BotDuel cost FAIL only |
| BotRun + BotDuel x3, L_ItemTest (--keep-going) | 1.25 / 1.73 | behaviour PASS (both), cost FAIL (windows 13-19 of 20-27 clean, no host cause named, load 1.25-1.7); world_kills [0,0,0]; JumpPad 28/29 (96.6 %) and 32/33 (97.0 %) in the two BotRun/BotDuel tables, the known marginal pad is above 90 % (H4: 87.9 %) |
| FFA x10, L_DM1 | 1.49 / 1.89 | only `budget>300` on 6 of 10 runs (310-362 us); stuck 1, dwell max 5, Drop 95.8 % (138/144), **JumpPad 100 % (94/94)** (stored e4 set 92/92), item coverage 35/35, kills 82 >= 36, hazard self 0 |
| BotDuel x3, L_Fornax_Blockout (cost-judged hazard-map set) | 1.49 / 1.81 | rounds PASS, cost NOT JUDGED (5-12 of 26 clean windows), hazard gate PASS self [0,0,0]; set FAIL only on link_success[Jump] 81.0 % (Isle_In, the H14 short-gap issue) |
| BotRun x3, L_Fornax_Blockout | 1.18 / 2.21 | no world kills; cost FAIL (2-5 cost windows per round at 17/20 clean); Jump 86.5 % (H14) |
| BotRun x3, L_Sacrarium_Blockout | 1.41 / 1.35 | **PASS 3/3**, behaviour and cost PASS (kills 8/5/5, world_kills [0,0,0]) |
| FFA x10 at 3, L_Nemus_Blockout (crash repro) | 1.45 / 1.66 | no crash in 10 runs; world_kill 0; Jump 100 % (42/42), JumpPad 100 %; fails only `budget>300` (8 runs) and item_coverage 31/41 (B2, not H9) |

**H11 start-up segfault:** `run 1 exit=139` in `nemus-ffa3.out` (m7-h11, 2026-11-13 13:37) was the first Nemus run after a fresh build; stdout/stderr went to /dev/null and the retry overwrote `ffa-1.log`, so no evidence survives. Not reproduced in 10 runs here (and the 10 runs of the first batch: none). Capture added to `ffa.sh` (above); if it recurs the crash dir and stdout are kept. Open, no root cause.
