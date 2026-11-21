# shellcheck shell=bash disable=SC2034
# Soak bands of Fornax: ADR 0008 G1/G2 (task 22, B6, 2026-11-20), ADR 0006 E3 / D51 (1) method for the lower bands: k = largest
# value with P(false FAIL | unchanged bots and layout) <= 2.5 % under Poisson and under a bootstrap over the observed runs / rounds,
# the smaller (per-run minima family-wise over the ten runs; Mega binomial; timed Mega returns binomial over 6 returns per set).
# Basis A = the final layout (d19266d, task 38) with the current bots (H15): FFA x10 at 3 (38 ffa-b), BotDuel x3 (m7-38).
# Basis B = A + the H15 FFA sets with one shard elsewhere (38 ffa-a) or the pre-38 items (H15 v2, fix) and the pre-H15 BotDuel sets
# (B4, b4b, h9): items-only differences / older bots (D40 caveat). The more lenient of A and B is the band.
# hazard_death_self (G2): upper bands replacing the provisional ADR 0008 5.3 gates (6 per FFA set, 1 per BotDuel round): K = the
# smallest value with P(false FAIL) <= 2.5 % under Poisson with the rate at its one-sided 95 % upper limit and under a bootstrap, the
# larger, over A and B. Derivation and detection power: docs/adr/evidence/0008-bands-b6/b6_bands.py -> b6_bands.out.
# Bot count (G1 c): the FFA lower bands hold at >= 3 bots, the FFA hazard band at <= 3 bots (self deaths grow with bots); ffa.sh
# --bots (BOTS) selects them. BotDuel is always 1 v 1. BotRun has NO basis with the current bots (only the B4 set, pre-H13 bots,
# which fails on the BotWatch park world-kill artefact): BotRun stays report-only, and BAND_ROUND_KILLS_MIN, which nettest.sh applies
# to BotRun kills and BotDuel frags alike, is left unset (the runner default 1 applies to both; the BotDuel set sum judges frags).
# The map-independent invariants (left_arena, world_kill, no_nav_point, giveup, stuck sums/bursts, dwell <= 5 s, nav build, link
# success >= 90 % per type, item coverage 30/30 at 3 bots) are NOT in this file and cannot be loosened here. Floor Z: the default 0
# (the lava bed at -60 is not a floor, ADR 0008 3.5).
# Re-derive (G1 e): after a layout-table or bot-knob change, or once a current-bot BotRun x3 exists (G1 f).
VELOX_BAND_FILE="L_Fornax"
_velox_ffa_bots="${BOTS:-3}"

if [ "$_velox_ffa_bots" -ge 3 ] 2>/dev/null; then
  # --- FFA set at >= 3 bots (120 s, seeds 1-10); observed set sums 141 (A) | 150, 157, 154 (B)
  BAND_FFA_KILLS_MIN=118          # sum of kills over 10 runs (A 118, B 127; P(false FAIL) 2.2 %); mean 14.1 per run in A
  BAND_FFA_RUN_KILLS_MIN=5        # kills in every run (observed 10-20 over 40 runs; P 1.7 % over ten runs)
  BAND_FFA_RUN_PICKUPS_MIN=57     # pickups in every run (observed 68-90; A 60, B 57; P 2.3 %)
  BAND_FFA_RUN_MEGA_MIN=2         # Health.Mega takes in every run (observed 4 in 40 of 40 runs; A 2, B 3; P 1.0 %)
fi
if [ "$_velox_ffa_bots" -le 3 ] 2>/dev/null; then
  BAND_HAZARD_SELF_SET_MAX=4      # hazard_death_self over FFA x10 (G2): 0 in 40 H15 runs (A: K 3), 4 in 80 runs with the pre-H15 sets
                                  # (B: K 4); P(false FAIL) 0.6 %. Caught: 1 self death per run (set mean 10) 97 %, the Orbita B5 / H12
                                  # broken-edge sets (128-182) always. Replaces the provisional 6.
fi

# --- BotDuel (nettest.sh, 1 v 1, Live phase): A frags 12 11 16, camp 6 8 6, timed Mega returns 6 of 7 within 3 s, 7 of 7 within 10 s;
#     B adds frags 10 11 12 12 9 11 9, camp 7 7 12 4, returns 8 of 9 / 9 of 9
BAND_BOTDUEL_FRAGS_MIN=23         # BotDuel sum of frags over the set of 3 (A 27, B 23; P(false FAIL) 2.0 %)
BAND_BOTDUEL_CAMP_MIN=12          # BotDuel sum of camp episodes (A 12, B 13; P 2.1 %)
BAND_MEGA_WITHIN3_MIN=1           # timed Mega returns within 3 s over the set (A 1, B 2; P 0.9 %)
BAND_MEGA_WITHIN10_MIN=2          # ... within 10 s (A 2, B 3; P 0.8 %)
BAND_HAZARD_SELF_ROUND_MAX=4      # hazard_death_self in every BotDuel round (G2): 0 in 3 H15 rounds (A: K 4), 1 in 10 rounds (B: K 3);
                                  # P(false FAIL) 1.1 % over three rounds; the B5 broken rounds (10 19 22) are all over it. Replaces the
                                  # provisional 1, which had P(false FAIL) about 23 % over three rounds at the B upper rate.
unset _velox_ffa_bots
