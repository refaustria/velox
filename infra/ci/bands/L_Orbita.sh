# shellcheck shell=bash disable=SC2034
# Soak bands of Orbita: ADR 0008 G1/G2 (task 22, B6, 2026-11-20), ADR 0006 E3 / D51 (1) method for the lower bands: k = largest
# value with P(false FAIL | unchanged bots and layout) <= 2.5 % under Poisson and under a bootstrap over the observed runs, the
# smaller (per-run minima family-wise over the ten runs; Mega binomial). Basis A = FFA x10 at 4 bots on the final layout (d52e0a3 with
# the H12 nav tile) with the current bots (H15 v2); basis B = A + the H15 iteration set (fix) + four H13 sets on the same layout
# (60 runs, D40 caveat). The more lenient of A and B is the band.
# hazard_death_self (G2): upper bands replacing the provisional ADR 0008 5.3 gates (10 per FFA set, 2 per BotDuel round): K = the
# smallest value with P(false FAIL) <= 2.5 % under Poisson with the rate at its one-sided 95 % upper limit and under a bootstrap, the
# larger, over A and B. Derivation and detection power: docs/adr/evidence/0008-bands-b6/b6_bands.py -> b6_bands.out.
# Bot count (G1 c): the FFA lower bands hold at >= 4 bots, the FFA hazard band at <= 4 bots; ffa.sh --bots (BOTS, default 3: pass
# --bots 4 as the stored sets do) selects them. BotDuel is always 1 v 1. BotDuel lower bands: NO basis with the current bots (only
# the H13 set b5b) -> report-only (candidates in b6_bands.out: frags 4, camp 24, Mega within 3 s 2, within 10 s 2). BotRun is not
# part of Orbita's soak (ADR 0008 5.3) and has no band.
# The map-independent invariants (left_arena, world_kill, no_nav_point, giveup, stuck sums/bursts, dwell <= 5 s, nav build, link
# success >= 90 % per type, item coverage 37/37) are NOT in this file and cannot be loosened here.
# Re-derive (G1 e): after a layout-table or bot-knob change, or once a current-bot BotDuel x3 exists (G1 f).
VELOX_BAND_FILE="L_Orbita"

# Arena floor of the left-arena label (ADR 0008 3.5, the one E0.3 (e) exception; unchanged from task M7 S5). Orbita's lowest deck
# (Hortus) is at Z -320, the void hazard slab spans Z -2000..-1400 and KillZ is -2600. nettest.sh / ffa_summary.py label a bot
# "left_arena" when it is below VELOX_ARENA_FLOOR_Z - 1500 = -2200: below the void volume (a healthy void death never trips it first)
# and 400 cm above KillZ.
VELOX_ARENA_FLOOR_Z=-700

_velox_ffa_bots="${BOTS:-3}"
if [ "$_velox_ffa_bots" -ge 4 ] 2>/dev/null; then
  # --- FFA set at >= 4 bots (120 s, seeds 1-10); observed set sums 140 (A) | 152 (H15 fix), 152 141 129 148 (H13)
  BAND_FFA_KILLS_MIN=117          # sum of kills over 10 runs (A 117, B 121; P(false FAIL) 2.1 %); mean 14.0 per run in A
  BAND_FFA_RUN_KILLS_MIN=5        # kills in every run (observed 10-20 over 60 runs; P 1.8 % over ten runs)
  BAND_FFA_RUN_PICKUPS_MIN=59     # pickups in every run (observed 73-92; P 2.3 %)
  BAND_FFA_RUN_MEGA_MIN=2         # Health.Mega takes in every run (observed 4 in 59 of 60 runs, 3 once; A 2, B 3; P 1.0 %)
fi
if [ "$_velox_ffa_bots" -le 4 ] 2>/dev/null; then
  BAND_HAZARD_SELF_SET_MAX=13     # hazard_death_self over FFA x10 (G2): 8 in 20 H15 runs (A: K 13, per run 0-3), 26 in 60 runs with the
                                  # H13 sets (B: K 11); P(false FAIL) 1.6 %. Caught: 2 self deaths per run (set mean 20) 93 %, the B5 and
                                  # H12 broken-edge sets (182, 128) always; 1 per run only 14 % (a weak breakage reads as noise here).
                                  # Replaces the provisional 10 (P(false FAIL) 11 % at the A upper rate).
fi
unset _velox_ffa_bots

# --- BotDuel hazard (nettest.sh): only H13 rounds exist (b5b: self 1 0 0). G2 (b) admits them for this upper band because the FFA
#     self-death rate did not move from H13 to H15 (0.45 vs 0.40 per run); re-derive with the first current-bot BotDuel x3.
BAND_HAZARD_SELF_ROUND_MAX=5      # hazard_death_self in every BotDuel round: K 5, P(false FAIL) 1.7 % over three rounds at the upper
                                  # rate 1.58 per round; the B5 broken rounds (10 19 22) are all over it. Replaces the provisional 2
                                  # (P(false FAIL) 51 % over three rounds at that rate).
