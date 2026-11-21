# shellcheck shell=bash disable=SC2034
# Soak bands of Culmen: ADR 0008 G1 (task 22, B6, 2026-11-20), ADR 0006 E3 / D51 (1) method: k = largest value with
# P(false FAIL | unchanged bots and layout) <= 2.5 % under Poisson and under a bootstrap over the observed runs / rounds, the smaller
# (per-run / per-round minima family-wise over the set; Mega binomial over its 35 s cycles). Basis A = run3 of task 19b on the final
# layout (111166b) with the current bots (H15); basis B = A + run2 (kiosk layout, pre-19b items: an items-only difference, same bots).
# The more lenient of A and B is the band. Derivation: docs/adr/evidence/0008-bands-b6/b6_bands.py -> b6_bands.out.
# Bot count (G1 c): a band holds at the bot count of its basis or more (counts grow with bots). ffa.sh --bots (BOTS) selects the FFA
# block, nettest.sh --bots (BOTS_COUNT) the BotRun block. FFA x10 at 8 bots (ADR 0008 5.3, report-only, cost not judged) gets the
# 6-bot bands, which it clears by a wide margin (observed set sums 350 / 355); its own values are in b6_bands.out, not set.
# Item coverage at 3 bots: the lead decision of 2026-11-20 (STATUS.md, task 19 (e)) makes the 3-bot coverage gate report-only for
# Culmen (43/46 in the final set; the misses move between runs). Item coverage is a map-independent invariant judged by ffa_set.py;
# a band file cannot exempt a real map (BAND_FFA_COVERAGE_EXEMPT is honoured on test maps only), so an FFA x10 at 3 bots on Culmen
# still prints item_coverage FAIL and the lead reads that line as report-only under the decision (G4). 6- and 8-bot coverage (46/46)
# and every other invariant stay gating. allunreach_air (flagged in most Culmen runs) is a diagnostic, never banded (G3).
# Re-derive (G1 e): after a layout-table or bot-knob change, or once 12 current-bot BotRun rounds exist.
VELOX_BAND_FILE="L_Culmen"
_velox_ffa_bots="${BOTS:-3}"
_velox_net_bots="${BOTS_COUNT:-3}"

if [ "$_velox_ffa_bots" -ge 6 ] 2>/dev/null; then
  # --- FFA set at >= 6 bots (120 s, seeds 1-10); observed set sums 213 (A) | 207 (run2)
  BAND_FFA_KILLS_MIN=182          # sum of kills over 10 runs (A 185, B 182; P(false FAIL) 2.3 %); mean 21.0 per run
  BAND_FFA_RUN_KILLS_MIN=9        # kills in every run (observed 15-27 over 20 runs; A 10, B 9; P 1.1 % over ten runs)
  BAND_FFA_RUN_PICKUPS_MIN=82     # pickups in every run (observed 98-120; P 2.5 % / 1.9 %)
  BAND_FFA_RUN_MEGA_MIN=1         # Health.Mega takes in every run (observed 3-4; A 1, B 2; binomial at the Wilson lower p, 0.1 %)
elif [ "$_velox_ffa_bots" -ge 3 ] 2>/dev/null; then
  # --- FFA set at 3-5 bots (the 3-bot basis); observed set sums 57 (A) | 58 (run2)
  BAND_FFA_KILLS_MIN=43           # sum of kills over 10 runs (A 43, B 43; P(false FAIL) 2.3 % / 2.0 %); mean 5.7 per run
  # BAND_FFA_RUN_KILLS_MIN deliberately unset at 3 bots: at 5.7 kills per run no per-run floor >= 1 meets 2.5 % over ten runs (DM1 alike)
  BAND_FFA_RUN_PICKUPS_MIN=44     # pickups in every run (observed 58-76 over 20 runs; A 45, B 44; P 1.9 %)
  BAND_FFA_RUN_MEGA_MIN=1         # Health.Mega takes in every run (observed 3-4; P 0.2 %)
fi

if [ "$_velox_net_bots" -ge 3 ] 2>/dev/null; then
  # --- BotRun (nettest.sh, 3 Medium bots, 90 s): A rounds kills 6 7 5, pickups 66 54 61, Mega 3 4 3, links ok 18 25 26;
  #     run2 rounds 8 7 8 / 49 49 67 / 3 3 3 / 26 18 22 (6 rounds; E4 wants 12 -> re-derive then)
  BAND_ROUND_KILLS_MIN=1          # kills by bots in every round (the runner default, stated; P(round < 1) 0.3 % over three rounds)
  BAND_BOTRUN_PICKUPS_MIN=40      # pickups by bots in every round (A 43, B 40; P 1.8 %)
  BAND_BOTRUN_LINK_OK_MIN=12      # VeloxBotLink result=ok in every round (A 12, B 12; P 1.3 % / 1.7 %)
  BAND_BOTRUN_KILLS_MIN=10        # BotRun sum of kills over the set of 3 (A 10, B 12; P 1.5 %)
  # BAND_BOTRUN_MEGA_REQUIRED unset: 3-4 takes per round in 6 of 6 rounds, but with p at its Wilson lower limit over 6 rounds a
  # Mega-less round has P 1.9 % per round, 5.5 % over three; not supported at 2.5 % (a dead Mega route still shows in FFA_RUN_MEGA_MIN).
  # BAND_BOTRUN_MAJOR_REQUIRED unset: Quad and Haste spawn once per 90 s round and are taken once; under Poisson(1 per round) a set
  # of three without one has P = e^-3 = 5 %, so 6 of 6 rounds do not support it at 2.5 %; major coverage is printed (5/5 in both sets).
fi
unset _velox_ffa_bots _velox_net_bots
