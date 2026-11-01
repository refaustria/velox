# shellcheck shell=bash disable=SC2034
# Soak bands of DM1 ("Praesidium"): ADR 0006 E3 (2026-11-01), derived by the D51 (1) method from the task 06b sets on
# L_DM1_Blockout (docs/adr/evidence/0006-dm1-blockout-06b/, quiet host) with task 06's final-layout sets as the second basis
# (docs/adr/evidence/0006-dm1-blockout/botrun6, botduel6, ffa6; D40 caveat). Derivation: 0006-dm1-blockout-06b/e3_bands.py
# -> e3_bands.out. Lower band k = largest k with P(sum < k | unchanged bots and layout) <= 2.5 % (Poisson and bootstrap
# over rounds, the smaller). The art map L_DM1 is generated from the same layout table as the blockout (ADR 0006 section 2),
# so both maps share this file: L_DM1_Blockout.sh sources it. Sourced by infra/ci/lib.sh ci_load_bands with every assignment
# exported. The map-independent invariants (left_arena, world_kill, no_nav_point, giveup, stuck sums/bursts, dwell <= 5 s,
# nav build, link success >= 90 % per type, item coverage) are NOT in this file and cannot be loosened here. No coverage
# exemption on DM1 (E0.3 a): every one of the 35 pickups must be taken over the FFA x10 set.
VELOX_BAND_FILE="L_DM1"

# --- BotRun (FFA, 3 Medium bots, 90 s), per round; BotDuel round for the first
BAND_ROUND_KILLS_MIN=1            # kills by bots in a BotRun round / frags in a BotDuel round (P(0 | unchanged) 0.2 % / 0.9 % per round)
BAND_BOTRUN_PICKUPS_MIN=30        # pickups by bots in a BotRun round (observed 56-72, mean 62.5; 35 items on DM1)
BAND_BOTRUN_MEGA_REQUIRED=1       # a Health.Mega take by a bot in every BotRun round (observed 3-4 of <= 4 cycles)
BAND_BOTRUN_LINK_OK_MIN=5         # VeloxBotLink result=ok in every BotRun round (observed 13-30; 16 links on DM1)

# --- bot set (--repeat 3), E3
BAND_BOTRUN_KILLS_MIN=11          # BotRun sum of kills by bots (rounds 6 8 5 | 4 4 11, set mean 19.0; P(fail) 1.8 %)
# BAND_BOTRUN_MAJOR_REQUIRED=1    # every major item PRESENT ON THE MAP taken over the set (Mega, RA, YA, Quad in FFA). Set when
#                                 # task 07b makes the runner judge map-present majors; the runner's fixed 7-item list would FAIL DM1
#                                 # (no BattleSuit/Haste/Regen on this map, ADR 0006 section 3).
BAND_BOTDUEL_FRAGS_MIN=7          # BotDuel sum of frags (rounds 7 3 4 | 4 6 4, set mean 14.0; P(fail) 1.4 %)
BAND_BOTDUEL_CAMP_MIN=21          # BotDuel sum of camp episodes, Live phase (rounds 11 12 7 | 10 8 14, set mean 31.0; P(fail) 2.4 %)
BAND_MEGA_WITHIN3_MIN=1           # BotDuel timed Mega returns within 3 s of the respawn, sum over the set (D51 6; E3: p(<= 3 s) 0.42-0.53)
BAND_MEGA_WITHIN10_MIN=2          # ... within 10 s (p(<= 10 s) 0.92-1.00)

# --- FFA set (ffa.sh, 3 Medium bots, 120 s, seeds 1-10), E3
BAND_FFA_KILLS_MIN=34             # sum of kills over 10 runs (judged for exactly 10 runs; runs 4 7 3 5 4 3 6 5 7 3 | 3 4 5 5 1 3 6 7 6 6, set mean 46.5; P(fail) 2.4 %)
# BAND_FFA_RUN_KILLS_MIN is deliberately unset on DM1 (report-only): at 4.65 kills per run P(run = 0) is 1 % per run, 9 % over
# ten runs, and a single kill-less run among nine normal ones says nothing the sum band does not (E3).
BAND_FFA_RUN_PICKUPS_MIN=40       # pickups in every run (observed 55-86, mean 72.5)
BAND_FFA_RUN_MEGA_MIN=2           # Health.Mega takes in every run (observed 4 in 20 of 20 runs: every 35 s cycle; a dead ledge route gives 0)
