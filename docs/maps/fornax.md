# Fornax — Duel foundry (map concept, M7)

**Status:** concept, docs only (theme 5 of `docs/maps/BRIEF.md`). Nothing here is built or measured; every number is derived from the movement and item data named in section 0 and must be re-checked by the blockout task.
**Display name:** Fornax (Latin: furnace). **Slug:** `fornax`. **Working ids:** `MAP_ID = "FORNAX"`, `L_Fornax_Blockout` / `L_Fornax`, layout table `Tools/editor/maps/fornax_layout.py`.

## Pitch (20 seconds)

1. A compact 1v1 tournament map inside a volcanic foundry: three rooms around a furnace hub, joined by two straight lanes and one upper gallery, 48 x 36 m, three height levels.
2. Each room holds exactly one major item and they get more dangerous as they get stronger: Yellow Armor on a safe floor, Red Armor on an island in a molten pool, Mega Health on a crane bridge above two lava trenches.
3. Lava is the map's only hazard and it is a floor, not a pit: it costs 25 health per touch, you can jump out, and knocking the opponent in counts as your frag.
4. Three signature moves: walk off the Charging Deck and land on the Red Armor, the lava slalom along the Slag Run into a ramp jump, and the pad ride over a trench up to the Mega.
5. No powerup, no teleporter, no void. Every strong position has at least three ways in and can be shelled from below, so nobody can sit on an item.

## 0. Numbers this design is built on

| Fact | Value | Source |
|---|---|---|
| Ground speed / jump velocity / gravity | 800 cm/s / 675 cm/s / -2000 cm/s2 | `Source/Velox/Movement/VeloxMovementSettings.h` (same values in `DA_MovementSettings_Default`) |
| Jump apex, air time, flat jump range at ground speed | 114 cm, 0.675 s, 540 cm | derived (v2/2g, 2v/g, speed x air time) |
| Step height, walkable slope | 45 cm, 45.5 deg | same header |
| Strafe-jump speed | peak factor >= 1.4 (>= 1120 cm/s) after four hops in a 5120 cm corridor | `docs/movement-tooling.md` (`StrafeRun`, `analyze_run.py --min-peak-factor 1.4`) |
| Rocket-jump apex above take-off | 713 cm scripted (jump + rocket), 660 cm bots, 880 cm highest knockback apex seen | `docs/tasks/m2-combat/notes/05-report.md`, `m4-bots/notes/05-report.md`, `m4-bots/07f` |
| Pads | `AVeloxJumpPad` AimAtTarget, the target is the apex of the capsule centre, launch origin 70 cm above the floor, max launch speed 4000 cm/s | `Tools/editor/veloxmap.py` (`jump_pad`, `PAD_*`), DM1 pads as worked examples |
| Bot gap links | `JUMP` up to 500 cm, `JUMP_FAST` 700 cm, none beyond 900 cm | `Tools/editor/README.md` |
| Respawn | health (incl. Mega) 35 s, armor 25 s, ammo 40 s, weapons 5 s (game mode) | `Tools/editor/create_item_content.py` |
| Budgets | <= 600 draw calls, <= 300 k tris in view, <= 48 MB lightmaps, <= 25 kit meshes x <= 8 materials | ADR 0006 section 8 |
| Skins | Red, Blue, Green, Yellow, Magenta, Cyan; environment V <= 0.50 and S <= 0.25 | `create_cosmetics_content.py`, `docs/art/style-guide.md` |

Conventions are DM1's (ADR 0006 section 2): centimetres, X east, Y north, 100 cm grid, 400 cm kit module, level step 320 cm, lowest walkable floor top at Z = 0, clip-first collision, one layout table for blockout and art.

## 1. Theme, mood and why it fits Velox

A working foundry cut into a volcano: dark basalt walls, soot-black steel, a furnace body in the middle of the map with a glowing mouth, molten metal running in open channels across the floors, an overhead crane on rails with a ladle hanging from its hook. The mood is hot below the knee and cold above it: the lava only lights the floor band, while the walls and ceilings sit in cool, neutral light from roof slits. It fits Velox because the theme is the gameplay. The channels are the hop lines, the crane bridge is the Mega platform, the furnace is the sight blocker of the hub, and every route is a strafe lane, a hop over something that hurts, a pad or a rocket jump. It stays cheap to render: flat-shaded low-poly kit, one unlit emissive material for the lava, baked lighting, no translucency, no particles.

## 2. Mode, players, size

