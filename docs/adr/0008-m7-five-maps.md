# ADR 0008 — M7: five maps as blockouts (shared map pipeline, themed kit, hazards and void, bot edge behaviour)

**Status:** Proposed 2026-11-12 (architect). The human approved building all five concepts on 2026-11-12 ("setze alles um"); this ADR turns that into decisions and tasks.
**Scope:** everything `dev`/`dev-hard` need to build `docs/maps/{sacrarium,culmen,orbita,nemus,fornax}.md` as **blockouts** (`L_<Name>_Blockout`), with the kit and art-pass decisions fixed far enough that the art pass needs no second ADR. Amendments to this ADR are numbered **G1, G2, …**.
**Amends:** ADR 0006 §2 (one generator for all maps), §7 and E0.3 (e) (soak metrics on hazard maps, one floor-Z exception), §8 (kit cap wording); ADR 0004 §6 (kill credit for hazard deaths); ADR 0005 §5/§10 (edge-aware overlay, hop landing guard, airborne recovery, hazard escape; these get D-numbers when implemented). It supersedes nothing: every hard invariant and budget of ADR 0006 stays in force. ROADMAP M7 says "two more maps"; the lead updates it to five.

## Context

Facts verified in the tree at 359b68c:

1. **Generator.** `Tools/editor/create_dm1_map.py` (858 lines) holds four things in one file, all bound to `maps/dm1_layout.py` through `from maps import dm1_layout as L`: record expansion (`expand_wall`, `expand_stairs`, `expand_ramp_fills`, `all_solids`), the validator (`validate_layout`, pad arc simulation), the art planner (`plan_art`, `art_plan`, `plan_protrusion`) and the Unreal spawning (`build_map`, `build_art_map`). The first three are pure Python (`--validate-only` runs without the editor). `Ramp` rises along +X only at the global `RAMP_DEG`, from `FLOOR_Z`; `Stairs` run along X only with the global `TREAD`/`RISER`; `veloxmap.CLIP_H` is a constant 1800; `veloxmap.ramp()` and `stairs()` have no yaw.
2. **Kit.** `Tools/blender/kit.py` asserts `MAX_PIECES = 25` and 8 materials globally and the palette (V ≤ 0.5, S ≤ 0.25) on every material except the emissive fixture; `create_kit_materials.py` repeats the palette assertion (`PALETTE_EXEMPT = ("Light",)`); `import_kit.py` has its own `MAX_PIECES = 25`. The kit has 18 meshes, 8 `MI_Kit_*`, 5 textures. `audit_map.py` estimates draw calls as the sum of material slots over visible components (no batching) and enforces 600.
3. **World damage.** The only world damage is `AVeloxCharacter::FellOutOfWorld` (`Character/VeloxCharacter.cpp`): it builds a `FVeloxDamageSpecParams` with `EnvironmentKillDamage` (1000), tag `Velox.Damage.Environment`, `bNoArmor`, `bNoKnockback`, on the **victim's own ASC**, so `FVeloxDamageEvent::Instigator` is the victim and `AVeloxGameMode::PlayerDied(Victim, Killer, Tags)` scores a suicide (−1, `bSuicide` in the feed). No last-attacker record exists. `Velox.Damage.Fall` is declared and used nowhere: Velox has no fall damage.
4. **Soak runners.** `infra/ci/nettest.sh`, `ffa_summary.py`, `ffa_set.py` count `world_kill` as a `VeloxKill` line whose tags contain `Damage.Environment`, and `left_arena` as a live bot below `VELOX_ARENA_FLOOR_Z − 1500`. `ffa.sh` hard-codes `?Bots=3`. A map without a band file runs report-only; the map-independent invariants are judged regardless.
5. **Bots.** The combat overlay (`VeloxBotCombat::Overlay`, lateral strafe amplitude 0.7, projectile dodge, retreat) writes move input without consulting the nav mesh. The hop guard (`UVeloxBotPathFollowingComponent::HasHopHeadroom`, D18) checks headroom only. Off-mesh recovery ends in a suicide by environment damage (D17). `VeloxNavCoverage` exempts by the forbidden `NavLintIgnore` tag only. The reachability FT is `velox.bots.FtDm1Reachability` (`Tests/VeloxDm1ReachabilityFt.cpp`), DM1 by name.
6. **Host.** `/` has 12 GB free (floor 5 GB). A worktree that runs the editor needs its own compiled module (`Binaries` ≈ 0.9 GB plus `Intermediate`). Every UE run goes under the soak lock, so UE work is serial on this VM. The VM cannot bake or render; bakes and GPU numbers wait for the Mac Studio (about 2026-11-20). LFS holds about 15 MB today; budget 600 MB including `.blend` (STATUS q39).
7. **Net version.** ADR 0007 F2 folds the gameplay-tag hash into the net version: new native tags make new builds incompatible with the deployed 0.6.1 clients and server.

The five concepts split cleanly: Sacrarium, Nemus and Culmen need generator work only; Fornax (lava) and Orbita (void) need combat and bot code.

## Decision

### 1. One generator, one spec module per map

**1.1 File layout.**

| File | Content | Imports `unreal` |
|---|---|---|
| `Tools/editor/maps/records.py` | all record namedtuples (with defaults for new fields), kind constants, `DEFAULT_RULES` | no |
| `Tools/editor/maps/<slug>_layout.py` | one spec module per map: `dm1`, `sacrarium`, `culmen`, `nemus`, `fornax`, `orbita` | no |
| `Tools/editor/mapgen.py` | pure Python: record expansion, `validate(L)`, pad arc simulation, line-of-sight, art planner, protrusion and emissive plan checks, golden dump | no |
| `Tools/editor/create_map.py` | CLI and Unreal spawning (`build_blockout(L)`, `build_art(L)`) on `veloxmap.py` | yes (optional, as today) |
| `Tools/editor/create_dm1_map.py` | 10-line shim: `create_map.main(["--map", "dm1"] + sys.argv[1:])`; `VELOX_DM1_MODE` still honoured | — |
| `Tools/editor/maps/golden/<slug>.json` | golden dump per map (1.5) | — |

