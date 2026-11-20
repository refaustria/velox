# shellcheck shell=bash disable=SC2034
# Soak bands of Nemus: ADR 0008 G1 (task 22, B6, 2026-11-20). REPORT-ONLY: this file sets no BAND_* value.
# Every Nemus set (B2b on the final layout df83346: FFA x10 at 3 twice, FFA x10 at 6, BotRun x3) ran with the H13 bots, before H14
# and H15 (1fe0997). G1 (b): a band needs a basis on the final layout with the current bots; older-bot sets may widen a basis (D40)
# but cannot be the only one. The candidate values below are derived by the E3 / D51 (1) method from the H13 sets
# (docs/adr/evidence/0008-bands-b6/b6_bands.py -> b6_bands.out) and become the bands, unchanged or re-derived with the new rounds in
# the basis, after one confirmation with the current bots: FFA x10 at 3, FFA x10 at 6, BotRun x3 (G1 f, a dev task).
#   FFA at >= 3 bots: BAND_FFA_KILLS_MIN=54 (20 runs, mean 7.0, P 2.1 %), BAND_FFA_RUN_KILLS_MIN=1 (0.9 %), BAND_FFA_RUN_PICKUPS_MIN=39
#                     (observed 46-68, 2.2 %); FFA_RUN_MEGA_MIN not supported (2-4 takes per run, binomial P(run < 1) over ten runs > 2.5 %)
#   FFA at >= 6 bots: BAND_FFA_KILLS_MIN=204 (10 runs, mean 23.3, 2.5 %), BAND_FFA_RUN_KILLS_MIN=11 (1.6 %),
#                     BAND_FFA_RUN_PICKUPS_MIN=82 (observed 102-117, 2.3 %), BAND_FFA_RUN_MEGA_MIN=1 (1.8 %)
#   BotRun x3 (3 rounds only, E4 wants >= 12): BAND_BOTRUN_PICKUPS_MIN=36, BAND_BOTRUN_LINK_OK_MIN=9, BAND_BOTRUN_KILLS_MIN=15,
#                     BAND_ROUND_KILLS_MIN=2; BOTRUN_MEGA_REQUIRED not supported (one round with a single take)
# allunreach_air (ADR 0008 G3): flagged in 2 of 10 (FFA at 3) and 5 of 10 (FFA at 6) runs; a diagnostic, never banded.
# The map-independent invariants (left_arena, world_kill, no_nav_point, giveup, stuck sums/bursts, dwell <= 5 s, nav build <= 2 s,
# link success >= 90 % per type, item coverage) are judged on Nemus as on every map and cannot be loosened here. No hazard on Nemus.
VELOX_BAND_FILE="L_Nemus"