- **Mode:** Duel (`DA_GameMode_Duel`, 2 active players). Secondary: FFA with 3–4 players, same item set. Not offered for more than 4.
- **Why Duel only:** three majors on 25/35 s timers, 5 spawns and a 48 x 36 m footprint give two players a meeting every few seconds. With 6+ players the lava rooms turn into knockback lotteries and the spawn rules (section 6) cannot be met.
- **Footprint:** X -2400..2400, Y -1800..1800 (48 x 36 m, outer walls 100 cm). DM1 is 70 x 38 m; Fornax has about 60 % of its walkable area (estimate 1,150 m2).
- **Height layers:** L0 = 0 (floors, lava rims), L1 = 320 (Charging Deck, Flue, West Landing, East Gallery), L2 = 640 (Crane Bridge). Lava surface at Z = -40, lava bed (collision) at Z = -60.
- **Ceilings (clear height above the room floor):** Crucible 960, Casting Hall 1240, Tap Room 960, Hearth 640, Flue / Slag Run / gallery tunnel 400.

## 3. Layout

Top-down, north up. One character is 100 cm in X, one row is a 200 cm band in Y labelled with its top edge. The sketch is a reading aid; **the rectangle table below is binding**.

```
  y top   x=-2400   -1400     -400      600       1600    2400
          |         |         |         |         |       |
   1800   ###########################LLLLLLssssssss..GGGG#
   1600   ###########################LLLLLLssssssss..GGGG#
   1400   #FFgFFFFFFFFFFFFFFFFFFFFFFFLLLLLL.~~~~~~...GGGG#
   1200   #FFFFFFFFFFFFFFFFFFFFFFFFFFLLLLLLCCCCCMCCCCGGGG#
   1000   #DDDDDDDDDDDDDD#ccc.......#LLLLLLCCCCCCCCCCGGGG#
    800   #DDDDDDDDDDDDDD#ccc.......d.......~~~~~~...GGGG#
    600   #sss~~~~~~~~...#ccc.......d......l....1....GGGG#
    400   #sss~~~~~~~~...#..........#################TTTT#
    200   #sss~~~RRR~~...d...XXXX...#################TTJT#
      0   #sss~~~==~~~...d...XXXX...#################TTTT#
   -200   #..............#..........#................GGGG#
   -400   #..............#.....r....d............Y...GGGG#
   -600   #..............#..........d................GGGG#
   -800   ##dddd#############dddd####...p.......2....GGGG#
  -1000   #.........................:................GGGG#
  -1200   #......~~~~~~~~~~.........:................GGGG#
  -1400   #.........................:....////////////GGGG#
  -1600   #.........................#....////////////GGGG#
  -1800   ################################################
```

Legend: `#` solid, `.` floor L0, `~` lava, `D` Charging Deck (L1), `F` Flue (L1), `L` West Landing (L1), `G` East Gallery (L1), `T` gallery tunnel (L1), `C` Crane Bridge (L2, floor L0 underneath), `s` stairs, `/` 15 deg ramp, `c` 30 deg chute, `X` furnace body, `RRR` Red Armor island, `==` slag bridge, `d` door, `:` open end of the Slag Run. Items: `M` Mega, `Y` Yellow Armor, `r` RL, `l` LG, `J` RG, `g` SG, `p` PG, `1`/`2` pads.

### Binding rectangles (cm; z = floor top)

| Id | Area | X | Y | z | Notes |
|---|---|---|---|---|---|
| A | **Crucible** (west atrium) | -2300..-900 | -900..900 | 0 | 14 x 18 m, ceiling 960 |
| A.pool | Molten pool | -2000..-1200 | -300..500 | lava | 8 x 8 m; north edge is the deck face, west edge the stair flank |
| A.isle | RA island | -1750..-1450 | -50..250 | 0 | 300 x 300, centre (-1600, 100); 250 cm of lava on the W, N and E sides |
| A.bridge | Slag bridge | -1700..-1500 | -300..-50 | 0 | 200 wide walkway from the south floor to the island |
| A.stairs | Deck stairs | -2300..-2000 | -300..500 | 0 -> 320 | 16 risers of 20 cm, treads 50, rise north; east flank open to the pool |
| A.deck | Charging Deck | -2300..-900 | 500..900 | 320 | solid platform, 4 m deep; free south edge above the pool |
| F | **Flue** (north lane) | -2300..300 | 1000..1400 | 320 | 26 x 4 m, straight, no step; openings to the deck at X -2200..-1800 and -1400..-1000; open east end onto the landing |
| H | **Hearth** (hub) | -800..200 | -900..900 | 0 | 10 x 18 m, ceiling 640 |
| H.furnace | Furnace body | -500..-100 | -200..200 | solid | full height; glowing mouth on the south face |
| H.chute | Chute | -800..-500 | 446..1000 | 0 -> 320 | 30 deg ramp, rises north into the Flue through the Hearth north wall |
| S | **Slag Run** (south lane) | -2300..300 | -1700..-1000 | 0 | 26 x 7 m; banks 250 wide (Y -1700..-1450 and -1250..-1000) |
| S.gutter | Gutter | -1700..-700 | -1450..-1250 | lava | 10 m long, 200 wide |
| B | **Casting Hall** (north-east) | 300..2300 | 300..1700 | 0 | 20 x 14 m, ceiling 1240 |
| B.landing | West Landing | 300..900 | 900..1700 | 320 | solid platform 6 x 8 m |
| B.stairs | North stairs | 900..1700 | 1400..1700 | 320 -> 0 | 16 risers, descend east from the landing |
| B.t1 / B.t2 | Casting trenches | 1000..1600 | 700..900 / 1200..1400 | lava | 6 m long, 200 wide each |
| B.mid | Mid strip | 900..1600 | 900..1200 | 0 | 300 wide floor between the trenches, under the bridge |
| B.col | East column | 1600..1900 | 300..1700 | 0 | joins south strip, mid strip and stair foot without a hop |
| B.crane | Crane Bridge | 900..1900 | 850..1250 | 640 | 10 x 4 m deck, underside 580; ends flush above the landing edge and the gallery edge |
| G | **East Gallery** | 1900..2300 | -1700..1700 | 320 | solid, 34 m long, 4 m deep; open to B and C, tunnel (ceiling 400) through the mass at Y -300..300 |
| C | **Tap Room** (south-east) | 300..2300 | -1700..-300 | 0 | 20 x 14 m, ceiling 960, no lava |
| C.ramp | Tap ramp | 706..1900 | -1700..-1400 | 0 -> 320 | 15 deg, rises east to the gallery; in line with the Slag Run's south bank |

