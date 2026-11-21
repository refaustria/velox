# 14 (S3) — Culmen spec module (pure Python)

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done for what the generator can express (pure Python); 5 raised pads and 1 spawn level pending a generator feature (see below)
**Depends on:** P4
**Wave:** 1 spec modules (parallel)

## Goal
Write the Culmen layout as a spec module from its concept. Pure Python, no editor, no build; may run in parallel with the other spec tasks. Deviations from a concept's coordinates are allowed where the validator demands them (the graph and the rules are binding, ADR 0006 §3); list each in the task notes.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/culmen_layout.py`
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0008-m7-five-maps.md` — §1.2 (spec contract), §1.3-1.4, §2.1 (theme pieces), §4 (Culmen decisions), §5.3; concept `docs/maps/culmen.md`
- ADR 0006 (map pipeline; its hard invariants and budgets stay in force) and `docs/tasks/m7-maps/README.md` (rules)

## Acceptance criteria
- [ ] `create_map.py --map culmen --validate-only` green
- [ ] All 11 pad arcs clear solids by >= 60 cm
- [ ] Free-edge report attached
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
`Tools/editor/maps/culmen_layout.py` written (62 solids, 46 pickups, 9 spawns, 37 links: 6 JUMP_PAD, 4 ROCKET_JUMP, 6 JUMP_FAST, 21 DROP). `create_map.py --map culmen --validate-only` green, `--selftest` OK, `--mode art --validate-only` OK (378 kit instances, 0 protrude), DM1 golden OK. No golden written.

**Generator gaps found (need P-task / lead; not edited here).**
1. **`Pad` has no floor Z.** `pad_launch`/`pad_arc`/`pad_arc_clearance_problem`, the link start (`(x, y, 0)`) and `spawn_pads_and_links` (`vm.jump_pad` puts the actor at z 70) all assume Z = FLOOR_Z. Culmen has 5 pads on raised floors (P3/P4 Spire deck 320, P5/P6 roofs 640, P8 car roof 320). They are in `PADS_RAISED` + `PAD_FLOOR_Z` + `LINKS_PENDING`, NOT in `PADS`/`LINKS`. Verified with a throw-away proxy (FLOOR_Z = pad floor + 0.5): see table. Also `pad_arc` hits the deck under the pad at t = 0 unless the origin is lifted. When `Pad.z` exists: `PADS += PADS_RAISED`, `LINKS += LINKS_PENDING`, RULES pad_count 11 / JUMP_PAD 11, EXPECTED jumppad/targetpoint 11, navlink 42.
2. **`Spawn` has no z** (LOS uses `FLOOR_Z`, spawner `FLOOR_Z + 100`): the concept's S8-S10 on L1 are not expressible. S8/S9 moved to L0 (S8 (-3000,-1700) PG-W, S9 (-3200,1750) SG-W), S10 dropped: 9 spawns instead of 10 (no further L0 spot is both >= 10 m from the rest and <= 8 m from a weapon).
3. `pad_arc` returns the discretised landing sample (up to ~8 cm below the top at 240 Hz); `pad_arc_clearance_problem` then treats a top more than 1 cm above that sample as "not the landing solid" and grows it in Z, so a genuine landing fails. Worked around by +0.1 cm target-Z tuning (P1 +2.1, P7/P9 +0.5, P3/P4 +1.1). Fix: return the interpolated top intersection.
4. `Haste` is gated like the Quad (`DisabledInModes` Duel in the layout): `create_item_content.py` must list the tag on `DA_Item_Powerup_Haste` or `spawn_items` fails.

**Pad arcs (all 11, 60 cm clearance, `pad_arc_clearance` 60; "limit" = largest clearance the arc still meets).**
| Pad | Floor | Simulated landing | vs concept | Clears solids by |
|---|---|---|---|---|
| P1 | 0 | (-3640, 999, 639) | 1 cm | >= 90 |
| P2 | 0 | (3640, -999, 639) | 1 cm | >= 90 |
| P7 | 0 | (349, 2000, 319) | 1 cm | >= 200 |
| P9 | 0 | (0, -2151, 319) | 1 cm | >= 80 |
| P10 | 0 | (-3191, -1022, 320) | 30 cm | >= 72 |
| P11 | 0 | (3191, 1022, 320) | 30 cm | >= 72 |
| P3 | 320 | (-2904, -451, 639) | 4 cm | >= 180 |
| P4 | 320 | (2904, 451, 639) | 4 cm | >= 180 |
| P5 | 640 | (-350, 350, 319) | 0 | >= 170 |
| P6 | 640 | (350, -350, 319) | 0 | >= 170 |
| P8 | 320 | (-2301, 1099, 319) | 2 cm | >= 68 |
Landing zones: RampHeads 320 x 600 (>= 300), Car 1200 x 400, Viaduct 6000 x 500, EndTerr 1200 x 600, Towers 1200 x 1600, Spire 1200 x 1200, TerraceW 600 x 2800.

**Deviations from the concept (and why).**
- D1 Footprint Y -2400..2400 (concept -2400..2200): a skyline `Wall_N` (Y 2200..2400, 10 m high) makes the footprint symmetric and the perimeter clip flush.
- D2 Clip is `CLIPS` records (4 walls + `Sky_Lid` at 1800, id not `Clip_*`) instead of `CLIP_MODE`: the RJ clearance rule needs a ceiling clip with a non-`Clip_` id, and the free-edge report sees the perimeter. RJ clearances 1480 / 1800.
- D3 P5/P6 are "downward min-speed arcs" in the concept; the validator only knows upward pads. The concept's arc (apex 1098, 1.56 s) is identical to an upward pad whose target is the apex: targets (-1708, 463.2, 1170.8) / mirror. P1/P2/P3/P4/P7/P9 targets +0.1..2.1 cm (item 3).
- D4 Apex raised for the 60 cm clearance: P10/P11 target z 534.6 (concept 490; +39 became >= 72), landing 30 cm from the concept; P8 target z 930.5 (concept 770) because the Porticus ridge is a flat 480 box in the blockout.
- D5 Roof ramp bases: `RampBaseNW/SE` (z 0..320) under the `fill="solid"` ramps (their fill starts at z0 = 320). Clivus head landings `LandW/LandE` are 300 x 442 podiums (the concept's 70 cm head would break the 120 cm width rule); `BrW/BrE` start at x +-1700.
- D6 Kiosks are 250 high boxes (kind wall, 50 deg ridge only in art); a player dropping from the Viaduct can stand on them in the blockout. Porticus ridge is flat 480.
- D7 Drops: 21 instead of ~26. E3 (d) forbids: Viaduct north edge (Porticus S 250-500 cm ahead), Spire N/S edges and car south edge (Porticus 250-600 cm ahead), terrace south ends (Viaduct ahead), car east edge (P7 150 cm), terrace inner edge in front of the Clivus. JUMP_FAST: 6 (three spots, both ways) instead of 4. RJ5/RJ6 have no link, as in the concept.
- D8 BrSW/BrSE and the End Terrace edges stay without parapets (concept stubs only on BrW/BrE).
- D9 Items as the concept; PG-W/SG-E landings coincide with the pickups (concept). Item ids: `PG_W`, `SG_E`, ... (two PG, two SG, Cells/Shells `_W`/`_E`).

**Free-edge report (`--free-edges`, report only, 280.0 m total):** BrSE 13.0, BrSW 10.0, Car 18.0, EndTerrNE 12.0, EndTerrSW 10.0, LandE 3.5, LandW 3.5, RampBaseNW 8.5, RampBaseSE 8.5, RampHeadNW 3.0, RampHeadSE 3.0, Spire 34.0, TerraceE 19.5, TerraceW 17.5, TowerE 29.5, TowerW 29.5, Viaduct 57.0 m. These are the E3-blocked edges above (no link possible) plus roof outer faces; nothing gates on it.

**Other figures.** Spawn to majors visible 0 %; BrW-BrE blocked by the Spire core 0 % visible; Trench ends (68 m) and Viaduct ends (56 m) clear (0 % blocked); every spawn <= 8 m from a weapon; ammo 8.0-10.8 m from its weapon.
