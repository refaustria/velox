# Task M5-03 — nav-coverage lint, report-only run on the three test maps

Run 2026-10-31 on `m5-map-characters` (31c1555 + task 03), `infra/ci/navlint.sh /Game/Maps/<Map> --report-only`,
defaults `Grid=50`, `MinClusterM2=1`. Nav as built by the server: `agent_radius=45 agent_height=140 step=45 cell=19
cell_height=10 slope=45.5`. The test maps carry no `Layout:` tags yet (task 02 adds them), so `layout=-` everywhere;
`actor=` is the actor label of the surface the cluster lies on, `over=` the first pawn-blocking surface above the
cluster centre. `kind=walkin` = the cluster borders covered floor (a walking bot can step into it), `isolated` = it does
not (reached only by a jump, pad, rocket jump or knockback). The gate (ADR 0006 section 6) counts both kinds.

| Map | walkable m² | covered % | clusters (walkin / isolated) | lowclear polys (inside solid) | lint time |
|---|---|---|---|---|---|
| `L_ItemTest` | 2727.50 | 91.92 | 11 (9 / 2) | 0 (0) | 0.042 s |
| `L_CombatTest` | 1600.00 | 98.73 | 3 (0 / 3) | 6 (6) | 0.025 s |
| `L_MovementTest` | 9164.00 | 97.66 | 19 (15 / 4) | 13 (13) | 0.113 s |

Classes used below: **E** = expected (deliberate test geometry, or a surface that is unreachable by design under D17's
"≥ 120 cm wide or blocked / unreachable by design" rule), **F** = finding for the lead (not fixed in this task),
**A** = lint artefact (the lattice, not the map).

## L_ItemTest

| i | m² | centre | extent | actor | kind | over | class | interpretation |
|---|---|---|---|---|---|---|---|---|
| 0 | 70.00 | (4700,1150,5) | 2200×1500 | Room_Floor | isolated | none | E | The D17 room's floor slab extends 100 cm beyond its walls on every side: an exterior ledge ring inside the nav bounds, unreachable. |
| 1 | 62.00 | (4700,1150,255) | 2000×1300 | Room_Wall_N | isolated | none | E | The D17 room's wall tops (100 cm wide, Z 250): D17 width class, unreachable. |
| 2 | 50.00 | (0,1550,258) | 5000×100 | Corridor_Wall | walkin | Corridor_WallCap_RampTop | E | Corridor wall top (100 cm wide). `walkin` only through the classification's dZ tolerance (95 cm): the cap top (265) is 51 cm above the YA ramp-top platform, more than a step, so not walkable from there. |
| 3 | 12.00 | (4700,650,5) | 1200×100 | Room_Floor | walkin | none | E | D17 room south slot (`ROOM_SLOT_W` 100 cm) — the deliberate off-mesh bed of `FtOffMesh`. |
| 4 | 10.00 | (4800,1650,5) | 1000×100 | Room_Floor | walkin | none | E | D17 room north slot, same. |
| 5 | 4.50 | (2100,1450,5) | 500×100 | Ground | walkin | none | F | 100 cm slot between the Red Armor platform's north face (Y 1400) and the corridor wall (Y 1500), X 1850–2350: a D17 bed of the D41 / D44 / D45 shape. |
| 6 | 4.00 | (-1500,1450,5) | 400×100 | Ground | walkin | none | F | The same slot north of the Mega platform (X −1700…−1300). |
| 7–10 | 2.00 each | (±200,0,205), (0,±200,205) | 200×100 | Cross_X / Cross_Y | walkin | none | E | The four arms of the central cross (100 cm wide, top 200): D17 width class, reachable only by a jump. `walkin` because Recast leaves a tiny poly at the crossing (the chamfer distance there exceeds the erosion), which the arms' inner cells border. |

**The known D17 bed north of the YA ramp is not reported**: since D41 (task 04i) the slot carries `Ramp_NorthSlotSlab`
and its fills, which continue the ramp surface to the corridor wall, and its cells now project to the ramp's nav within
the tolerance. The two slots of the same shape that remain are clusters 5 and 6 (north of the Mega and RA platforms).
D41 says the YA slot "stays — it is a real-map shape and the D17 bed"; whether 5 and 6 are the intended beds now
(tag `NavLintIgnore`) or should be sealed like the YA slot is the lead's call.

## L_CombatTest

| i | m² | centre | extent | actor | kind | class | interpretation |
|---|---|---|---|---|---|---|---|
| 0 | 8.00 | (-1300,1500,255) | 400×200 | Cover_07 | isolated | E | Cover block top (400 × 150, Z 250): narrower than 2 × 57 cm erosion + capsule, no nav; not jumpable (250 cm). |
| 1 | 6.00 | (-400,1000,255) | 300×200 | Cover_02 | isolated | E | Same (300 × 200, Z 250). |
| 2 | 6.00 | (900,1600,205) | 200×300 | Cover_08 | isolated | E | Same (150 × 300, Z 200). |

