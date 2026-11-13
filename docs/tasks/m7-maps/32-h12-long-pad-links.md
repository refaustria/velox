# 32 (H12) — Long jump-pad links never connect (nav tile span)

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** open
**Depends on:** B5 (merged)
**Wave:** bot/nav track (lead-added 2026-11-13 from B5, docs/tasks/m7-maps/21-b5-orbita-build.md notes). Blocks B5b (Orbita), helps every map with long pads.

## Goal
Detour connects an off-mesh link only if its end lies in the start's tile or a neighbouring one (tile size 1000 cm). On L_Orbita_Blockout five of six pad links (22–35 m) span 2–4 tiles and are absent from the nav graph (`VeloxNav link_span` fires for P1, P2, P4, P5, P6); bots never use them, Scopulus is unreachable (FtMapReachability 21 checks fail). Nemus P1 and L_HazardTest Pad_Gap had to be moved for the same reason. Make long pad (and rocket-jump) links work.

**Lead decision (reversible):** first try a per-map nav tile size (spec field, e.g. `NAV_TILE_SIZE_UU` in the layout module, written by the generator into the map's RecastNavMesh; Orbita ~4000) and measure nav build time (gate ≤ 2.0 s) and nav memory. If that breaks the nav build budget or Detour limits, implement a bot-side long-link mechanism instead (e.g. the bot subsystem keeps its own long-link table and the path follower splices it in), and say why.

## Scope
- `Tools/editor/{mapgen.py,veloxmap.py,create_map.py}`, `Tools/editor/maps/records.py`, `Tools/editor/maps/orbita_layout.py` (tile size field only), `Source/Velox/Bots/*` (only if the fallback is needed), nav lint (`VeloxNavCoverage.*` link_span stays as a warning), regenerated `Content/Maps/Orbita/L_Orbita_Blockout.umap`, tests/FT rows, this file
- DM1 / Sacrarium / Nemus goldens must stay byte-identical unless the field is explicitly set there

## Acceptance criteria
- [ ] On L_Orbita_Blockout: `VeloxNav link_span` count 0; navlint PASS (clusters 0, lowclear 0), nav build ≤ 2.0 s
- [ ] FtMapReachability on Orbita green (Scopulus reachable from every spawn/major)
- [ ] FFA ×10 at 4 bots: every pad link has attempts and ≥ 90 % success (per-link table)
- [ ] DM1, Sacrarium, Nemus navlint + their fts rows unchanged green; DM1 golden OK; build + full automation + fts.sh green

## Notes / outcome
(Filled by the agent.)