CLI: `create_map.py --map <slug> [--mode blockout|art] [--validate-only] [--golden check|write] [--selftest]`. No function takes the layout from a module global: `L` is a parameter everywhere (`mapgen.all_solids(L)`), loaded with `importlib.import_module("maps.%s_layout" % slug)`.

**1.2 Spec module contract.** Required: `MAP_ID` (the slug in capitals: `SACRARIUM`, `CULMEN`, `NEMUS`, `FORNAX`, `ORBITA`; no `DMn` numbers), `SLUG`, `DISPLAY_NAME`, `MAP_PATH` (`/Game/Maps/<Name>/L_<Name>_Blockout`), `ART_MAP_PATH` (`/Game/Maps/<Name>/L_<Name>`), `LOG_PREFIX`, `FLOOR_Z`, `KILL_Z`, `NAV_CENTER`, `NAV_SIZE`, `FILL_NAV_KINDS`, `FLOATING_KINDS`, `EXPECTED`. Optional with defaults from `records.py`: every record list (missing = empty), `CLIP_MODE` (`"box"` = four walls plus ceiling as DM1; `"walls"`; `"none"` for Orbita), `CLIP_H` (1800; Nemus 2000), `THEME` (kit theme name, §2), `SKY_CUBEMAP`, `RULES` (dict merged over `DEFAULT_RULES`), `LOS_BLOCKED`, `LOS_CLEAR`, `POINTS`, `SOAK` (§5.3), `MODES`. `DEFAULT_RULES` holds exactly the constants `validate_layout()` uses today (walkable width 120, doors 240–400, ledge 120, pad zone 300 × 300, pad landing tolerance 60, `RJ_CLEARANCE_MIN` 900, spawn spacing 1000, spawn-to-weapon 800, ammo 600–1200, ramp angle tolerance 0.5°), so DM1 validates under unchanged numbers. A map overrides a rule only with a comment that cites its concept section.

**1.3 Record extensions** (new fields are trailing with defaults, so `dm1_layout.py` changes only its import line):

- `Ramp(id, x0, y0, y1, rise, dir="+x", deg=None, z0=None, fill="solid")`. `dir` ∈ `+x −x +y −y` is the uphill direction; `x0` is the foot coordinate along the run axis and `y0..y1` the band across it (for `±y` ramps `x0` is a Y value and `y0..y1` an X band: all code goes through one helper `mapgen.ramp_frame(r)` → foot point, unit direction, cross band; nobody reads the fields directly). `deg` defaults to the module's `RAMP_DEG`; `z0` to `FLOOR_Z`; `fill="solid"` makes the stepped fill boxes down to `z0` (today's behaviour), `fill="none"` leaves a free-spanning slab and the validator then demands ≥ 160 cm clear and ≥ 170 cm wide under it (D41). The validator asserts `deg` < 45.5 and reports the angle per ramp. Users: Sacrarium east ramp (`+y`), Culmen (`deg=20`, roof ramps with `z0=320`), Nemus roots and humps (`−x`, `deg=20`), Fornax chute (`deg=30`), Orbita (`z0=−320` and `z0=0`).
- `Stairs(id, x_from, x_dir, y0, y1, steps, axis="x", z0=None, tread=None, riser=None)`. `axis="y"` swaps the roles as for ramps (`mapgen.stairs_frame(s)`). Nemus pit stairs: `axis="y"`, `tread=40`. The DM1 foot rule in `expand_stairs` (first step merged, `STAIR_FOOT_EXTRA`) stays the default behaviour.
- `Oct(id, kind, mat, cx, cy, d, z0, z1, kit=None)`: a regular octagon of across-flats `d`, spawned as **four** boxes `d × d·tan 22.5° × (z1 − z0)` at yaw 0/45/90/135 (`veloxmap.octagon()`). Two squares at 0/45° make a star with concave pockets that catch the capsule; that reading of the Nemus text is rejected. In the pure-Python validator an `Oct` is a vertical cylinder: radius `d/2` where it must block (line of sight) and `0.541 d` where it must be cleared (pad arcs, widths). `kit` names the single mesh that replaces it in art mode (`Column_300`, `Trunk_300`, `Trunk_600`).
- `Hazard(id, type, x0, x1, y0, y1, z0, z1)`, `type` ∈ `lava`, `void` (§3). Validator: no item, spawn, pad, pad landing or link end inside a hazard box; for `lava` the box top is 20–60 cm below the adjoining floor top and the bed is a `Box` of kind `hazard` under it.
- `Box.kind` gains `hazard` (the lava bed: `BlockAll`, `can_ever_affect_navigation = False`, greybox material `M_Greybox_Pit`).
- `veloxmap.py`: `ramp(..., yaw=0.0)` (rotator `(0, deg, yaw)`, centre offset rotated by yaw), `stairs(..., axis="x", direction=1, z0=0.0)`, `octagon()`, `hazard()`, `clip_walls(..., mode=)`, and `CLIP_H` becomes a default argument fed from the spec module. The three test-map scripts keep calling the old signatures unchanged.

**1.4 Validator extensions** (all pure Python, all maps unless a rule says otherwise):