**Doors (all 300 high, 400 wide unless noted):** A–Hearth at X -900..-800, Y -200..200; A–Slag Run at X -2200..-1800 through the wall Y -1000..-900; Hearth–Slag Run at X -500..-100; Hearth–B at X 200..300, Y 400..800; Hearth–C at X 200..300, Y -800..-400; Slag Run–C open end at X 300, Y -1600..-1200.

## 4. Areas

**A — Crucible (Red Armor).** A tall room whose middle is an 8 x 8 m pool of molten metal. The RA sits on a 3 x 3 m island. Four ways onto it, none dominant: walk the 2 m slag bridge from the south, hop 250 cm from the east floor, hop 250 cm downhill from the open flank of the stairs, or walk off the Charging Deck and land on it (section 7). There is no weapon and no spawn in this room, on purpose: you come for the armor and leave. Cover: none at L0; the deck gives 4 m of depth to step back from splash. Sightlines: the whole room from the deck and from both doors; nothing from the Flue (the solid deck blocks the downward line).

**Charging Deck (L1, part of A).** A 14 x 4 m platform along the north wall, 50 Health at its west end. Three exits: stairs down, two openings north into the Flue; plus a clean drop onto the east floor strip. It overlooks the RA, so it is the map's most tempting perch, and it is exposed to rockets from the whole Crucible floor and to anyone arriving through the Flue behind it.

**F — Flue (L1 lane).** 26 m straight strafe lane at L1 with the Shotgun at its west end. Continues over the West Landing, so the clear line from its west wall to the landing edge is 32 m. Rail lane risk is discussed in section 9.

**H — Hearth (hub).** The furnace body stands in the middle and blocks every straight line through the hub; players circle it in 3 m aisles. The Rocket Launcher lies in front of the furnace mouth. Four doors plus the Chute up to the Flue. Ceiling 640, so no rocket-jump tricks here; it is a room you pass through with a rocket in the pipe.

**S — Slag Run (L0 lane).** 26 m lane with a 10 m lava gutter down the middle. Run either 2.5 m bank, or zigzag over the gutter (200 cm, any jump clears it). The south bank lines up with the Tap ramp. Rockets ammo and three 5 Health on the north bank, 25 Health at the east end.

**B — Casting Hall (Mega).** The showpiece. Two parallel trenches cut the floor into a south strip (LG, Pad 1), a 3 m mid strip (25 Health) and the north stairs. The Crane Bridge spans the room 640 cm up between the West Landing and the East Gallery, right above the mid strip; its long edges hang over the trenches, so stepping off slowly (below 190 cm/s) drops you into lava and a pushed player lands in it. The Mega sits at the bridge centre. Up by Pad 1, by rocket jump from either L1 platform (320 cm) or from the floor (640 cm, expert). Down by dropping off either end. Nobody can hold the bridge: it is visible from every point in B, from the landing and from the gallery.

**G — East Gallery (L1).** A 34 m balcony along the east wall that tunnels through the mass between B and C. The Railgun lies in the middle of the 6 m tunnel, which is a tube with two open ends: a good place to collect, a bad place to stay.

**C — Tap Room (Yellow Armor).** The safe room and the trailing player's base: YA on open floor, Plasma Gun, 25 Health, cells, no lava. Pad 2 and the 15 deg ramp lead up to the gallery and from there to the RG and the Mega fight. Four ways in (Slag Run, Hearth door, gallery drop, ramp).

## 5. Items

All definitions are `/Game/Items/DA_Item_<Type>_<Name>`. No powerup in any mode (so no `DisabledInModes` use). Machinegun is the spawn weapon; no MG pickup, no bullets ammo (as on DM1).

