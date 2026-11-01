# 07e — Live confirmation sets on `L_DM1`, recipe-complete evidence, E4 re-run

**Milestone:** M5 First real map + characters
**Agent:** dev
**Status:** todo
**Depends on:** 07c (ADR 0006 E4), 07d (`--map` for DM1Run/FT), 13b merged (run on the final M5 code)

## Goal
ADR 0006 E4 (a)/(e): one more BotRun ×3 + BotDuel ×3 set and one FFA ×10 set on `L_DM1` on an idle host, with evidence that can be `--reeval`ed (D48 (4)(ii) recipe: server/client logs, `ffa-N.log`, stats, host.txt). Then re-run `docs/adr/evidence/0006-dm1-bands-e4/e4_bands.py` with the new directory added and report every band k. This brings the bot scripts to ≥ 12 rounds on the final layout and is the first half of the dock N trigger (ii).

## Scope — files/modules the agent may touch
- `docs/adr/evidence/0006-dm1-art-e4/` (new) — evidence, gzipped logs
- `docs/adr/evidence/0006-dm1-bands-e4/` — add the new basis to `e4_bands.py` input, new `.out`
- No band file change. If a lower band would be raised, or a set FAILs after the D49 one rerun, or a dock N trigger fires, report back instead (architect ruling).

## Acceptance criteria
- [ ] `nettest.sh --map L_DM1 --scripts BotRun --repeat 3 --keep-going` and the same for BotDuel: `bands=L_DM1`, set PASS (or report).
- [ ] `ffa.sh --map L_DM1 --repeat 10`: SET PASS (or report).
- [ ] `nettest.sh --reeval` / `ffa.sh --reeval` on the committed evidence reproduces the verdicts.
- [ ] E4 script output with the new basis; dock N pooled tally over the last two confirmation rounds.
- [ ] Idle host (no foreign UE process at start/end, logged in host.txt), soak lock, all own UE processes killed.