- **Line of sight.** The existing spawn-to-major test becomes `mapgen.los(L, a, b)`: a segment from eye height (`EYE_Z` 160 above the source floor) to 60 cm above the target, against all solids, ramp slabs (as their fill boxes) and `Oct` cylinders. `LOS_BLOCKED = [(a, b), …]` must be blocked and `LOS_CLEAR` must be clear; `a`/`b` are spawn ids, item ids, the group names `spawns`/`majors`, or names in `POINTS`. Nemus (spawns ↛ Quad/RA/Mega, Corona ↛ RA) and Sacrarium (loft ↛ ground arches) state theirs; the longest sightline into each spawn is printed as a figure.
- **Link ends on ground.** Every `Link` start and end lies on a solid top within 5 cm in Z and ≥ 45 cm (agent radius) inside its edge. This is the Orbita "death link" rule and is hard on every map; `veloxmap.drop_links()` is not called by `create_map.py` at all (links come only from `Link` records).
- **Drop rule E3 (d)** from ADR 0006: each `DROP` end has 300 × 300 of clear top, ≥ 500 cm to the wall ahead, ≥ 300 cm from any pad trigger.
- **Pad arcs:** as today, plus `RULES["pad_arc_clearance"]` (default = today's value; Culmen 60) against every solid before and after the apex, and launch speed ≤ `PAD_MAX_LAUNCH_SPEED`.
- **Free edges** (report only, a figure per map): walkable tops whose edge has a drop > 45 cm and neither a link, a wall nor a listed exemption. Culmen reads it; nothing gates on it.
- **Instancing: not in M7.** The generator keeps one `StaticMeshComponent` per kit instance and `audit_map.py` keeps its estimator. Instanced components would change the collision diff, the protrusion audit and the lightmap policy for a saving nobody has measured. If a Mac measurement shows > 600 real draw calls on Culmen, Nemus or Orbita, a G-amendment decides between merged perimeter meshes and ISM for dressing only.

**1.5 DM1 stays unchanged.** Three gates, in this order:

1. **Golden, before the refactor.** `mapgen.golden(L)` returns a JSON-able dict: the sorted `figure` lines of the validator, `all_solids(L)`, the art plan (`art_plan(L)` as tuples), `expected_counts`, `art_counts`, and every pad's simulated landing. Task P1 produces `maps/golden/dm1.json` from the **current** `create_dm1_map.py` (a throwaway script importing its functions), before any other change.
2. **`create_map.py --map dm1 --golden check`** must be byte-identical after every pipeline task (floats printed with `repr`); it runs in `infra/ci/build.sh` before the build for every map that has a golden file (pure Python, about a second). `--golden write` is allowed only in a commit that changes that map's layout module.
3. **Engine check, once per pipeline task that touches spawning.** Regenerate `L_DM1_Blockout` and `L_DM1`, run `audit_map.py --dump-actors` (new: class, `Layout:` tag, transform, scale, mesh, materials, collision profile, nav flags per actor, sorted; text) on the committed and on the regenerated maps, diff empty (transforms within 0.01 cm), `--diff-collision` empty. Then `git checkout` the two `.umap` files: **the committed DM1 maps are not rewritten by M7.** Their bytes stay as they are until the Mac bake. The two dumps go to `docs/adr/evidence/0008-dm1-golden/`.

General rule that follows: a `.umap` is committed only together with a change of its layout module or of the generator output for it (each regeneration is a new LFS object).

**1.6 Map-generic test tooling.** `velox.bots.FtDm1Reachability` becomes `velox.bots.FtMapReachability [WalkExempt=<item ids>]` in `Tests/VeloxMapReachabilityFt.cpp` (the old command name stays as an alias): Full-filter paths spawn → pickup and major ↔ major, pad link ends within 60 cm of `ComputeLandingPoint`, **every link start and end projects onto nav** with `UVeloxBotItemRouter::GetOnMeshExtent()`, and, when `WalkExempt` is given, Walk-filter paths to every pickup not listed. `fts.sh` gets one row per map. `ffa.sh` and `nettest.sh` gain `--bots N` (default 3, so stored sets re-evaluate unchanged). Each map gets `Content/Data/MoveScripts/<Name>Run.json`, judged like `DM1Run`.

### 2. Kit: a shared core plus one theme set per map

**2.1 Structure.** One Blender generator, themes as data: `Tools/blender/kit.py --theme <name>` with theme modules `Tools/blender/kit_themes/{dm1,sacrarium,culmen,nemus,fornax,orbita}.py` (piece list, material table, textures, palette classes). Output per theme: `ArtSource/Kit/<theme>.blend`, FBX under `ArtSource/Export/`, `kit_manifest_<theme>.json`. `--theme dm1` reproduces today's `kit.blend` content and manifest (regression: manifest identical apart from the file name; DM1's assets are not re-exported).

- **Core** (`SM_Kit_*`, `/Game/Environment/Kit/Meshes`): the 18 existing meshes, untouched, plus four new shared pieces that three concepts each invented under different names: `SM_Kit_Floor_800` (800 × 800 × 20; Culmen `Floor_800`, Orbita `Deck_800`, Nemus `Ground_800`), `SM_Kit_Parapet_400` (400 × 20 × 120; Culmen glass parapet, Orbita parapet), `SM_Kit_Strip_400` (400 × 10 × 5 emissive line; Culmen neon strip and rail inlay by scale, Orbita edge glow by scale), `SM_Kit_Ramp_20` (Culmen).
- **Theme pieces** (`SM_<Theme>_<Piece>`, `/Game/Environment/Kit/<Theme>/`): Sacrarium 9 (as its §12), Culmen 5 (`GlassBridge_400`, `Roof_Ridge_400`, `Maglev_Car`, `Holo_Sign`, `Skyline_Ring`), Orbita 8 (`Truss_800`, `Keel`, `Container_400`, `Dish`, `Rock_A`, `Rock_B`, `Grow_Rack`, `Backdrop_Wing`), Nemus 12 (its §13 without `Ground_800`), Fornax 7 (its §11). **45 new meshes** in total.
- **Cap.** ADR 0006 §8 stands as written: ≤ 25 distinct kit meshes and ≤ 8 materials **per map**, enforced by `audit_map.py` on the map. `kit.py` and `import_kit.py` assert per theme: core pieces the theme uses + theme pieces ≤ 25, materials ≤ 8. Resulting counts: Sacrarium 21, Culmen 21, Orbita 23, Nemus 23, Fornax 25 (no headroom: a further Fornax piece retires one).
- **Materials.** One master, `M_Kit_Master`, with two additions: (a) a tint and one scalar from **custom primitive data** (banner colour in Sacrarium, identity trim in Orbita, holo glyph index in Culmen), so per-instance variation costs no material; (b) for `MI_Fornax_Lava` an unlit, UV-panning variant by static switch plus shading-model override on the instance (if the Python API refuses the override, a second master `M_Kit_Unlit` is acceptable; the task reports which). Instances are `MI_<Theme>_<Name>`, eight per theme as the concepts list them; `MI_Kit_*` stays DM1's. Theme switch in the generator: the spec module's `THEME` selects the manifest and a `mat → MI` table; blockout mode ignores it.

**2.2 Palette assertion with classes.** Every material in a theme module carries a class; `kit.py` and `create_kit_materials.py` assert the same table:

