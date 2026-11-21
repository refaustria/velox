# 23 (P6) — Generator gaps found by the five spec modules

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done
**Depends on:** P4, S1–S5 (all merged)
**Wave:** 1b (pure Python, no engine)

## Goal
Close the generator gaps that S1–S5 hit, so every map can express its concept (raised pads/spawns, maps without Quad, open sky) and every spec validates green. Lead-added task (2026-11-12), not in ADR 0008's list.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/records.py`, `Tools/editor/mapgen.py`, `Tools/editor/veloxmap.py`, `Tools/editor/create_map.py`
- `Tools/editor/create_item_content.py` (Haste Duel tag only; asset regeneration is engine work, deferred to B3)
- `Tools/editor/maps/{orbita,culmen}_layout.py` — only to fold the prepared pending lists (PADS_ELEVATED/SPAWNS_ELEVATED, PADS_RAISED/PAD_FLOOR_Z/LINKS_PENDING) into the main lists and to remove the sky/ceiling lids (Ceil_Hub, Sky_Lid) once the rule no longer needs them
- `Tools/editor/README.md`, this task file

## Gaps (verbatim from the S-task reports)
```
From S5 Orbita:
- records.Pad and Spawn need trailing z (elevated pads/spawns); mapgen + veloxmap.jump_pad/player_start must use it (PADS_ELEVATED/SPAWNS_ELEVATED in orbita_layout to fold in).
- per-instance item mode gating (YA_N, Scopulus 25 Health, Specula shards) in spawn_items.
- Link needs required_speed.
- pad_arc_clearance_problem must exclude the landing deck itself (landing 1-6 cm below top).
- rocket-jump clearance rule must work without a ceiling (open-sky maps); then remove orbita Ceil_Hub lid (lead decision: no lid).
- longest-sightline figure caps at 300 m on open maps: report "open" / use a sensible cap.
From S4 Fornax:
- validate_layout (~line 899) requires a Quad disabled in Duel on every map; must be "if a Quad exists, it is disabled in Duel" (Fornax has no powerup).
- Spawn.z (same as S5) — then move Fornax S1/S5 back to L1 per concept.
- Link both_ways flag (optional convenience).
- link start ring rule (45 cm) vs 50 cm stair treads: check rule is sane for tread-start hops.
From S2 Nemus:
- Pad/Spawn z (same) — after P6, Nemus restores concept Z (pit 0, forest 320, ring 640, Corona 960) and P1 back into the pit (lead decision; keeps ADR 0006 lowest-floor-0).
- art plan 600 kit instances vs concept 403: B2 must check draw calls.
From S3 Culmen:
- Pad.z / Spawn.z (pad_launch, pad_arc, spawn_pads_and_links, spawn_starts_and_camera, LOS code). Culmen has PADS_RAISED, PAD_FLOOR_Z, LINKS_PENDING; S10 dropped, S8/S9 moved to L0.
- pad_arc returns a discretised landing sample up to ~8 cm below top -> return interpolated top so pad_arc_clearance_problem stops failing genuine landings (same as S5 finding).
- DA_Item_Powerup_Haste needs Duel DisabledInModes in create_item_content.py (asset regen = engine, B3).
- Culmen/Orbita both added sky lids only for rocket-jump clearance rule -> rule must work without ceiling.
```

## Lead decisions (2026-11-12, reversible)
- Pad and Spawn get an optional trailing `z` (default `FLOOR_Z`); DM1 golden must stay byte-identical.
- Quad rule becomes "if a Quad exists, it is disabled in Duel".
- Rocket-jump clearance works without a ceiling on open-sky maps; Orbita and Culmen lose their helper lids.
- Nemus Z restore and Fornax spawn restore are follow-up tasks (S2b, S4b), not P6.

## Acceptance criteria
- [x] `create_map.py --map <m> --validate-only` green for dm1, sacrarium, nemus, culmen, fornax, orbita
- [x] Orbita has all 6 pads and S7 in its main lists; Culmen all 11 pads, 10 spawns per concept
- [x] `--map dm1 --golden check` byte-identical; `--selftest` green with a pass + fail fixture per new rule
- [x] Notes list every API change S2b/S4b and the B-tasks must know

## Notes / outcome
Branch `wip/m7-23-p6-generator-gaps`. `--validate-only` green: dm1 (137 solids), sacrarium, nemus, culmen (62 solids, 46 items, 10 spawns, 42 links, 11 pads), fornax (5 spawns, no Quad), orbita (54 solids, 7 spawns, 20 links, 6 pads). DM1 golden byte-identical, `--selftest` green (new `selftest_p6`: pass + fail fixture per rule).

API changes (S2b / S4b / B-tasks):
- `Pad(id x y target land z=None)`, `Spawn(id x y yaw z=None)`: z = deck height under it (absolute). Link starts of raised pads must carry that z (`Link.start`); `mapgen.pad_z` / `spawn_z`. `veloxmap.jump_pad(..., floor_z=0.0)` (only veloxmap change).
- `Link(id type start end pad required_speed=0.0 both_ways=False)`. Link-start ring (45 cm) unchanged: Fornax passes with 50 cm treads, no relaxation needed.
- Rules: Quad optional; new rule key `rj_open_sky` (default True); RJ clearance also counts a solid overhead; sightline / RJ clearance figures can be the string `"open"`.
- `mapgen.pad_arc(..., interpolate=False)`; `pad_arc_clearance_problem` skips the landing deck (top 0..8 cm above the landing, landing inside the footprint).
- `spawn_items`: per-instance gate tag `DisabledInMode:<tag>` (actor tag) where the definition does not list the mode; the C++ side must read it (B-tasks). Haste DA now Duel-disabled in `create_item_content.py` (assets not regenerated).
- Orbita: Ceil_Hub removed, pads/spawns folded. Culmen: Sky_Lid removed, PADS_RAISED folded (PADS_RAISED / PAD_FLOOR_Z kept as data), LINKS_PENDING removed, counts 11 pads / 42 links / 10 spawns.
- Culmen S9 / S10 deviate from the concept: at the concept positions both see RA over open sky (spawns -> majors rule). S9 (-2500, -2150) -> (-2500, -2000); S10 (2300, -1000) -> (2000, -1250); both still L1, >= 10 m apart, 7.2 m from a weapon.
- Not done (follow-ups): Nemus Z restore (S2b), Fornax spawn restore S1/S5 to L1 (S4b); the Culmen target-Z +0.1 tuning workarounds were left in place (arcs still pass).
