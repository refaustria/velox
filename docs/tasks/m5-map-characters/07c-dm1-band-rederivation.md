# 07c — DM1 band re-derivation over the 06b + 15 rounds

**Milestone:** M5 First real map + characters
**Agent:** architect
**Status:** done (2026-11-01, architect — ADR 0006 E4; `infra/ci/bands/L_DM1.sh` camp 21 → 22, FFA kills 34 → 36, every other band confirmed; evidence `docs/adr/evidence/0006-dm1-bands-e4/`)
**Depends on:** 07 (ADR 0006 E3 (g)), 15 (confirmation sets on `L_DM1`)

## Goal
ADR 0006 E3 (g) scheduled this: the DM1 bands in `infra/ci/bands/L_DM1.sh` were derived from six rounds per script. The 06b rounds (`docs/adr/evidence/0006-dm1-blockout-06b/`) and the task-15 rounds on the art map (`docs/adr/evidence/0006-dm1-art/`, same layout by `--diff-collision`) together are the basis for a D51 re-derivation. Result: an amendment E4 to ADR 0006 with the new or confirmed bands, and an updated `bands/L_DM1.sh` if a number moves. Read-only analysis; no UE run.

## Scope — files/modules the agent may touch
- `docs/adr/0006-first-map-and-characters.md` — new section E4
- `infra/ci/bands/L_DM1.sh` — band numbers only, with a comment citing E4
- `docs/adr/evidence/0006-dm1-bands-e4/` (new) — the derivation script/output (reuse `0006-dm1-blockout-06b/e3_bands.py`)

## Relevant ADRs / references
- ADR 0006 E2, E3; ADR 0005 D49, D51
- `docs/tasks/m5-map-characters/notes/15-report.md` (per-record link table; dock N at 86.6 % over 15 soaks is the watch item)

## Acceptance criteria
- [x] Count the rounds actually available per script (BotRun, BotDuel, FFA) on the final layout and state it; if < 12 per bot script, say so and either derive on what exists with the reasoning or rule that more rounds are needed (and how many). — 6 BotRun / 6 BotDuel / 20 FFA with the current bots (06b + 15), 9 / 9 / 30 with task 06 set 6 (D40); set 5 is not the final layout. < 12 for the bot scripts: derived now on the 9-round basis (E4 a), the 12-round basis arrives with one more BotRun ×3 + BotDuel ×3 confirmation on `L_DM1`.
- [x] D51 rules applied: a lower band never raised by a change that can only raise the count; every E3 band either confirmed or replaced with the derivation shown. — E4 (b) table; `e4_bands.out`. No count-raising change occurred (same layout, same bots), so the two raises (camp 22, FFA kills 36) are better estimation, not a violation of the clause.
- [x] Dock N links (DockW_N, DockE_N): ruling whether the per-record success rate needs a layout task, or stays a watch item, with a threshold. — watch item; pooled 119/132 = 90.2 % (06b 93.8 %, 15: 86.6 %, Fisher p = 0.24); thresholds E4 (d): FFA Drop < 93.1 % (E3 c), or dock N < 85 % over the two most recent confirmation rounds (n ≥ 120), or a second bot-set Drop FAIL in a row with ≥ half the misses on dock N after the D49 rerun.
- [x] `nettest.sh --reeval` and `ffa.sh --reeval` of the 06b and 0006-dm1-art sets under the new `L_DM1.sh` still PASS (run them, CPU-only, no UE). — 06b: BotRun `set PASS`, BotDuel `set PASS`, FFA `SET runs=10 PASS` (`reeval-*.out` in the evidence dir). **0006-dm1-art cannot be re-evaluated**: it holds no server/client logs and no `ffa-N.log` (the producing worktree is gone); judged by arithmetic on its committed per-round figures (BotRun 19 ≥ 11, BotDuel 12 ≥ 7, camp 36 ≥ 22, Mega 4 / 6, FFA 54 ≥ 36). E4 (e) makes the D48 (4)(ii) recipe a condition for future basis sets; STATUS open item for the lead.

## Report back
Standard format; list every band number old → new.
