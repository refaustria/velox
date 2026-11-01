# shellcheck shell=bash disable=SC1091
# L_DM1_Blockout shares the DM1 bands: the blockout and the art map L_DM1 come from one layout table (ADR 0006 section 2),
# and the bands are a property of the layout and the bots, not of the meshes (ADR 0006 E3). Values live in L_DM1.sh only.
source "$(dirname "${BASH_SOURCE[0]}")/L_DM1.sh"