| Class | Rule |
|---|---|
| `surface` | V ≤ 0.50, S ≤ 0.25 (unchanged); hue free |
| `fixture` | emissive, S ≤ 0.15 (white light fixtures, ice-white neon, window panes) |
| `accent` | emissive, hue in the theme's declared windows, each window ≥ 18° from every skin hue (0, 53, 128, 180, 212, 311) |
| `hazard` | emissive lava: hue 20–25°, texture mean V ≤ 0.35 (the crust rule), floor-only by 2.3 |
| `sky` | exempt from hue rules; the generator prints luminance statistics (Orbita: stars ≤ 0.5 % of the sky, planet V ≤ 0.40) |

Declared accent windows: Culmen 265° ± 10; Sacrarium 25° ± 5 (flames); Fornax 22° ± 3 (furnace mouth; lava is `hazard`); Orbita 30° ± 5 (void-edge strip); Nemus none. **Conflict resolved:** Orbita's red, green and cyan deck identity colours collide with three skin hues as emissives. They become `surface` trims (S ≤ 0.25, tinted through custom primitive data) with white `fixture` lights; only the amber edge strip is an accent, moved to hue 30° to keep 23° from Yellow.

**2.3 Emissive placement audit.** `mapgen.plan_emissive(plan)` (pure Python, beside `plan_protrusion`, runs in `--validate-only --mode art`) and the same check in `audit_map.py --emissive` on the built map: an instance whose material class is `fixture`, `accent` or `hazard` must be (a) flush: top ≤ 5 cm above a walkable top and ≤ 10 cm thick, or (b) ≥ 250 cm above the nearest walkable top below it, or (c) smaller than 0.5 m² in every projection (flames, pad rings, fixtures). `hazard` instances must lie below the adjoining walkable top. This is Culmen's rule 3 made global; it holds trivially on DM1.

**2.4 LFS budget** (limit 600 MB over the whole history, q39). Measured sizes: a kit mesh costs about 85 KB (FBX + `.uasset`), a 512 px texture about 0.45 MB (PNG + `.uasset`), `L_DM1_Blockout` 0.43 MB, `L_DM1` unbaked 1.6 MB.

| Item | Allowance |
|---|---|
| Today | 15 MB |
| 45 meshes, ≤ 25 new textures (512 px; Nemus ≤ 1024 px), five `.blend` | 25 MB |
| Five blockout maps, ≤ 4 committed versions each | 15 MB |
| Five art maps unbaked, ≤ 3 committed versions each | 40 MB |
| Built lighting data, six maps, ≤ 2 committed **Production** bakes each (lightmap budget 48 MB per map, expected 12–30) | 300 MB |
| Audio (lava, void cues), sky cubemaps | 5 MB |
| **Planned total / reserve** | **400 MB / 200 MB** |

Rules: Preview bakes are never committed; a third Production bake of a map needs the lead's OK; `infra/ci/lfs_budget.sh` (new) prints the tree total and the all-history total (`git lfs ls-files --all -s`), warns at 450 MB and fails at 600 MB; `audit_map.py` keeps printing its informational figure.

### 3. Hazards and void

**3.1 Principle.** `WorldSettings.KillZ` stays a safety net on **every** map, the void map included; a death through it is a defect. Lethal or damaging world space is always an `AVeloxHazardVolume`. So `world_kill 0` and `left_arena 0` stay hard everywhere and hazard deaths are separate, countable gameplay.

**3.2 Last attacker (server only, not replicated).** `UVeloxHealthComponent` (on the pawn, so it resets with each life) subscribes to the health set's `OnDamaged` and keeps `FVeloxLastAttacker { TWeakObjectPtr<AVeloxPlayerState> Attacker; double ServerTime; FGameplayTag DamageTag; }`. It is written when the event's instigator resolves to a player state other than the victim's own and the event carried health loss, armor loss or knockback, and its tags contain neither `Velox.Damage.Hazard` nor `Velox.Damage.Environment`. Self damage (own rocket) therefore never writes it and never clears it. `AVeloxPlayerState* GetCreditedAttacker(double Now, float WindowSeconds) const` returns the attacker if the record is younger than the window and the player state is still valid. The decision logic is a world-free function in `Combat/VeloxHazardRules.h` (unit-tested). Pure time window, no "reset on landing": simplest rule, and the one both concepts propose. When team modes arrive, a teammate is never credited (noted for the CTF ADR).

**3.3 `AVeloxHazardVolume`** (`Source/Velox/Combat/VeloxHazardVolume.{h,cpp}`): an `AActor` with a `UBoxComponent` root (static, overlaps `Pawn` only), `EVeloxHazardType Type { Lava, Void }`. Placed in the level, `bReplicates = false`, `bNetLoadOnClient = true`; overlap events are enabled on the authority only. Nothing about it is predicted or replicated: it never touches `UVeloxMovementComponent`, and clients learn of the damage through the existing GAS paths (health replication, `GameplayCue.Character.Damage`, death, kill feed). A `UVeloxHazardSubsystem` (world subsystem, `Combat/`) lists the volumes and answers `IsInHazard(Point)` and `HasHazards()` for bots and lint.

