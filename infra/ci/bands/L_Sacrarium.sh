# shellcheck shell=bash disable=SC2034
# Soak bands of Sacrarium: ADR 0008 G1 (task 22, B6, 2026-11-20), ADR 0006 E3 / D51 (1) method: k = largest value with
# P(false FAIL | unchanged bots and layout) <= 2.5 % under Poisson and under a bootstrap over the observed runs, the smaller (per-run
# minima family-wise over the ten runs; Mega per run binomial over its four 35 s cycles). Basis A = FFA x10 at 6 bots on the final
# layout (b35c294, task 31) with the current bots (H15); basis B = A + two H15 sets on the pre-31 layout (pedestals only). The more
# lenient of A and B is the band. Derivation: docs/adr/evidence/0008-bands-b6/b6_bands.py -> b6_bands.out.
# Bot count (G1 c): a band holds only at the bot count of its basis or more (counts grow with bots). ffa.sh's --bots value (BOTS)
# selects the block; nettest.sh BotRun runs with BOTS_COUNT. There is NO basis with the current bots at 3 bots (FFA x10 at 3 exists
# only with H10 bots, BotRun x3 only with H9/H10 bots on older layouts), so FFA at 3 bots and BotRun stay report-only here
# (candidate FFA-at-3 values from H10 bots in b6_bands.out, not set). No BotDuel is part of Sacrarium's soak (ADR 0008 5.3).
# The map-independent invariants (left_arena, world_kill, no_nav_point, giveup, stuck sums/bursts, dwell <= 5 s, nav build, link
# success >= 90 % per type, item coverage 38/38) are NOT in this file and cannot be loosened here. No hazard on Sacrarium.
# Re-derive (G1 e): after a layout-table or bot-knob change, or once a current-bot FFA x10 at 3 and a BotRun x3 exist.
VELOX_BAND_FILE="L_Sacrarium"
_velox_ffa_bots="${BOTS:-3}"

if [ "$_velox_ffa_bots" -ge 6 ] 2>/dev/null; then
  # --- FFA set at >= 6 bots (ffa.sh --bots 6, 120 s, seeds 1-10); observed set sums 236 (A, final) | 240, 235 (B, pre-31 H15);
  #     the dropped RA-pedestal try of task 31 (226) is not in either basis
  BAND_FFA_KILLS_MIN=206          # sum of kills over 10 runs (A 206, B 207; P(false FAIL) 2.2 %); mean 23.6 per run
  BAND_FFA_RUN_KILLS_MIN=11       # kills in every run (observed 19-30 over 30 runs; P(false FAIL) 1.4 % over ten runs)
  BAND_FFA_RUN_PICKUPS_MIN=86     # pickups in every run (observed 105-122; P(false FAIL) 2.1 %)
  BAND_FFA_RUN_MEGA_MIN=2         # Health.Mega takes in every run (observed 4 in 30 of 30 runs; binomial at the Wilson lower p, 1.0 %)
fi
unset _velox_ffa_bots
