# shellcheck shell=bash disable=SC2034
# Soak bands of DM1 ("Praesidium"): ADR 0006 E3 (2026-11-01), re-derived by E4 (2026-11-01, task 07c) by the D51 (1) method over every
# round on the final layout: task 06b on L_DM1_Blockout (docs/adr/evidence/0006-dm1-blockout-06b/) plus task 15 on L_DM1
# (docs/adr/evidence/0006-dm1-art/; same layout by --diff-collision), with task 06 set 6 as the second basis (D40 caveat): 9 BotRun,
# 9 BotDuel, 30 FFA rounds. Derivation: docs/adr/evidence/0006-dm1-bands-e4/e4_bands.py -> e4_bands.out (E3's e3_bands.py stays as the
# six-round record). Lower band k = largest k with P(sum < k | unchanged bots and layout) <= 2.5 % (Poisson and bootstrap over rounds,
# the smaller). The art map L_DM1 is generated from the same layout table as the blockout (ADR 0006 section 2), so both maps share this
# file: L_DM1_Blockout.sh sources it. Sourced by infra/ci/lib.sh ci_load_bands with every assignment exported. The map-independent
# invariants (left_arena, world_kill, no_nav_point, giveup, stuck sums/bursts, dwell <= 5 s, nav build, link success >= 90 % per type,
# item coverage) are NOT in this file and cannot be loosened here. No coverage exemption on DM1 (E0.3 a): every one of the 35 pickups
# must be taken over the FFA x10 set. Next re-derivation (E4 c): when the layout table or a bot knob changes, after the human's Mac
# verdict, or once 12 current-bot rounds per bot script exist (one more BotRun x3 + BotDuel x3 confirmation).
VELOX_BAND_FILE="L_DM1"

# --- BotRun (FFA, 3 Medium bots, 90 s), per round; BotDuel round for the first — all confirmed by E4
BAND_ROUND_KILLS_MIN=1            # kills by bots in a BotRun round / frags in a BotDuel round (P(0 | unchanged) 0.2 % / 1.2 % per round, E4)
BAND_BOTRUN_PICKUPS_MIN=30        # pickups by bots in a BotRun round (observed 56-72 over 9 rounds, mean 62.6, 6.1 sd above the band; 35 items on DM1)
BAND_BOTRUN_MEGA_REQUIRED=1       # a Health.Mega take by a bot in every BotRun round (observed 3-4 of <= 4 cycles in all 9 rounds)
BAND_BOTRUN_LINK_OK_MIN=5         # VeloxBotLink result=ok in every BotRun round (observed 11-30, mean 20; 16 links on DM1)

# --- bot set (--repeat 3), E3 values re-derived by E4
BAND_BOTRUN_KILLS_MIN=11          # BotRun sum of kills by bots (E4 confirmed: rounds 6 8 5 | 7 5 7 | 4 4 11, set mean 19.0; P(fail) 1.8 %)
BAND_BOTRUN_MAJOR_REQUIRED=1      # every major item PRESENT ON THE MAP taken over the set (Mega, RA, YA, Quad in FFA; Duel has no Quad); nettest.sh
                                  # judges the majors with a VeloxPickupReset/Spawn line in the round logs (task 07b), not the seven of D51
BAND_BOTDUEL_FRAGS_MIN=7          # BotDuel sum of frags (E4 confirmed: rounds 7 3 4 | 3 4 5 | 4 6 4, set mean 13.3; P(fail) 2.1 %; the six
                                  # current-bot rounds alone give k = 6 with P(fail | 7) 2.6 % -- at the edge, D49 one-rerun fallback applies)
BAND_BOTDUEL_CAMP_MIN=22          # BotDuel sum of camp episodes, Live phase (E4: 21 -> 22; rounds 11 12 7 | 10 15 11 | 10 8 14, set mean 32.7; P(fail) 2.0 %)
BAND_MEGA_WITHIN3_MIN=1           # BotDuel timed Mega returns within 3 s of the respawn, sum over the set (D51 6; E4: p(<= 3 s) 0.50-0.58, P(fail) <= 1.6 %)
BAND_MEGA_WITHIN10_MIN=2          # ... within 10 s (p(<= 10 s) 0.94-1.00)

# --- FFA set (ffa.sh, 3 Medium bots, 120 s, seeds 1-10), E3 values re-derived by E4
BAND_FFA_KILLS_MIN=36             # sum of kills over 10 runs (judged for exactly 10 runs; E4: 34 -> 36; 30 runs, mean 4.90 per run, set mean 49.0,
                                  # observed sets 47 54 46; P(fail) Poisson 2.3 %, bootstrap 0.4 %)
# BAND_FFA_RUN_KILLS_MIN is deliberately unset on DM1 (report-only): at 4.9 kills per run P(run = 0) is 0.7 % per run, 7 % over
# ten runs, and a single kill-less run among nine normal ones says nothing the sum band does not (E3, E4).
BAND_FFA_RUN_PICKUPS_MIN=40       # pickups in every run (observed 55-86 over 30 runs, mean 73.6, 4.6 sd above the band)
BAND_FFA_RUN_MEGA_MIN=2           # Health.Mega takes in every run (observed 4 in 30 of 30 runs: every 35 s cycle; a dead ledge route gives 0)