**lowclear 6 (all inside=1)**: nav islands at Z 10 *inside* `Cover_01`…`Cover_06` (centres (1197,902), (−399,1007),
(200,−104), (504,304), (1302,−104), (−1197,504)). The combat map's cover boxes lack
`fill_collision_underneath_for_navmesh` (the M4-04e class: Recast leaves a walkable floor span inside a solid box).
**F**: harmless today (unreachable islands; the bots' projections never start inside a box), but it fails the gate;
fix is one line in `create_combat_test_map.py` (`cover` into the fill kinds) when the map is next regenerated (task 02's
library makes that the default for solid boxes).

## L_MovementTest

| i | m² | centre | extent | actor | kind | over | class | interpretation |
|---|---|---|---|---|---|---|---|---|
| 0 | 126.00 | (2550,0,255) | 5300×1200 | CorridorWall_L | isolated | none | E | Strafe corridor wall tops (L/R/Back/End, 100 cm wide, Z 250). |
| 1 | 15.00 | (1175,3100,1124) | 250×600 | RampPlatform_45deg | walkin | RampPlatform_45deg | F | West 250 cm of the 45° ramp's top platform (top Z 1131) carry no nav; the same platform has nav islands inside it (lowclear 1–4, Z 10 and Z 1075). Cause not isolated in this task; the 45° ramp is 0.5° under `AgentMaxSlope`, so Recast's voxel slope test is marginal there. Unimportant for play (bots do not use the ramp lanes), but it fails the gate. |
| 2, 3, 11, 12 | 14 / 14 / 2 / 2 | X 125…1025, Y 5900 | ≤ 350×400 | Stairs_60cm_* | isolated (11: walkin) | none | E | The "must block" 60 cm stairs: treads 80 cm deep, unclimbable at step 45, no nav by design (the clusters span several treads because the dZ tolerance is 95 cm). |
| 4 | 9.25 | (625,950,5) | 650×700 | Ground | walkin | Ramp_10deg | F | **D41's exact class**: the floor under the 10° ramp slab where the underside clearance is ≥ 140 cm (a pawn fits) but Recast generates no mesh (voxel clearance just under 14 cells). Movement test map only; fill or tag. |
| 5 | 4.25 | (125,3050,5) | 150×700 | Ground | walkin | Ramp_45deg | F | Same under the 45° ramp's low end. |
| 6, 7, 9, 10, 14, 15, 16 | 1.5–4.25 | Y 650 / 1350 / 2050 / 2750 / 3450, X 225…2000 | ≤ 450×100 | Ground | walkin | none | F | 100 cm slots between adjacent ramp lanes / ramp platforms (lanes 600 wide on a 700 pitch): D17 beds next to the open floor. |
| 8 | 3.00 | (1975,1000,283) | 50×600 | RampPlatform_10deg | walkin | none | A | One-cell strip at the platform's east ledge (X 1976, 1 cm from the cell centre): Recast erodes a ledge by ≈ 4 cells, more than the half-cell tolerance covers when the ledge is off the 50 cm lattice. |
| 13 | 2.00 | (1075,-3500,645) | 50×400 | JumpPadC_Ledge | walkin | none | A | Same at the 640 cm ledge's east edge. |
| 17, 18 | 1.00 each | (575,5200,245), (725,5200,325) | 50×200 | Stairs_40cm_05 / _07 | walkin | none | A/F | Single 40 cm treads without nav (80 cm treads, step 45 with 40 cm risers is marginal for Recast). |

**lowclear 13 (all inside=1)**: nav islands inside `RampPlatform_10/20/30/45/50deg`, `JumpPadA_Platform` and
`JumpPadC_Ledge` (no `fill_collision_underneath_for_navmesh` on the movement map's platforms) — **F**, same class and
fix as the combat map.

## Synthetic positive (verification step 3)

Scratch copy of `create_combat_test_map.py` writing a separate map `/Game/Maps/L_NavLintSynth` (not committed, deleted
after; `L_CombatTest` itself was not touched) with a 300 × 300 × 20 cm slab tagged `Layout:synth_slab` at (−800, −300):

| slab | navlint result |
|---|---|
| top Z 170 (underside 150) | no new cluster — Recast builds floor nav under a 150 cm clearance, so the floor is covered (correct) |
| top Z 165 (underside 145) | `cluster i=0 m2=9.00 center=(-800,-300,5) extent=(300,300) actor=Ground layout=- cells=36 kind=walkin over=Synth_Slab over_layout=synth_slab` |
| top Z 165 + a NoCollision marker tagged `NavLintIgnore` over the area | cluster gone, walkable 1609 → 1600 m² (exemption works) |

All three synthetic runs exit 1 without `--report-only` (the unchanged combat findings), so the gate's exit code is
exercised as well.