| Id | Item | X, Y, Z | Respawn |
|---|---|---|---|
| RA | Armor.Red (100) | -1600, 100, 0 | 25 s |
| Mega | Health.Mega (100) | 1400, 1050, 640 | 35 s |
| YA | Armor.Yellow (50) | 1500, -500, 0 | 25 s |
| RL | Weapon.RocketLauncher | -300, -550, 0 | 5 s |
| LG | Weapon.LightningGun | 900, 500, 0 | 5 s |
| RG | Weapon.Railgun | 2100, 0, 320 | 5 s |
| PG | Weapon.PlasmaGun | 600, -1000, 0 | 5 s |
| SG | Weapon.Shotgun | -2100, 1200, 320 | 5 s |
| Rockets | Ammo.Rockets | -900, -1125, 0 | 40 s |
| Lightning | Ammo.Lightning | 1750, 1000, 0 | 40 s |
| Slugs | Ammo.Slugs | 2100, 900, 320 | 40 s |
| Cells | Ammo.Cells | 1700, -1200, 0 | 40 s |
| Shells | Ammo.Shells | -1100, 700, 320 | 40 s |
| H50 | Health.Large | -2150, 700, 320 | 35 s |
| H25 x 4 | Health.Medium | (-1600, -700, 0), (1300, 1050, 0), (500, -450, 0), (0, -1575, 0) | 35 s |
| H5 x 6 | Health.Small | Slag north bank (-1500 / -1300 / -1100, -1125, 0); Tap ramp (1000 / 1300 / 1600, -1550, z 79 / 159 / 240) | 35 s |
| Shard x 6 | Armor.Shard | Deck stairs (-2150, -150 / 100 / 350, z 80 / 180 / 280); North stairs (1500 / 1300 / 1100, 1550, z 100 / 180 / 260) | 25 s |

30 pickups. Each ammo lies 8–11.2 m from its weapon (DM1 rule 6–12 m).

**Timing and control logic.**

- The controlling player cycles RA and Mega. RA to Mega is about 35 m plus the pad flight (island, east hop, Hearth north aisle, B door, Pad 1): about 5.5 s running, about 4 s strafing. The return over the top (bridge west end, landing, Flue, deck, walk-off onto the RA) is about 44 m and the fastest line on the map for a strafe-jumper.
- 25 s and 35 s timers drift against each other and coincide every 175 s, so the controller must regularly choose one, and each choice sends them through a lava room.
- The trailing player's cycle is YA (25 s) plus the Tap Room's 25 Health, PG and the gallery RG. From the YA it is 4 s to the gallery mouth overlooking the Mega pad arc, which is where comebacks start: a rail or a rocket at the right moment puts the Mega taker into trench T1.
- YA to RA is about 28 m (3.5 s), so a controller who also wants the YA gives up the Mega window. All three majors cannot be held by one player.
- Stack per 25 s if one player took everything: 150 armor + 30 from shards; that needs both stairs and both ends of the map.

## 6. Spawns

Five spawns. Rules as on DM1: none in line of sight of RA or Mega, at least 10 m apart, a weapon within 8 m (horizontal), facing into the room. `VeloxSpawn::Select` (farthest half plus avoid radius) is unchanged.

| Id | X, Y, Z | Yaw | Weapon within 8 m | Why it does not see a major |
|---|---|---|---|---|
| S1 | -1700, 1200, 320 | 180 | SG (4 m) | Flue; the wall between the two deck openings and the solid deck block the line to the island |
| S2 | -650, -700, 0 | 0 | RL (3.8 m) | Hearth; walls |
| S3 | 500, 500, 0 | 0 | LG (4 m) | under the bridge line: the deck (z 580..640) hides the Mega |
| S4 | 450, -1350, 0 | 0 | PG (3.8 m) | Tap Room; walls |
| S5 | 2100, -450, 320 | 90 | RG (4.5 m) | gallery, C side; tunnel walls |

No spawn in the Crucible (every point sees the RA). A sixth spawn has no legal place yet (section 15, question 5).

## 7. Movement routes

Lava hop arithmetic uses flat jump range 540 cm at 800 cm/s; a drop of 320 cm takes 0.566 s, a drop of 640 cm takes 0.80 s.

**Strafe lanes and hop lines**

| Route | Geometry | What it gives |
|---|---|---|
| Flue | 26 m x 4 m at L1, plus 6 m of landing (32 m clear) | the fast line between the RA side and the Mega side; expect about 1.25–1.3 x ground speed at the east end (to be measured; `StrafeRun` reaches 1.4 x in 51 m) |
| Flue launch | jump off the landing's east edge (X 900) between Y 900 and 1200 at 1000 cm/s | flies about 10 m under the bridge along the 3 m mid strip and lands on the east column; left or right of the line is a trench |
| Slag Run, south bank | 26 m x 2.5 m, then 4 m of Tap Room floor, then the 12 m ramp | lane speed carried into a ramp jump at the gallery edge (X 1900), landing toward the RG |
| Lava slalom | zigzag over the gutter (200 cm) between the two banks | the bunny-hop line of the map; a missed landing costs 25 health |
| Double trench hop | from the B south strip, take off between Y 560 and 650, land on the mid strip (Y 900..1200), hop again over T2 onto the north stairs east of X 1450 (steps <= 100 cm high there) | crosses B south to north in two hops without the east column |
| RA island hops | 250 cm gaps from the east floor and from the stair flank | any direction, any speed above a walk with a jump |

