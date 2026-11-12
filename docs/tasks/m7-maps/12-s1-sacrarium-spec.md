# 12 (S1) — Sacrarium spec module (pure Python)

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (validator green except 3 DM1-literal checks, see notes)
**Depends on:** P3
**Wave:** 1 spec modules (parallel)

## Goal
Write the Sacrarium layout as a spec module from its concept. Pure Python, no editor, no build; may run in parallel with the other spec tasks. Deviations from a concept's coordinates are allowed where the validator demands them (the graph and the rules are binding, ADR 0006 §3); list each in the task notes.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/sacrarium_layout.py`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.2 (spec contract), §1.3-1.4, §2.1 (theme pieces), §4 (Sacrarium decisions), §5.3; concept `docs/maps/sacrarium.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `create_map.py --map sacrarium --validate-only` green
- [ ] Pad landings, sightline figures and RA to Mega distance are in the task notes
- [ ] Deviations from the concept listed in the task notes
- [ ] Golden file is NOT written here (the B-task does it)
- [ ] `create_map.py --map dm1 --golden check` green at the end

## Verification steps
1. `create_map.py --map dm1 --golden check` green (pure Python).
2. For engine/C++ tasks: build green for Editor and Game, then the runs named in the acceptance criteria under `flock -o <scratchpad>/soak.lock`; no UE processes left behind; `df -h /` checked first (DISK PAUSE below 5 GB).
3. Commit and push only from your own worktree; never touch the main checkout.

## Report back
Standard format (result, what changed, verification, open issues). Note explicitly if Unreal was not available to compile.

## Notes / outcome
`maps/sacrarium_layout.py` written (boxes plus a `_holed_wall` helper instead of `Wall` records, so ground doors and level-1 openings can be stacked; 9 spawns, 38 items, 4 pads, 22 links, CLIP_MODE box, ceilings as `Ceil_*` clips).

**Validator.** `create_map.py --map sacrarium --validate-only` reports every geometric rule green (pad arcs and landings, 300x300 landing zones, RJ clearance 1280, spawn spacing / weapon <= 8 m / no LOS to Mega or RA, ammo 6..12 m, items on solid tops) and exactly three DM1-literal failures in `mapgen.validate_layout` that no non-DM1 map can pass: `links: JUMP_PAD=4 ROCKET_JUMP=2, expected 3 and 2`, `expected 3 pads`, and the DM1 item-mix dict (Sacrarium: 2 Shotgun, 1 Bullets, 6 Medium). Outside my scope (mapgen is P4's): those checks must move into `DEFAULT_RULES`/spec fields. Also `mapgen.blockout_path/art_path` build `/Game/Maps/SACRARIUM/L_SACRARIUM_Blockout` from MAP_ID; the ADR wants `MAP_PATH`/`ART_MAP_PATH` (defined in the module, not yet used). DM1 golden check: OK. No Sacrarium golden written.

**Pad landings (simulated by `pad_arc` vs concept).**

| Pad | Concept | Simulated | Delta |
|---|---|---|---|
| Quad Flight | (0, -960, 640) | (0, -963, 637.3) | 3 cm |
| South Shrine | (-300, -996, 640) | (-300, -995, 639.3) | 1 cm |
| West Shrine | (-974, 0, 320) | (-973, 0, 319.3) | 1 cm |
| Mega pad | (1700, -1628, 320) | (1700, -1626, 319.4) | 2 cm |

All within 60 cm / 30 cm; no arc hits a solid before its apex.

**Sightlines (eye 160, 720 rays, all non-floor solids).** Longest line into each spawn: S1 40.0 m, S2 35.0, S3 35.0, S4 37.0, S5 31.5, S6 34.5, S7 34.5, S8 34.0, S9 28.5 m. Loft (eye z 800, x -1100..1100 every 200) vs 26 208 floor samples outside the atrium: 356 visible (1.4 %). This contradicts the concept's "no line from the loft passes a ground arch": a line from the loft eye enters the arcade under a gallery deck (z 267 at the deck edge, below 300) and leaves through the arch, so slivers of the N aisle (x -400..400, y 1200..1600), the W aisle strip (x -2000..-1600, via arch and west window) and the E aisle (x 1200..1600, y 0..400) are visible. Not fixed (graph is binding); P4's `LOS_BLOCKED` loft assertion must either be relaxed to "slivers" or the arches get lower lintels / wall stubs.

**RA <-> Mega.** Straight line 55.6 m. Upper route (RA ledge door, NW landing, N gallery, E gallery, SE landing, Mega ledge door, Mega) on waypoints: 74.4 m (concept 65-70 m; the extra is the gallery corners at x +-1000, cutting corners gives about 70). Neither Mega nor RA is visible from any spawn (S4/S5 to RA are cut by the RA ledge deck).

**Deviations from the concept.** (D1) 12 arcade columns, none at x or y = 0, so the arch axes are not blocked at the centre (concept: 16). (D2) stair shards at x -2475/-2225/-1975 (25 cm into the tread). (D3) atrium E/W wall runs span y +-1200, corners belong to the N/S runs (no overlaps). (D4) the 300..320 slab between a ground door and the level-1 opening above it is a floating `lintel`, not a bridge (width >= 120 rule). (D5) NE drop link end (1450, 550) is 50 cm from H25_E (1450, 500), and the E-aisle lane is exactly 300 wide there (E3 (d) marginal; B1 may shift one). (D6) wall heights: S-aisle-adjacent walls 1200, Crypt 480, others 800; ceilings are clips tiling the plan. Items, spawns, pads and links otherwise as in concept sections 6-8 and 14.

**For B1.** Nothing built or run in the engine. LIGHTS/CAPTURES are empty; no `LOS_*`/`POINTS`/`RULES` fields yet (a comment marks where P4's go).
