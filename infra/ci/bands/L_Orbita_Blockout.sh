# shellcheck shell=bash disable=SC2034
# Soak bands of Orbita (ADR 0008 section 3.5, task M7 S5). Report-only: this file holds NO BAND_* value on purpose. Bands come from
# the architect task B6 by the E3 method after three report-only sets; until then every metric is printed and none is judged
# (the runner task verifies that a band file without BAND_* values keeps the map report-only).
#
# What it does set: the arena floor of the left-arena label. Orbita's lowest deck (Hortus) is at Z -320 (E0.3 e exception, ADR 0008
# 3.5), the void hazard slab spans Z -2000..-1400 and KillZ is -2600. nettest.sh / ffa_summary.py label a bot "left_arena" when it is
# below VELOX_ARENA_FLOOR_Z - 1500 = -2200: below the void volume (a healthy void death never trips it first) and 400 cm above KillZ.
# The map-independent invariants (left_arena, world_kill, no_nav_point, giveup, stuck, dwell, nav build, link success, item coverage)
# stay hard and are not in this file.
VELOX_BAND_FILE="L_Orbita_Blockout"
VELOX_ARENA_FLOOR_Z=-700