**Drops with a speed window**

| Route | From -> to | Window |
|---|---|---|
| Deck walk-off onto the RA | deck edge Y 500, Z 320 -> island Y -50..250 | lands on the island for 442..972 cm/s without jumping (800 cm/s lands 47 cm south of the centre). Faster than 972 overshoots: lined up with the slag bridge (X -1700..-1500) you land on it, otherwise in lava until 1413 cm/s clears the pool. A jump from the edge at >= 810 cm/s clears the whole pool onto the south floor |
| Bridge step-off | bridge edge Y 850 or 1250, Z 640 | below 190 cm/s you land in the trench underneath; 190..690 cm/s lands on the south strip (south side) |

**Pads** (capsule-centre apex as the target, as DM1)

| Pad | Pad X, Y | Target (apex) | Landing | Launch speed, flight | Notes |
|---|---|---|---|---|---|
| Pad 1 (Bellows) | 1400, 450, 0 | 1400, 900, 760 | 1400, 1021, 640 (bridge centre is Y 1050; zone 400 x 400) | 1747 cm/s (vz 1661, vy 542), 1.05 s | crosses trench T1; feet pass the bridge edge 25–40 cm above the deck. Getting hit on the way up means lava |
| Pad 2 (Tap) | 1400, -1000, 0 | 1900, -1000, 420 | 2046, -1000, 320 (gallery) | 1454 cm/s (vz 1183, vx 845), 0.76 s | same shape as DM1 PadA |

**Rocket and plasma jumps** (capability: 713 cm scripted, 660 cm bots)

| Jump | From -> to | Height needed | Verdict |
|---|---|---|---|
| RJ-West | West Landing (320) -> Crane Bridge (640) | 320 | easy; bot `ROCKET_JUMP` link (run-up 550 cm available) |
| RJ-East | East Gallery (320) -> Crane Bridge (640) | 320 | easy for humans; bot link only if a 350 cm run-up proves enough (convention is 450) |
| RJ-Floor | B mid or south strip (0) -> Crane Bridge (640) | 640 | expert; 73 cm of margin on the scripted apex, 20 cm for bots, so no bot link |
| RJ-Deck | Crucible floor or RA island (0) -> Charging Deck (320) | 320 | easy; from the island it is the "grab and rocket out" exit (250 cm horizontal) |
| RJ-Tap | Tap Room floor (0) -> gallery (320) | 320 | easy; alternative to Pad 2 without the pad sound |
| Plasma climb | Crucible east wall -> deck (320) | 320 | optional and slow; climb height is not measured yet (M2 checklist item 14), so nothing depends on it |

Clearance above rocket-jump take-offs follows DM1's 900 cm rule where the jump is intended: B (1240 over L0, 920 over L1), A and C (960 over L0). From the deck and in the Hearth, Flue, Slag Run and tunnel the ceiling blocks rocket jumps by design.

**Ramps.** Tap ramp: 15 deg, 1194 cm run, 320 rise, existing `Ramp_15` piece; ramp jump at the top. Chute: 30 deg, 554 cm run, 320 rise, walkable both ways (limit 45.5 deg), a fast descent from the Flue into the Hearth beside the RL. How much speed the movement component's ramp handling (`RampSlideMinNormalSpeed`, `RampExitMinAngle`) keeps on entry and exit of both ramps is to be recorded with `velox.move.Record` in the blockout task; the layout does not depend on a particular value.

## 8. Loops and flow

1. **West loop (RA):** Crucible floor -> stairs -> Charging Deck -> Flue -> Chute -> Hearth -> west door -> Crucible. About 75 m; shortcut by the deck walk-off.
2. **North-east loop (Mega):** Hearth -> B south strip -> Pad 1 -> Crane Bridge -> west end drop -> West Landing -> Flue -> Chute -> Hearth. About 70 m.
3. **South-east loop (YA / RG):** Hearth -> Tap Room -> Pad 2 or ramp -> gallery -> tunnel (RG) -> B side of the gallery -> drop to the east column -> B door -> Hearth. About 80 m.
4. **Outer ring:** Crucible -> Slag Run -> Tap ramp -> gallery -> B -> north stairs -> West Landing -> Flue -> deck -> stairs -> Crucible. About 130 m, touches no hub door.
5. **Top line:** RA -> RJ-Deck -> Flue -> landing -> RJ-West -> Mega -> east drop -> gallery -> RG -> YA. The full-control run; costs two rockets and about 90 health in self-damage (44 per jump in the M2 measurement), which is exactly what the Mega gives back.

