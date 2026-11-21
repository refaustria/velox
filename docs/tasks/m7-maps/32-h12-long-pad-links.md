# 32 (H12) — Long jump-pad links never connect (nav tile span)

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev-hard
**Status:** done (2026-11-14, dev-hard)
**Depends on:** B5 (merged)
**Wave:** bot/nav track (lead-added 2026-11-13 from B5, docs/tasks/m7-maps/21-b5-orbita-build.md notes). Blocks B5b (Orbita), helps every map with long pads.

## Goal
Detour connects an off-mesh link only if its end lies in the start's tile or a neighbouring one (tile size 1000 cm). On L_Orbita_Blockout five of six pad links (22–35 m) span 2–4 tiles and are absent from the nav graph (`VeloxNav link_span` fires for P1, P2, P4, P5, P6); bots never use them, Scopulus is unreachable (FtMapReachability 21 checks fail). Nemus P1 and L_HazardTest Pad_Gap had to be moved for the same reason. Make long pad (and rocket-jump) links work.

**Lead decision (reversible):** first try a per-map nav tile size (spec field, e.g. `NAV_TILE_SIZE_UU` in the layout module, written by the generator into the map's RecastNavMesh; Orbita ~4000) and measure nav build time (gate ≤ 2.0 s) and nav memory. If that breaks the nav build budget or Detour limits, implement a bot-side long-link mechanism instead (e.g. the bot subsystem keeps its own long-link table and the path follower splices it in), and say why.

## Scope
- `Tools/editor/{mapgen.py,veloxmap.py,create_map.py}`, `Tools/editor/maps/records.py`, `Tools/editor/maps/orbita_layout.py` (tile size field only), `Source/Velox/Bots/*` (only if the fallback is needed), nav lint (`VeloxNavCoverage.*` link_span stays as a warning), regenerated `Content/Maps/Orbita/L_Orbita_Blockout.umap`, tests/FT rows, this file
- DM1 / Sacrarium / Nemus goldens must stay byte-identical unless the field is explicitly set there

## Acceptance criteria
- [x] On L_Orbita_Blockout: `VeloxNav link_span` count 0; navlint PASS (clusters 0, lowclear 0), nav build ≤ 2.0 s
- [x] FtMapReachability on Orbita green (Scopulus reachable from every spawn/major)
- [x] FFA ×10 at 4 bots: every pad link has attempts and ≥ 90 % success (per-link table)
- [x] DM1, Sacrarium, Nemus navlint + their fts rows unchanged green; DM1 golden OK; build + full automation + fts.sh green

## Notes / outcome
dev-hard, branch `wip/m7-32-h12-long-pad-links` on origin/m7-maps (e1e24cd), engine 5.8.4. Raw logs: /mnt/oldroot/velox-scratch/m7-h12/.

- **Approach: per-map nav tile size (lead's first option), no bot-side fallback needed.** Spec field `NAV_TILE_SIZE_UU`
  (Orbita 4000: any link under 40 m sits in adjacent tiles wherever it lies). `mapgen.build_map` / `build_art_map` place a
  `RecastNavMesh` actor carrying it (`veloxmap.nav_data`, label `RecastNavMesh_Velox`, counted `navdata`) only when the field is
  set; other maps get no actor and keep the auto-created 1000 cm `RecastNavMesh-VeloxPawn`. Two engine details forced a small C++
  helper `UVeloxNavDataLibrary::SpawnMapNavMesh` (Bots/VeloxNavData.*): ARecastNavMesh is `notplaceable` (EditorActorSubsystem
  returns None, the commandlet editor world has no nav system to spawn one), and a fresh instance carries the class-default
  NavDataConfig, which `RegisterNavData` rejects (`RegistrationFailed_AgentNotValid`, then auto-creates the 1000 cm one); the helper
  stamps the single supported agent (`VeloxPawn`) with `SetConfig`. `bForceRebuildOnLoad` keeps the runtime build; the umap holds no
  nav tiles (+2 KB). Clients never load it (ANavigationData `bNetLoadOnClient`).
- **Pure-Python guard:** `validate_layout` now fails a link whose ends are more than one nav tile apart at the map's tile size
  (`mapgen.nav_link_tile_span`, same judgement as the runtime `VeloxNav link_span`, Recast axes x_r = -x, z_r = -y). Without the
  field Orbita reports exactly P1, P2, P4, P5, P6. DM1 / Sacrarium / Nemus goldens byte-identical; orbita.json gains `navdata: 1`.
- **Nav build / memory (navlint x3):** tile 1000: 48 tiles, 1.31-1.49 s, 39.6 KB Detour tile data; tile 4000: 8 tiles,
  1.05-1.10 s (FFA runs 1.00-1.06 s), 29.7 KB. `velox.bots.NavCoverage` logs a new `VeloxNavCoverage navmem` line (engine
  `LogMemUsed` also prints its LogNavigation line). navlint PASS clusters 0 lowclear 0; `VeloxNav link_span` 0 (navlint and all 10 FFA logs).
- **FtMapReachability-L_Orbita_Blockout PASS** checks 6 failed 0 (B5: 21 failed; Scopulus reachable).
- **FFA x10 at 4** (`ffa.sh --map L_Orbita_Blockout --repeat 10 --bots 4`): link_success JumpPad 100.0 % (85/85, aborted 18;
  B5: P3 only, 8/8). Per pad ok/attempts: P1 3/3, P2 31/31, P3 9/9, P4 9/9, P5 16/16, P6 17/17 (P1 is rarely chosen: n=3). Jump 79/79,
  Drop 15/29 (n<30). Set FAIL on items not in this task: max_dwell_s 6 (run 9, one fight_stall on Armamentarium), item_coverage 36/37
  (Health_Medium at (1300,-1000) untaken; B5's Trabs H25_3 is now taken). hazard_death_self 128 / credited 82 (B5 182 / 96; H13).
- **Other maps unchanged:** navlint DM1, Sacrarium, HazardTest PASS; Nemus lowclear 1 (Hump3_Dn), identical to its committed B2
  evidence (pre-existing, not touched here). Build VeloxEditor+Velox green, automation CI OK, fts.sh OK 18/18.
- Follow-ups: ADR 0008 1.2 should list `NAV_TILE_SIZE_UU` as an optional spec field (not edited, out of scope); the art map
  (`--mode art`) places the same actor but was not regenerated here (Orbita has no art map yet).