- **Lava:** on begin overlap, and then every `LavaTickSeconds` while the pawn still overlaps, apply `LavaDamagePerTick`. Per-pawn last-damage time is kept for one second after leaving, and contact damage is skipped if the last tick was less than `LavaTickSeconds` ago (no double hit from boundary jitter). The box top **is** the lava surface, so "overlapping" equals "feet below the surface". The actor ticks only while it has occupants.
- **Void:** on begin overlap, apply `EnvironmentKillDamage` once.
- **Damage path:** `VeloxEffects::MakeDamageSpec(SourceASC, Params)` with `bNoArmor`, `bNoKnockback`, `HitLocation` at the feet, `DamageScale` 1, tag `Velox.Damage.Hazard.Lava` or `Velox.Damage.Hazard.Void`. `SourceASC` is the **credited attacker's ASC** (it lives on the player state, so it outlives the attacker's pawn) with `bSelf = false`; without a credited attacker it is the victim's own ASC, exactly as `FellOutOfWorld` does. No new scoring code: the instigator flows through `FVeloxDamageEvent` into `AVeloxCharacter`'s death handling and `PlayerDied(Victim, Killer, Tags)`. Credited → frag +1 for the attacker, feed entry "A [LAVA] B" / "A [VOID] B"; uncredited → the existing suicide (death, −1). Hazard damage must not be scaled by the attacker's Quad: the task verifies `VeloxDamageExecution.cpp` and adds the test. Battle Suit against lava (Q3's enviro behaviour) is deferred; none of the five maps places a Battle Suit.
- **Tunables** in `UVeloxCombatSettings`: `LavaDamagePerTick = 25`, `LavaTickSeconds = 0.25`, `HazardKillCreditSeconds = 5.0`.
- **Tags:** `Velox.Damage.Hazard`, `.Lava`, `.Void` (native, `Core/VeloxGameplayTags`). Not under `Damage.Environment`: the runners match that substring for `world_kill`. Feed labels in `UI/VeloxHUDFormat.h`.
- **Log line** (parsed by the runners): `VeloxHazard type=<lava|void> victim=<label> damage=<n> credited=<label|none> age=<s> at=<x,y,z>`.
- `FellOutOfWorld` is not changed and never credits anyone.