Control flows around the furnace. The hub connects everything at L0 but shows nothing, so the leader has to commit to a door, and the Flue and gallery let the trailing player get above them. Loops 1 and 2 share the Flue, loops 2 and 3 share the gallery, loops 1 and 3 share only the hub: there is always a way to refuse the fight and rotate.

## 9. Sightlines, chokes, camping

| Spot | Risk | Why it does not dominate |
|---|---|---|
| Charging Deck over the RA | strongest perch; sees the island and both Crucible doors | three approaches (stairs, two Flue openings), one behind the camper; no weapon or armor up there, only 50 Health; splash from the whole floor |
| Crane Bridge | holds the Mega | visible from every point of B, the landing and the gallery; 4 m wide over lava; nothing to hide behind |
| Gallery tunnel (RG) | tube with a 17 m rail view each way | two open ends 6 m apart; rockets from either end; no health |
| Hearth | hub with the RL | four doors and the Chute; the furnace prevents covering more than two of them |
| Tap Room YA | open floor | four entrances; deliberately the easy major |

**Rail lanes.** Gallery 34 m (Y -1700..1700, broken into two 17 m halves by where the RG lies), Slag Run 26 m, Flue 26 m continuing through B at L1 eye height to the east wall: **46 m, the longest line on the map**. DM1's lanes are 68 m, so this is inside precedent, but in Duel it deserves a look in the blockout: if it decides games, a 200 x 200 cm stack pillar on the landing at (500..700, 1100..1300) cuts it to 26 + 14 m at the cost of a dogleg in the Flue launch. Crucible diagonal 23 m, Casting Hall diagonal 24 m.

**Chokes.** The Hearth doors (400 wide) are the only L0 links between the three rooms apart from the Slag Run; the Flue and the gallery bypass them at L1. The slag bridge is 200 wide and is the only walking route to the RA; that is intended.

**Sound.** With the existing attenuation set (ADR 0006 section 9): Pad 1 is audible within 30 m, which covers B, the Hearth, the gallery and the Flue's east half, so a pad ride to the Mega is always announced; rocket jumps are audible map-wide; pickups within 25 m. Proposed additions for this map (new assets, section 12): a lava contact sizzle on the `SA_Player` curve, a second footstep surface (steel grating) for the deck, landing, gallery and bridge so that L1/L2 movement can be told from L0, and a quiet lava ambience (below 400 Hz, at least 18 dB under footsteps) so it never masks them.

## 10. Readability, palette, lighting

**The problem this theme has:** lava is orange, and two of the six skins are Red and Yellow. The rule "the player is the most saturated thing in any frame" needs an explicit exception and limits.

- **Surfaces:** basalt walls (H 220, S 0.12, V 0.22), darker ceilings (V 0.15), slag floor (H 30, S 0.08, V 0.30), soot steel (H 210, S 0.10, V 0.30), grating decks (V 0.38) and pale hazard kerbs along every lava rim (H 45, S 0.25, V 0.50). All inside the style guide's environment limits; walls and ceilings are cool and neutral so every skin, including Red and Yellow, stands out against them at eye level.
- **Lava:** one unlit emissive material, hue fixed at 20–25 deg (18+ deg from the Red skin, 35 deg from Yellow), drawn as dark crust plates (about 60 % of the area, V 0.12) with glowing cracks, so its average brightness stays below the characters' emissive floor. It exists only as a floor plane 40 cm below floor level: about 94 m2 in total (pool 50, gutter 20, trenches 24), 8 % of the walkable area. Seen level, a standing player is never in front of lava; seen from above (deck, bridge) they can be, which is the case the acceptance check in section 14 measures.
- **Style-guide change needed:** a "hazard emissive" class beside the light fixture, with these limits, and `kit.py` / `create_kit_materials.py` must accept `MI_Kit_Lava` in their palette assertion.
- **Lighting (all static, CPU Lightmass, path B bake on the Mac as for DM1):** about 28 cool-white overhead lights (roof slits and fixtures) carry the rooms; about 22 small warm lights sit just above the channels and in the furnace mouth with radius <= 600 cm so the orange stays below about 200 cm; 6 neutral accents on majors and pads. Roughly **56 static lights**, zero runtime cost. The lava material does not contribute to the bake (no emissive static lighting); the warm lights fake its glow, which keeps the bake time and the look controllable. 6 reflection captures. No dynamic lights beyond the existing muzzle and rocket lights.
- **Skybox:** the map is enclosed. Roof slits in the Casting Hall and two wall slits in the Crucible show `TC_Sky_Fornax`, a 128 px cubemap (slate ash sky, dull ember horizon). No volumetrics, no fog cards, no particles.

## 11. Kit and budgets

