# shellcheck shell=bash disable=SC2034
# Soak bands of L_ItemTest (ADR 0005 D36/D46/D48/D49/D51 (7); moved here by ADR 0006 section 7, values unchanged).
# Sourced by infra/ci/lib.sh ci_load_bands with every assignment exported. A map without a file in this directory runs
# report-only (`band=none`): every metric is still printed, none of the variables below is judged. The map-independent
# invariants (left_arena, world_kill, no_nav_point, giveup, stuck/bursts, dwell, nav build time, link success, item
# coverage) are NOT in this file and cannot be loosened here, except by the explicit, printed BAND_FFA_COVERAGE_EXEMPT list.

# --- BotRun (FFA, 3 Medium bots, 90 s), per round; BotDuel round for the first
BAND_ROUND_KILLS_MIN=1            # kills by bots in a BotRun round (frags in a BotDuel round)
BAND_BOTRUN_PICKUPS_MIN=15        # pickups by bots in a BotRun round
BAND_BOTRUN_MEGA_REQUIRED=1       # a Health.Mega take by a bot in every BotRun round
BAND_BOTRUN_LINK_OK_MIN=2         # VeloxBotLink result=ok in every BotRun round

# --- bot set (--repeat 3), D51 (7)
BAND_BOTRUN_KILLS_MIN=20          # BotRun sum of kills by bots
BAND_BOTRUN_MAJOR_REQUIRED=1      # every major item (Mega, RA, YA, BattleSuit, Haste, Quad, Regen) taken by a bot over the set
BAND_BOTDUEL_FRAGS_MIN=20         # BotDuel sum of frags
BAND_BOTDUEL_CAMP_MIN=7           # BotDuel sum of camp episodes (Live phase)
BAND_MEGA_WITHIN3_MIN=1           # BotDuel timed Mega returns within 3 s of the respawn (sum over the set)
BAND_MEGA_WITHIN10_MIN=2          # ... within 10 s (D51 6; the D48 6 s count stays printed as within_6s)

# --- FFA set (ffa.sh, 3 Medium bots, 120 s), D36/D51 (7)
BAND_FFA_KILLS_MIN=74             # sum of kills over 10 runs (judged for exactly 10 runs)
BAND_FFA_RUN_KILLS_MIN=2          # kills in every run
BAND_FFA_RUN_PICKUPS_MIN=20       # pickups in every run
BAND_FFA_RUN_MEGA_MIN=1           # Health.Mega takes in every run
# Item coverage (a map-independent invariant) found by task m5-04 on the stored D51 FFA x10 set: these two ammo pickups are never
# taken by the 3 Medium bots in 10 x 120 s (bots fetch ammo by need; the grid's Rockets box sits at Z=350). Listed here so the
# L_ItemTest verdict does not change; the exception is printed on every run and exists for no other map.
BAND_FFA_COVERAGE_EXEMPT="DA_Item_Ammo_Bullets DA_Item_Ammo_Rockets"
