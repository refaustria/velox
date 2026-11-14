# shellcheck shell=bash disable=SC2034
# Fornax: provisional hard hazard gate only (ADR 0008 5.3, hazard review F9; task M7 H9). Every other band stays report-only until the
# architect task B6 (E3 method); BAND_HAZARD_* values do not switch the other bands to judged (has_band ignores them).
# hazard_death_self: sum over FFA x 10 <= 6 (ffa_set.py), per BotDuel round <= 1 (nettest.sh).
VELOX_BAND_FILE="L_Fornax_Blockout"
BAND_HAZARD_SELF_SET_MAX=6
BAND_HAZARD_SELF_ROUND_MAX=1
