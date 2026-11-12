# 16 (S5) — Orbita spec module (pure Python)

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (validate-only green; elevated pads/spawn gated on records, see below)
**Depends on:** P4
**Wave:** 1 spec modules (parallel)

## Goal
Write the Orbita layout as a spec module from its concept. Pure Python, no editor, no build; may run in parallel with the other spec tasks. Deviations from a concept's coordinates are allowed where the validator demands them (the graph and the rules are binding, ADR 0006 §3); list each in the task notes.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/orbita_layout.py`, `infra/ci/bands/L_Orbita_Blockout.sh`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.2 (spec contract), §1.3-1.4, §2.1 (theme pieces), §4 (Orbita decisions), §5.3; concept `docs/maps/orbita.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `create_map.py --map orbita --validate-only` green
- [ ] No link end off a deck
- [ ] Void slab and KillZ per §3.4
- [ ] Band file sets `VELOX_ARENA_FLOOR_Z=-700` and no `BAND_*` value (report-only)
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
`maps/orbita_layout.py` (54 solids, 37 items, 6 spawns, 17 links, 3 pads, void slab) and `infra/ci/bands/L_Orbita_Blockout.sh` (`VELOX_ARENA_FLOOR_Z=-700`, no `BAND_*`). `create_map.py --map orbita --validate-only` and `--selftest` green; `--map dm1 --golden check` OK; no golden for Orbita written. Link ends: hard rule on, all 17 ends on a deck (the 3 elevated pad links and S7 checked separately: ok). Void: `Hazard("VoidSlab","void", X -10400..10200, Y -9400..9800, Z -2000..-1400)`, `KILL_Z -2600`, `CLIP_MODE "none"` (ADR 0008 3.4).

**Blocking generator gaps (report, not edited).**
1. *No pad or spawn height.* `mapgen.pad_launch/pad_arc` launch from `FLOOR_Z`, `veloxmap.jump_pad` / `player_start` put pad and start at FLOOR_Z + constant. P3 (Scopulus 160), P4 (Hortus -320), P6 (Trabs 320) and S7 (Trabs 320) cannot be expressed. They sit in `PADS_ELEVATED` / `SPAWNS_ELEVATED` and join `PADS`/`SPAWNS`/links/`RULES` counts/`EXPECTED` automatically once `records.Pad`/`Spawn` get a trailing `z` field (`"z" in Pad._fields`). Needed: `z` in records, `pad_launch/pad_arc/pad_arc_clearance_problem` and `check_hazards` use `p.z`, `vm.jump_pad(..., z)`, `player_start` z, and the target/landing markers. Until then B5 cannot build the full map.
2. *Per-instance mode gating.* YA_N (FFA only), H25_Scop (Duel only), the 3 Specula shards (Duel only) carry `disabled_in_modes` but `spawn_items` fails when the DA does not list the tag (DAs are per type). Needs per-instance gating in `vm.pickup` or per-mode DA variants.
3. *`Link` has no `required_speed`* (G1 `JUMP` needs 650, G2 600 per concept 5.3); the spawner uses class defaults.
4. `veloxmap.hazard()` (H2) for the void slab. `mapgen.pad_arc_clearance_problem` is unusable on decks: the simulated landing sits up to one step (1-6 cm) below the deck top, so the landing deck counts as an obstacle (`s[7] > lz + 1`); RULES leaves `pad_arc_clearance` at 0 and the clearance below is computed by a script that compares with the authored landing top.
5. RJ clearance reads a ceiling clip. With `CLIP_MODE none` there is none, so `Ceil_Hub` (X/Y +-1000, Z 1700..1800) was added; a rule "no ceiling = unlimited" would remove it.
6. `longest_sightline` prints 300.0 m on every spawn (rays leave the map; reach 30000): the figure means nothing here.

**Pad landings (true launch height, `pad_arc` with FLOOR_Z = deck z; the three flat-launch pads also in `--validate-only`).**
| Pad | Concept landing | Simulated | xy delta | speed cm/s | min distance to a solid |
|---|---|---|---|---|---|
| P1 | (3500,0,320) | (3503,0,318.6) | 3 | 2519 | 500 (N1) |
| P2 | (-2600,3000,160) | (-2598,3001,154) | 2 | 2456 | 112 (AR_Cover, at the pad) |
| P3 | (-1300,1400,0) | (-1301,1402,-0.8) | 2 | 1680 | 231 (Pylon_NW) |
| P4 | (0,0,640) | (-1,3,637) | 3 | 3105 | 100 (HO_Rack1, at the pad) |
| P5 | (-3600,-500,0) | (-3604,-496,-4) | 6 | 2788 | 447 (K1) |
| P6 | (500,3300,-320) | (504,3302,-320) | 5 | 2678 | 224 (TR_Pylon, at the pad) |
All below 4000 cm/s, no arc hits a solid before its apex. Apexes were retuned because the generator launches from trigger height 70, not 0: P1 (2660,0,520)->(2730,0,520), P2 (-3255,1880)->(-3250,1885), P3 (-1680,2160)->(-1685,2170), P4 (-150,940)->(-130,830), P6 (2545,2340)->(2615,2310). Landings, decks and run-outs are the concept's.

**Deviations from the concept (graph kept).** D1 Clivus (CL) and Clivus Orientis (CO) feet at 2794.2 / -2794.2 (and RN/RS foot +-594.2) so the 15 degree ramp tops meet their decks flush (run 1194.2, not 1200); Hortus box starts at y 2794.2 (5.8 cm over the foot). D2 D1 drops: 6 of the 8 (W x2, E x2, N x1 at x -720, S x1 at x 720): RN/RS and their landings cover the rest of the north/south Corona edges; ends 260 cm outside the edge (a 300 x 300 clear top must not touch the Corona wall); D2 drops end 175 cm out for the same reason. D3 east pylons at (1500, +-1300) instead of (1100, +-1100) and S2/S3 at x 1250 (not 1350): only then is each east hub spawn behind a pylon as seen from the Trabs node (checked by LOS_BLOCKED, 0 %). D4 S5 at (-1300,-3100) (P5 trigger was 283 cm away). D5 Rockets ammo (1400,-2600): the concept spot is the edge of container K2. D6 ammo distance 600..2800 (`AMMO_MIN/MAX_CM`), spawn-to-weapon 2500: the concept's own item positions (11..27 m). D7 K1 crate 200 wide so it is a legal walkable top; AR cover, parapets, racks, outcrop, dish and pylons are `wall` boxes (not standing surfaces) except the Trabs pylon. D8 decks are 200 thick `floor` boxes without nav fill (`FILL_NAV_KINDS = ()`), bridges `bridge`. D9 `Ceil_Hub` lid (item 5). D10 Specula shard heights etc. as concept; Specula/Corona/hub z are the concept's.

**Line of sight.** Blocked (0 % visible): hub N/S bands and W/E bands (through Corona), Specula to Horreum (dish), hub west to RA (cover block), Trabs node to S1/S2/S3. Clear (0 % blocked): Trabs tip to Mega, Armamentarium to Scopulus, Trabs node to hub east band.

**Not verified.** Nothing built or run in Unreal. Heights 16-25 m run-outs of P3/P4/P6 pads are estimates as in the concept (A3 measures them).