**Reused from DM1 (all 18 pieces):** `Floor_400`, `Ceiling_400`, `Wall_400`, `Wall_Trim`, `Wall_Half`, `Wall_Door300`, `Wall_Door400`, `Pillar`, `Ramp_15`, `Stairs_5x20`, `Ledge_Edge`, `Railing_Stub`, `Bridge_400`, `Pad_Ring`, `Pedestal`, `Light_Fixture`, `Skirting_400`, `Ceiling_Beam`. Geometry unchanged; Fornax uses its own tints.

**New pieces (7, which takes the kit to the cap of 25):**

| Piece | Size | Use |
|---|---|---|
| `SM_Kit_Lava_400` | 400 x 400 flat, 2 tris | lava surface, `MI_Kit_Lava` |
| `SM_Kit_Channel_Rim_400` | 400 x 20 x 60 | channel wall and hazard kerb |
| `SM_Kit_Ramp_30` | 400 wide, 277 run, 160 rise (two per chute) | the Chute |
| `SM_Kit_Grate_400` | 400 x 400 x 20 | deck, landing, gallery and bridge floors |
| `SM_Kit_Crane_Girder_400` | 400 x 40 x 80 | crane rails under the ceilings and the bridge underside (above 250 cm) |
| `SM_Kit_Furnace_Mouth` | 400 x 40 x 400 wall piece | furnace south face with an emissive inset |
| `SM_Kit_Crucible` | about 200 x 200 x 250, <= 300 tris | ladle on the crane hook above the Mega, hung above 250 cm; the map's landmark |

**Materials:** a Fornax set of 8 instances of `M_Kit_Master` (Basalt, BasaltDark, SlagFloor, Steel, SteelDark with the trim folded in, Grate, Light, Lava). The budget of 8 is per map, so the DM1 set is untouched. `M_Kit_Master` needs an unlit, UV-panning switch for Lava (one static switch; no new master).

**Estimates against ADR 0006 section 8** (to be replaced by `audit_map.py` figures):

| Budget | Limit | Estimate |
|---|---|---|
| Draw calls from any position | 600 | about 300–350: <= 25 meshes x <= 8 materials instanced, 30 pickups, 2 characters, weapons, HUD. The map is smaller and more enclosed than DM1 |
| Triangles in view | 300 k | under 60 k (about 1,100 kit instances at 30 tris average, plus characters) |
| Lightmaps | 48 MB | about 24 MB (about 95 floor tiles at 128, about 350 wall pieces at 64, small pieces at 32) |
| Kit meshes / materials | 25 / 8 | 25 / 8 (no headroom; a further piece means retiring one) |
| Translucent surfaces | none except LG beam and pickup glow | none added |

## 12. Hazard rules (lava)

Fornax is the first Velox map with a hazard. ADR 0006 section 3 ruled lava out for DM1 only; no code for it exists (`FellOutOfWorld` is the only world damage, and it counts as a suicide). The rules below are the proposal; they need a short ADR before any implementation task.

1. **Lava is a floor.** The bed is ordinary static collision 60 cm below the rim. There is no swimming and no change to `UVeloxMovementComponent`, prediction or replication. The rim is higher than the 45 cm step height and lower than the 114 cm jump apex: you cannot walk out, you can always jump out.
2. **Damage:** 25 on contact and 25 every 0.25 s while the feet are below the lava surface, server-side, ignoring armor, no knockback, tag `Damage.Environment.Lava`. With jump held, a landing in lava is one tick: 25 health for a touch, 100 health gone in 0.75 s for someone who stays. All values in `UVeloxCombatSettings`.
3. **Credit:** a lava death within 5 s of the last damage or knockback from an opponent is that opponent's frag (kill feed shows lava); otherwise it is a suicide with the usual -1. This "last attacker" record does not exist yet and is the same feature the space map's void rule needs.
4. **Everything else passes through:** projectiles hit the bed and explode normally (a rocket jump out of the lava works), pickups never lie in lava, corpses are removed by their timer.
5. **Feedback:** sizzle sound and the existing pain sounds; the damage direction indicator points down. No screen effect that hides the opponent.
6. **Implementation shape:** `AVeloxHazardVolume` (box volume, server tick over overlapping pawns, applies the existing damage spec path) in `Source/Velox/Combat/`, placed by a new `hazard` record and `veloxmap.hazard()` helper; `KillZ -1000` stays as the safety net and `VELOX_ARENA_FLOOR_Z` stays 0 (the bed at -60 is far above the left-arena threshold).

## 13. Bots and nav