**3.4 Placement.** Fornax: one `Hazard(type="lava")` per channel from bed (−60) to surface (−40) over a `Box` of kind `hazard`. Orbita: one `Hazard(type="void")` slab, top at −1400 (the concept's kill height, so its fall times hold), bottom −2000, lateral extent the footprint plus 6000 cm on every side (maximum knockback 2500 cm/s over the longest fall stays inside); `KILL_Z = −2600`; `CLIP_MODE = "none"`.

**3.5 Amendment of ADR 0006 §7 and E0.3 (e).**

- The map-independent invariants are unchanged and stay hard on all five maps: `left_arena 0`, `world_kill 0`, `no_nav_point 0`, `giveup 0`, stuck and bursts, dwell ≤ 5 s, nav build ≤ 2 s, link success ≥ 90 % per type at ≥ 30 attempts, per-instance item coverage. Orbita's proposal to split `world_kill` is not adopted; 3.1 makes it unnecessary.
- E0.3 (e) gets one exception: Orbita's lowest deck is at −320 as designed. `infra/ci/bands/L_Orbita_Blockout.sh` sets `VELOX_ARENA_FLOOR_Z=-700`, so `left_arena` fires at −2200: below the void volume, 400 cm above KillZ. A band file with no `BAND_*` value keeps the map report-only (the runner task verifies this). Fornax keeps the default 0 (the bed at −60 is not a floor).
- New metrics on every map, parsed from `VeloxHazard` and `VeloxKill`: `hazard_touch`, `hazard_death_credited`, `hazard_death_self`, per bot and per set. `hazard_death_credited` is gameplay and never judged. `hazard_death_self` has a **provisional hard gate** for "blockout done" (5.3) and gets a band by the E3 method after three report-only sets (architect ruling).
- `VeloxNavCoverage` skips samples inside a hazard volume (exempt by volume, reported as `hazard_m2=`); `NavLintIgnore` stays forbidden. Gate unchanged: `clusters − edge = 0`, `lowclear = 0`.

**3.6 Bots** (ADR 0005 amendments; all four behaviours are active only when `UVeloxHazardSubsystem::HasHazards()` is true, so the DM1 and test-map bands remain valid without re-derivation):

1. **Edge-aware overlay.** In `VeloxBotCombat::Overlay`, before a lateral, dodge or retreat wish is written: probe `Feet + Velocity2D × 0.25 s + Wish × EdgeProbeCm (150)`; if it does not project onto nav with `GetOnMeshExtent()`, try the mirrored lateral; if that fails too, drop the overlay's lateral and retreat components for this tick (the path wish remains). Result cached for 0.1 s like the hop headroom sweep. Consequence accepted for the first version: on hazard maps bots leave a ledge only through links.
2. **Hop landing guard** (extends D18): a hop is refused when `Feet + Velocity2D × HopAirSeconds` does not project onto nav. Without it Hard bots strafe-jump into Fornax's gutter.
3. **Airborne recovery** (extends D17): a bot in flight (`VeloxBotRules::IsFlight`) that is not on a link maneuver steers with full air control toward the nearest nav point at or below its height (projection with a 600 cm horizontal extent around the predicted landing). **Pad discipline:** during the airborne phase of a `JUMP_PAD` maneuver the move input is zero until ground contact (the task verifies what `TickAirborne` does today and adds a test); aim stays live.
4. **Hazard escape:** a bot whose feet are in a lava volume faces the nearest nav point (400 cm extent) and holds forward and jump; this runs ahead of `UpdateOffMeshRecovery`.

Deferred: choosing knockback weapons because the enemy stands near an edge; leading rails along pad arcs; moving Duel camp positions off the RA island and the Mega bridge (decided from the first Fornax soak).

**3.7 Test fixture.** `Tools/editor/create_hazard_test_map.py` → `L_HazardTest`: an 800 × 800 platform over a void slab, a floor with one lava channel, one pad across a gap, a dummy spawner. FTs in `fts.sh`: `FtHazard` (lava 25 per 0.25 s with death at the computed time and armor untouched; rocket a dummy into the void → frag credited; walk off → suicide; hit, wait 6 s, walk off → suicide; own rocket → suicide; Quad on the attacker does not scale lava), `FtBotEdge` (a Hard bot fights a dummy on the platform for 60 s: 0 hazard deaths), `FtBotLavaEscape` (a bot placed in lava is out within 1 s).

### 4. Per-map open questions: decisions

"Accepted" = accepted as the concept's recommendation (human, 2026-11-12: "setze alles um"). "Lead default" = the concept gave no recommendation; the conservative option is taken and is reversible by a layout-record or data change.

**Sacrarium** (§17): 1 Quad Flight pad lands on the loft as designed: lead default, reversible (one `Pad` record retargets it to the north gallery). 2 Arch axes stay straight; the offsets remain the documented fallback: lead default. 3 Fall damage: Velox has none and M7 adds none; valid for all five maps (adding it later is its own ADR): lead default. 4 Nine spawns under the DM1 rules: lead default. 5 Quad only: accepted. 6 Brazier flames are static emissive geometry; the vertex wobble is an art-pass option after the Mac verdict: lead default. 7 Name "Sacrarium": lead default.

**Culmen** (§18): 1 Quad and Haste, both `DisabledInModes` Duel: accepted (consistent with q38; Haste is the first non-Quad powerup soaked on a real map, so item coverage must show bots take it). 2 Opaque "glass": accepted; ADR 0006 §8 (no translucency) stands. 3 Maglev train: **deferred**, not in M7 (replicated mover, credited kill volume, bot awareness: own ADR if ever wanted). 4 Plasma jump is not a supported technique in M7; no route or link depends on it, no measurement task is scheduled: lead default. 5 Violet plus ice white: accepted; ice-only is the fallback after the Mac shots. 6 The layout stays point-symmetric through the blockout (CTF is on the M7 roadmap): lead default.

**Orbita** (§16): Q1 normal gravity: accepted. Q2 5 s credit window; an uncredited void death is a suicide (−1): accepted, identical to ADR 0004 §6 for environment deaths. Q3 no railings beyond the listed parapets: lead default (parapets are `Box` records). Q4 Duel first, FFA 3–6 second: lead default (as written). Q5 the Yellow Armor stays on Semita: lead default.

**Nemus** (§18): 1 Quad: lead default (Regeneration has no soak history). 2 No height fog: accepted (the concept's default). 3 Baked sun pools and painted shafts, no additive cards: accepted; ADR 0006 §8 stands. 4 `MAP_ID = "NEMUS"`, no map numbers, no TDM in M7: lead default. 5 Downhill ramp speed: no movement change in M7 (ADR 0002 stands); `NemusRun` logs entry and exit speed on Radix Magna and the human rules on the feel after the Mac playtest: lead default. 6 Instancing: no (1.4).

**Fornax** (§15): 1 Lava 25 per touch and 25 per 0.25 s: accepted. 2 Credit to the last attacker, 5 s: accepted. 3 One Yellow Armor: accepted. 4 No per-map default skin pair; enemy-skin forcing (q36) already lets a player fix the opponent's colour, and the Mac contrast shots decide whether more is needed: lead default. 5 Five spawns: lead default. 6 No new footstep surface; hazard damage uses the existing pain cues in the blockout, and one lava sizzle, one lava ambience and one void-fall cue are synthesised placeholders (q35) in the art pass: lead default.

**Conflicts with accepted ADRs, resolved:** translucency (Culmen 2, Nemus 3): ADR 0006 §8 wins. Fall damage (Sacrarium 3, Culmen §16, Nemus §16): none exists, none is added. Gravity (Orbita Q1): default. Floor at Z = 0 (E0.3 e): one exception for Orbita (3.5). ADR 0006 §3 excluded void and lava for DM1 only. `world_kill`/`left_arena`: kept, not split (3.5). Nav build ≤ 2 s (Nemus 3 700 m², Culmen): stays hard; a breach comes back for an architect ruling instead of being waived in advance. FFA soak size: the concepts ask for 6–8 bots; §5.3 fixes it.

### 5. Order, parallelism, and what "blockout done" means

**5.1 Build order:** **Sacrarium → Nemus → Culmen → Fornax → Orbita.** Sacrarium uses only DM1's vocabulary plus one `+y` ramp and proves the generic pipeline. Nemus exercises every generator extension (yawed ramps, Y stairs, octagons, clip 2000, line of sight) with no code. Culmen is the largest link set (11 pads, about 45 links, 20° ramps) and the likeliest to need bot iteration. Fornax brings the hazard code on a small enclosed map. Orbita comes last: void, all four bot behaviours, and the fastest pad arcs the netcode has seen.

**5.2 Parallelism on this VM.**

- Pure-Python work needs no editor and no build: the five spec modules with `--validate-only` green (wave 1) can be written by five agents at once.
- Engine work is serial under the soak lock. At most **two** worktrees with a compiled module exist at a time: one "maps" integration worktree reused for all generate/lint/FT/soak tasks, and one C++ worktree for the hazard and bot track. Each agent checks `df -h /` first and reports DISK PAUSE below 5 GB.
- The hazard track (H1–H6) runs beside waves 0–2 and must be merged before the Fornax build task.
- Raw soak logs go to `/mnt/oldroot/velox-scratch/<task>/`; an evidence directory in the repo holds summaries and stays ≤ 5 MB per map.
- Mac Studio only (from about 2026-11-20): lighting bakes, real draw-call and frame-time numbers, the contrast shots, human playtests. Nothing in "blockout done" waits for it.
- Blockout maps are not added to `MapsToCook` and not to the public rotation by these tasks; the lead adds them when the first Mac playtest package is cut. Hazard tags change the net version (Context 7): the next production deploy must ship server and client downloads together.

**5.3 "Blockout done", per map.** All of: (a) `create_map.py --map <slug> --validate-only` green and the golden file written; (b) `navlint.sh L_<Name>_Blockout` PASS (`clusters − edge = 0`, `lowclear = 0`); (c) `FtMapReachability` green in `fts.sh` (with the map's `WalkExempt`); (d) `<Name>Run` PASS at 100 ms / 2 % loss; (e) the report-only soak sets below with **every map-independent invariant PASS** (stuck Σ ≤ 5 per three bots, pro rata) and the hazard gate where one applies; (f) evidence under `docs/adr/evidence/0008-<slug>-blockout/`; (g) the art-plan draw-call estimate is **not** part of it (deferred to the art pass and the Mac). Bands (`infra/ci/bands/L_<Name>.sh`) follow by architect ruling after (e).

| Map | Soak sets (quiet host) | Extra gate |
|---|---|---|
| Sacrarium | BotRun × 3; FFA × 10 at 3 bots; FFA × 10 at 6 bots (band basis) | coverage 38/38 |
| Nemus | BotRun × 3; FFA × 10 at 3; FFA × 10 at 6 | dwell on the Corona inside the DM1 camp band (report); nav build time recorded |
| Culmen | BotRun × 3; FFA × 10 at 3; FFA × 10 at 6; one FFA × 10 at 8 report-only (cost not judged) | Haste and both far 50 HP taken |
| Fornax | BotDuel × 3; BotRun × 3; FFA × 10 at 3 | `hazard_death_self` Σ ≤ 6 over FFA × 10 and ≤ 1 per BotDuel round; RA and Mega taken every round |
| Orbita | BotDuel × 3; FFA × 10 at 4 | `hazard_death_self` Σ ≤ 10 over FFA × 10 and ≤ 2 per BotDuel round |

The `hazard_death_self` numbers are provisional gates chosen so that a broken edge behaviour cannot pass as "report-only"; the band ruling replaces them.

## Consequences

- One generator and one validator serve six maps; a new map is a spec module plus a golden file. DM1 is guarded by a pure-Python golden check on every build and its committed maps are not touched.
- Hazards add one actor class, one subsystem, three settings, three tags and one record on the health component; scoring, feed, replication and prediction code are unchanged.
- The hard soak invariants keep their meaning on every map; hazard maps add metrics instead of exceptions.
- Bot behaviour on DM1 and the test maps is unchanged by construction (`HasHazards()` gate). The price: edge awareness is not exercised on the maps with the most soak history.
- The kit grows from 18 to 63 meshes in six themes, each map still within 25 × 8; planned LFS use is about 400 MB of 600.
- Risks carried forward: nav build time on Nemus and Culmen; link success with 22–45 links per map (DM1 needed two iterations for 16); prediction at 3 000 cm/s pad launches on Orbita (a correction mid-arc is a death); real draw calls on the three open maps; all contrast questions (neon, lava, foliage) wait for the Mac.

## Alternatives considered

- **Copy `create_dm1_map.py` per map.** Fastest for one map, five diverging validators after that. Rejected.
- **Void through `KillZ` with credit inside `FellOutOfWorld`** (Orbita §10). Fewer actors, but KillZ deaths would be gameplay on one map and a defect on five, and `world_kill` would need a per-map split in three runner scripts. Rejected for 3.1.
- **Engine `APainCausingVolume` / `AKillZVolume`.** They use `TakeDamage` and `UDamageType`, bypass GAS, armor rules, tags, the feed and kill credit. Rejected.
- **Lava as a physics volume with swimming.** Changes `UVeloxMovementComponent`, prediction and bots for a feature Q3 players experience as "the floor hurts". Rejected; lava is a floor.
- **Credit until the victim touches ground again** (UT style). More state, and a long pad arc after a hit would extend credit arbitrarily. Rejected for the fixed window.
- **A separate kit per map (no shared core).** About 100 meshes and duplicated floors, walls and ramps. Rejected. **One global kit capped at 25** cannot hold five themes. Rejected.
- **Edge-aware bots on every map.** Cleaner, but invalidates the DM1 and `L_ItemTest` bands. Deferred until those bands are re-derived for another reason.
- **Instanced static meshes in the generator now.** Decided against in 1.4.
- **Lyra.** Nothing new is adopted. Lyra has no hazard volumes or last-attacker credit; its damage execution and health-set pattern (already adopted in ADR 0003) is what the hazard volume plugs into. Lyra's experience-driven map definitions are not adopted: a Velox map stays a level plus a layout module.

## Implementation notes for dev/dev-hard

Tasks are 1–3 h each. "maps WT" = the shared integration worktree; every engine run is under the soak lock. Each task ends with `create_map.py --map dm1 --golden check` green.

**Wave 0 — pipeline**

| Id | Tier | Task | Files | Depends | Acceptance |
|---|---|---|---|---|---|
| P1 | dev | DM1 golden before any change; `audit_map.py --dump-actors` | `Tools/editor/maps/golden/dm1.json`, `audit_map.py`, `docs/adr/evidence/0008-dm1-golden/` | — | golden written from the unmodified generator; actor dumps of both committed DM1 maps stored |
| P2 | dev | Split the generator (1.1, 1.2), shim, `--golden` | `mapgen.py`, `create_map.py`, `create_dm1_map.py`, `maps/records.py`, `maps/dm1_layout.py` (import line), `Tools/editor/README.md`, `infra/ci/build.sh` | P1 | golden check identical; regenerated DM1 maps: actor-dump diff and `--diff-collision` empty; committed `.umap` files unchanged |
| P3 | dev | Ramp/Stairs extensions, yaw in `veloxmap`, per-map clip (1.3) | `records.py`, `mapgen.py`, `veloxmap.py`, `create_map.py` | P2 | `--selftest` covers four ramp directions, `z0`, `fill`, Y stairs, 20°/30°; test maps regenerate with unchanged actor counts |
| P4 | dev | `Oct`, `Hazard` record, LOS rules, link-end, drop and pad-clearance rules, free-edge report (1.3, 1.4) | same | P3 | `--selftest` cases per rule (one passing, one failing fixture each); DM1 golden identical |
| P5 | dev | `FtMapReachability`, `fts.sh` rows, `--bots N` in `ffa.sh`/`nettest.sh` (1.6) | `Tests/VeloxMapReachabilityFt.cpp`, `infra/ci/fts.sh`, `ffa.sh`, `nettest.sh`, `README.md` | — | `fts.sh` 7/7 as before with the alias; stored DM1 sets `--reeval` unchanged |

**Hazard track (C++ worktree; parallel to waves 0–2)**

| Id | Tier | Task | Files | Depends | Acceptance |
|---|---|---|---|---|---|
| H1 | dev-hard | Last attacker, tags, settings, `VeloxHazardRules` (3.2) | `Combat/VeloxHealthComponent.*`, `Combat/VeloxHazardRules.h`, `Combat/VeloxCombatSettings.h`, `Core/VeloxGameplayTags.*`, `Tests/` | — | automation tests: window edge, self damage ignored, hazard damage ignored, attacker gone |
| H2 | dev-hard | `AVeloxHazardVolume`, `UVeloxHazardSubsystem`, log line, `veloxmap.hazard()`, `L_HazardTest`, `FtHazard` (3.3, 3.7) | `Combat/VeloxHazardVolume.*`, `Combat/VeloxHazardSubsystem.*`, `veloxmap.py`, `create_hazard_test_map.py`, `Tests/`, `fts.sh` | H1, P4 | `FtHazard` green, all six cases; default soak list 13/13 |
| H3 | dev | Feed labels, runner metrics, nav-lint exemption, empty band file stays report-only (3.5) | `UI/VeloxHUDFormat.h`, `Bots/VeloxNavCoverage.cpp`, `nettest.sh`, `ffa_summary.py`, `ffa_set.py`, `lib.sh` | H2 | `navlint.sh L_HazardTest` PASS with `hazard_m2 > 0`; runners print the three metrics; DM1 `--reeval` unchanged |
| H4 | dev-hard | Edge-aware overlay and hop landing guard (3.6.1–2) | `Bots/VeloxBotCombat.*`, `Bots/VeloxBotPathFollowingComponent.*`, `Bots/VeloxBotSettings.h` | H2 | `FtBotEdge` green; BotRun × 3 on `L_DM1_Blockout` inside its bands (gate proves "unchanged") |
| H5 | dev-hard | Airborne recovery, pad discipline, hazard escape (3.6.3–4) | `Bots/VeloxBotController.*`, `Bots/VeloxBotManeuver.*` | H4 | `FtBotLavaEscape` green; a bot knocked off the test platform edge with 300 cm/s lands back in ≥ 8 of 10 seeded trials |
| H6 | architect | Design review of H1–H5, one pass | `docs/adr/reviews/0008-hazard-review.md` | H5 | defects ranked, D/G amendments written |

**Wave 1 — spec modules (pure Python, parallel)**

| Id | Tier | Map | Files | Depends | Acceptance |
|---|---|---|---|---|---|
| S1 | dev | Sacrarium | `maps/sacrarium_layout.py` | P3 | `--validate-only` green; pad landings, sightline figures and RA ↔ Mega distance in the task notes |
| S2 | dev | Nemus | `maps/nemus_layout.py` | P4 | same; LOS rules of its §17.1 |
| S3 | dev | Culmen | `maps/culmen_layout.py` | P4 | same; 11 pad arcs clear solids by ≥ 60 cm; free-edge report attached |
| S4 | dev | Fornax | `maps/fornax_layout.py` | P4 | same; hazard rules; island gaps 250, rim 60 |
| S5 | dev | Orbita | `maps/orbita_layout.py`, `infra/ci/bands/L_Orbita_Blockout.sh` | P4 | same; no link end off a deck; void slab and KillZ per 3.4 |

Deviations from a concept's coordinates are allowed where the validator demands them (the graph and the rules are binding, ADR 0006 §3); each is listed in the task notes.

**Wave 2 — build and bot iteration (maps WT, serial, in the order of 5.1)**

| Id | Tier | Map | Files | Depends | Acceptance |
|---|---|---|---|---|---|
| B1 | dev | Sacrarium | `Content/Maps/Sacrarium/L_Sacrarium_Blockout.umap`, `Content/Data/MoveScripts/SacrariumRun.json`, `fts.sh` row, golden, evidence | S1, P5 | 5.3 (a)–(f) |
| B2 | dev | Nemus | same pattern | S2, P5 | 5.3; `JUMP_FAST` gap link removed if < 90 % (concept §15) |
| B3 | dev-hard | Culmen | same pattern | S3, P5 | 5.3; roof pads agree with `DumpPadLandings` within 60 cm |
| B4 | dev-hard | Fornax | same pattern | S4, H5, H3 | 5.3 incl. hazard gate |
| B5 | dev-hard | Orbita | same pattern; `OrbitaRun` flies every pad with and without input | S5, H5, H3 | 5.3 incl. hazard gate; two-client pad check at 100 ms / 2 % logged with correction counts |
| B6 | architect | Band rulings, one per map as its sets arrive | `infra/ci/bands/L_<Name>.sh` (+ `_Blockout.sh` sourcing it), G-amendments | B1–B5 | bands by the E3 method; `hazard_death_self` banded for Fornax and Orbita |

A build task that still fails an invariant after one layout iteration is split by the lead (escalation rule); B1 and B2 move to `dev-hard` if the failure is link success.

**Wave 3 — kit and art pass (after a map's blockout is done; listed so the lead can size M7, detailed when scheduled)**

| Id | Tier | Task | Acceptance |
|---|---|---|---|
| K1 | dev | Kit themes in `kit.py`, `create_kit_materials.py`, `import_kit.py`; palette classes; custom-primitive-data tint; four shared core pieces (2.1, 2.2) | `--theme dm1` manifest identical; DM1 assets untouched; per-theme caps asserted |
| K2 | dev | `plan_emissive`, `audit_map.py --emissive`, `infra/ci/lfs_budget.sh` (2.3, 2.4) | selftest fixtures; DM1 passes |
| K3–K7 | dev | One theme kit each (pieces, eight instances, textures, sky cubemap) | manifest within caps, palette green, import verified |
| A1–A5 | dev (Culmen, Orbita: dev-hard) | Art mode per map: `mat → MI` table, tile policy, lights and captures from records | `--diff-collision` empty, protrusion ≤ 5 cm, emissive audit green, budgets of ADR 0006 §8, confirmation soak inside the map's bands |
| M1 | human + dev | Mac Studio: Production bakes, `stat` numbers from each map's worst view, contrast shots, playtest checklists | `Velox.Maps.LightingBuilt`; verdicts recorded in STATUS |

That is 5 + 6 + 5 + 6 = **22 tasks to five blockouts** (13 `dev`, 7 `dev-hard`, 2 `architect`), plus 13 for the kit, the art pass and the Mac.

## Open questions for the human

None blocks the blockouts. Two confirmations the lead should collect when convenient: (54) the public server keeps serving `L_DM1` only until a new map has passed a Mac playtest, and Orbita never becomes the default public map (its own risk 4); (55) the LFS plan of 2.4 (about 400 MB of 600, at most two committed Production bakes per map).

## Deliberately deferred

Culmen's maglev train; fall damage; Battle Suit against lava; plasma jump as a designed technique; instanced kit components; bots that use knockback tactically near edges; rail lead along pad arcs; a CTF variant of Culmen; fog and additive light shafts; per-map default skin pairs; edge-aware bots on non-hazard maps.

## Amendments

None yet.
