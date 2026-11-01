# 07c — DM1 band re-derivation over the 06b + 15 rounds

**Milestone:** M5 First real map + characters
**Agent:** architect
**Status:** todo
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
- [ ] Count the rounds actually available per script (BotRun, BotDuel, FFA) on the final layout and state it; if < 12 per bot script, say so and either derive on what exists with the reasoning or rule that more rounds are needed (and how many).
- [ ] D51 rules applied: a lower band never raised by a change that can only raise the count; every E3 band either confirmed or replaced with the derivation shown.
- [ ] Dock N links (DockW_N, DockE_N): ruling whether the per-record success rate needs a layout task, or stays a watch item, with a threshold.
- [ ] `nettest.sh --reeval` and `ffa.sh --reeval` of the 06b and 0006-dm1-art sets under the new `L_DM1.sh` still PASS (run them, CPU-only, no UE).

## Report back
Standard format; list every band number old → new.