- **No nav in lava.** Lava beds are a new box kind `hazard` that produces no nav mesh (nav modifier with the null area over each channel). The nav lint needs a matching rule: a `hazard` kind is exempt by kind, not by `NavLintIgnore` (which stays forbidden).
- **Links:** `JUMP_PAD` x 2; `ROCKET_JUMP` x 1–2 (RJ-West; RJ-East if the run-up works); `JUMP` both ways over lava x 7 (island east, island from the stair flank, gutter x 3, trench T1 x 2), all 200–250 cm, far inside the 500 cm `JUMP` limit; `DROP` candidates x 8 (deck east end, landing south edge, gallery into B and into C x 2, bridge ends x 2, landing east corner), each kept only if it meets the E3 (d) rule (clear 300 x 300 landing, 500 cm from a blank wall, not a pad). No `DROP` from the deck onto the island and none from the bridge's long edges: those are human routes.
- **The RA must be reachable on foot:** the slag bridge gives a pure walking path, so Easy bots (no hops) can take the RA and `FtFornaxReachability` can use the `Walk` filter for every pickup except the Mega (pad).
- **New bot behaviour needed (small):** (a) hazard escape: a bot whose feet are in a hazard volume faces the nearest nav point and holds forward plus jump, ahead of the existing off-mesh recovery; (b) the hop guard (D18) must also refuse a bunny hop whose landing point is off the nav mesh, otherwise Hard bots strafe-jumping the 2.5 m Slag banks will land in the gutter; (c) item value is unchanged, but the RA island and the Mega bridge should not be camp spots for the Duel camp rule (camp position on the nearest rim).
- **Soak invariants:** the map-independent set of ADR 0006 section 7 applies unchanged (`left_arena 0`, `world_kill 0`, link success >= 90 % per type, item coverage). Lava adds two report-only metrics, `hazard_touch` and `hazard_death` split into credited and self-inflicted; an architect ruling turns self-inflicted bot deaths into a band after three report-only sets, as E3 did for DM1.

## 14. Acceptance ideas for the blockout

1. Generation asserts (script): rectangle table as above, doors 240–400, ledges >= 120, pad zones >= 300 x 300, RJ clearances, spawn rules, ammo distances, island gaps 250, rim height 60.
2. `navlint.sh` clusters = 0 with the hazard exemption; nav build <= 2 s; `DumpPadLandings` within 60 cm of both link ends.
3. `FtFornaxReachability`: every spawn to every pickup (Full filter), every pickup except the Mega with the Walk filter, all major pairs.
4. Hazard FT: a pawn placed in lava loses 25 health per 0.25 s and dies at the computed time; a pawn knocked in by a second pawn credits the kill; a lone pawn does not; armor untouched.
5. `Content/Data/MoveScripts/FornaxRun.json` in the net soak (100 ms, 2 % loss): deck walk-off lands on the island at 800 cm/s, both pads reach their markers, gutter zigzag and double trench hop land on floor, Tap ramp jump lands on the gallery; checks green, stray corrections within the `PadRun` rule.
6. Report-only soaks: BotDuel x 3, BotRun x 3, FFA x 10 with 3 bots; all invariants green, RA and Mega taken every round, each `JUMP` link type >= 90 % success, `hazard_death` self-inflicted reported.
7. `audit_map.py`: budgets of section 11, protrusion <= 5 cm, collision diff empty between blockout and art map.
8. Mac playtest: the 46 m lane verdict, the deck perch verdict, and the contrast shots (each of the six skins on the RA island seen from the deck and on the mid strip seen from the bridge); Red and Yellow must remain readable at a glance.

## 15. Risks and open questions

**Risks**

1. **Bots and lava (biggest).** The bots have never seen a hazard; DM1 was explicitly built without one. Three small behaviours and a lint rule are needed (section 13), and the self-inflicted death rate is unknown until the first soak. Fallback if it does not converge: widen the Slag banks to 300 cm and add a walk bridge over each trench.
2. **Orange glow against Red and Yellow skins.** Mitigated by the crust pattern, the fixed hue, the floor-only placement and cool walls, but only a GPU playtest can confirm it.
3. **The Charging Deck may still be too strong** in Duel despite three approaches. Levers: move the 50 Health off it, or shorten it to the western 8 m.
4. **The 46 m Flue line** (section 9); pillar fix ready.
5. **Kit at the cap** (25 / 8): any additional piece for this or another M7 map forces a per-map kit split.
6. **Speed windows are computed, not flown.** The walk-off, the Flue launch and the double trench hop are tuned to 800–1000 cm/s on paper; the blockout must record them.

**Questions for the human**

1. Lava damage rule: 25 per touch and 100 per second as proposed, or Quake's harsher feel (about 30 per tick, effectively death in the pool)? This decides how much the RA island punishes.
2. Should a lava kill credit the last attacker (proposed, 5 s window)? The same rule would serve the space map's void.
3. One Yellow Armor (proposed) or a second one on the Charging Deck to give the west side an armor for the trailing player?
4. Is a per-map default skin pair for Duel acceptable (for example Cyan versus Magenta on Fornax) if the contrast shots show Red or Yellow too close to the lava?
5. Five spawns or six? A sixth needs either a relaxed "weapon within 8 m" rule or a second Shotgun on the Slag Run.
6. May Fornax add a second footstep surface and a lava ambience, or should M7 stay inside the existing sound set?
