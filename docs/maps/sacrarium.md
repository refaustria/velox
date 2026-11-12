# Sacrarium — map concept (theme 1 "Nostalgic")

**Pitch (20 seconds).**
1. **Sacrarium** ("the shrine"; slug `sacrarium`) is a square stone temple: one 16 m tall central atrium with the **Quad** on a dais, wrapped by two concentric rings — a ground-level cloister of four aisles and four corner chapels, and a gallery ring 3.2 m up inside the atrium.
2. A 24 m **rail loft** hangs 6.4 m up over the south side of the atrium; you reach it by flying over the Quad on the **Quad Flight** pad, by the shrine pad hidden behind the south wall, or by rocket-jumping from the galleries.
3. **Red Armor** and **Mega** sit on ledges in diagonally opposite chapels (NW / SE), 65–70 m apart on the upper route; neither is visible from the loft, so holding the Railgun and holding the armour are two different jobs.
4. Movement: three 54 m strafe lanes, a 12 m ramp that slides you straight onto the Mega pad, four jump-pad shrines, two rocket jumps of proven height and one expert one.
5. FFA 4–8, 56 × 56 m, three levels, no void, no lava, no teleporter: everything the current code, bots and kit pipeline already support, in a layout that shares nothing with DM1 Praesidium except the rules.

Status: **concept** (docs only, 2026-11-12). Nothing here is built or tested. Coordinates are a first layout proposal on the 100 cm grid; as in ADR 0006 §3, the graph and the rules are binding and exact coordinates are the blockout's job. The pad arcs were computed with the `AVeloxJumpPad` AimAtTarget arithmetic (launch origin 70 cm above the floor, gravity 2000 cm/s², landing when the capsule centre is 70 cm above the landing floor); the same arithmetic reproduces DM1's authored landings for PadA and PadD.

---

## 1. Name, theme, mood, fit

- **Display name:** Sacrarium. **Slug:** `sacrarium`. Proposed assets: `Tools/editor/maps/sacrarium_layout.py`, `/Game/Maps/Sacrarium/L_Sacrarium_Blockout`, `L_Sacrarium`. The `DMn` number is the lead's call (four other concepts are written in parallel).
- **Theme and mood.** A sunken temple of dark basalt and pale sandstone, lit by iron braziers and by a cold shaft of dusk light that falls through the atrium's oculus onto the Quad dais. Pointed arches, an arcade of octagonal columns under the galleries, lancet windows high in the atrium walls, jump pads set into rune-ringed shrine niches, muted banners that colour-code the four quarters. It is the stone hall where a late-90s arena player first learned to time a Quad. It is an homage to the *genre* of gothic arenas, not to any one map: the plan (a square double ring around one atrium with a loft), the names and the look are original.
- **Why it fits Velox.** (a) Movement first: the plan is long straight lanes joined by square rooms, i.e. strafe-jump straights and wide turns, plus a ramp line and pad arcs that reward knowing them. (b) Readability: grey-brown stone at V ≤ 0.5 / S ≤ 0.25 is the easiest backdrop for the bright skin set, and baked torchlight is what static Lightmass does well. (c) Cost: boxes on the 400 cm module, 12 of DM1's 18 kit meshes reused, no new gameplay code (no void, lava, teleporter, mover, water). (d) It complements DM1: Praesidium is two atriums and a tower on a 70 × 38 m rectangle, built for Duel first; Sacrarium is one atrium and two rings on a square, built for FFA first.

## 2. Mode and player count

- **FFA, 4–8 players** (best at 5–7). TDM-capable by size once team modes exist; not a CTF map.
- **Duel:** valid but not the target. The Quad is disabled in Duel through `DisabledInModes` as on DM1; with two players the outer ring is too long for them to meet (compare the BotDuel reasoning in ADR 0006 E3 (b)). Not recommended for the Duel rotation.
- Powerups: **Quad only** (respawn 120 s, first appearance 30–60 s after level start per `create_item_content.py`). No Haste/Regen/Battle Suit: one powerup, one place, one clock.

## 3. Size and height layers

- **Footprint** 56 × 56 m including outer walls (X, Y −2800..2800; X east, Y north; 100 cm grid, 400 cm module, 100 cm walls, as `dm1_layout.py`). The outline is a square with recessed mid-sides; the four corner blocks are the chapels. Walkable area ≈ 1 880 m² at level 0 plus ≈ 600 m² of decks (DM1: 70 × 38 m footprint).
- **Levels** (`LEVEL_STEP 320`, `FLOOR_Z 0` = lowest walkable floor, `KILL_Z −1000` as a safety net only):

| Level | Z | What is there |
|---|---|---|
| 0 | 0 | atrium floor and arcade, four aisles, four chapels (the cloister ring); Quad dais top at +40 |
| 1 | 320 | gallery ring inside the atrium, three corner landings over aisle ends, RA ledge, Mega ledge, top of the east ramp |
| 2 | 640 | rail loft (above the south gallery) |

- **Clear heights:** atrium 1600 (four wall tiers; 1280 above the galleries, the rocket-jump rule asks ≥ 900), south aisle 1200 (the shrine pad flies through it), the other aisles and three chapels 800, crypt chapel 480, under every deck 300 (deck 300..320, as DM1's bridges). Global clip roof at 1800 (`CLIP_H`).

## 4. Layout

One character = 100 × 100 cm, so the plans look stretched north–south by about 2 : 1. `#` solid, `.` floor, blank = the atrium's open well (floor at z 0, nothing above it up to the ceiling), `o` column or pillar (100 × 100), `/` east ramp, `=` stairs. Where a glyph is ambiguous the tables in §5–§8 are authoritative.

**Level 0.** `Q` Quad dais, `^` / `>` jump pad (launch direction), `R` Rocket Launcher, `S` Shotgun, `L` Lightning Gun, `P` Plasma Gun, `Y` Yellow Armor, `1`–`9` spawns:

```
       x -2800     -1800     -800      200       1200      2200      
         |         |         |         |         |         |     
  2800   ########################################################
         #.========....############################.............#
         #.========....############################.............#
         #.========....############################.............#
  2400   #.========....############################...........1.#
         #.............############################.............#
         #.............############################.............#
         #.............############################......R......#
  2000   #.............############################.............#
         #.............#..........................#.............#
         #......................................................#
         #...................3......S.......2...................#
  1600   #......................................................#
         #......................................................#
         ##########....#..........................#....##########
         #########......###########....###########......#########
  1200   #########......#........................#......#########
         #########..4...#........................#......#########
         #########......#........................#......#########
         #########......#........................#...///#########
   800   #########......#....o   o  o   o   o....#...///#########
         #########......#....                ....#...///#########
         #########......#....                ....#...///#########
         #########..L...#....       ^        ....#...///#########
   400   #########......#....o              o....#...///#########
         #########......#....                ....#...///#########
         #######.............                ........///#########
         #######>............o      Q       o........///#########
     0   #######.............                ......P.///#########
         #######.............                ........///#########
         #########......#....                ....#...///#########
         #########..5...#....o              o....#......#########
  -400   #########......#....                ....#......#########
         #########......#....                ....#......#########
         #########......#....                ....#.8....#########
         #########......#....o   o  o   o   o....#......#########
  -800   #########......#........................#......#########
         #########......#...........9............#......#########
         #########......#........................#......#########
         #########......#........................#......#########
 -1200   #########......###########....###########......#########
         ##########....#..........................#....##########
         #......................................................#
         #...................6......S.......7...................#
 -1600   #......................................................#
         #...o.....o............................................#
         #.............#.........^................#.............#
         #.............############################.............#
 -2000   #......Y......############################.............#
         #.............############################.............#
         #.............############################..^..........#
         #...o.....o...############################.............#
 -2400   #.............############################.............#
         #.............############################.............#
         #.............############################.............#
         ########################################################
 -2800
```

**Upper levels.** `g` gallery ring (z 320), `L` rail loft (z 640, the south gallery lies beneath it), `n` corner landing (z 320), `r` RA ledge, `m` Mega ledge (z 320), `+` opening in a wall at that level, `/` ramp, `=` stairs; `G` Railgun, `A` Red Armor, `M` Mega, `Y` Yellow Armor, `H` 50 Health; pad landings `q` Quad Flight, `s` South Shrine, `w` West Shrine, `x` Mega pad:

```
       x -2800     -1800     -800      200       1200      2200      
         |         |         |         |         |         |     
  2800   ########################################################
         ##========rrrr##########################################
         ##========rrrr##########################################
         ##========rrrr##########################################
  2400   ##========rrrr##########################################
         ##########rrrr##########################################
         ##########rArr##########################################
         ##########rrrr##########################################
  2000   ##########rrrr##########################################
         ##########rrrr#nnnn#####################################
         ##########rrrr+nnnn#####################################
         ##########rrrr+nnnn#####################################
  1600   ##########rrrr+nnnn#####################################
         ##########rrrr#nnnn#####################################
         ###############nnnn#######################++++##########
         ################+++######################nnnnnn#########
  1200   ################gggggggggggggggggggggggg+nnnnnn#########
         ################gggggggggggHgggggggggggg+nnnnnn#########
         ################gggggggggggggggggggggggg+nnnnnn#########
         ################gggggggggggggggggggggggg####///#########
   800   ################gggg                gggg####///#########
         ################gggg                gggg####///#########
         ################gggg                gggg####///#########
         ################gggg                gggg####///#########
   400   ################gggg                gggg####///#########
         ################gggg                gggg####///#########
         ###############+gggg                gggg####///#########
         ###############+ggwg                gYgg####///#########
     0   ###############+gggg                gggg####///#########
         ###############+gggg                gggg####///#########
         ################gggg                gggg####///#########
         ################gggg                gggg################
  -400   ################gggg                gggg################
         ################gggg                gggg################
         ################gggg                gggg################
         ################gggg                gggg################
  -800   ################LLLLLLLLLLLLLLLLLLLLLLLL################
         ################LLGLLLLLsLLqLLLLLLLLLLLL+nnnnnn#########
         ################LLLLLLLLLLLLLLLLLLLLLLLL+nnnnnn#########
         ################LLLLLLLLLLLLLLLLLLLLLLLL+nnnnnn#########
 -1200   #######################++++##############nnnnnn#########
         ###########################################+++##########
         ##########################################mmmmmmmmmmmmm#
         ##########################################mmmmmmmmmmmMm#
 -1600   ##########################################mmmxmmmmmmmmm#
         ##########################################mmmmmmmmmmmmm#
         ########################################################
         (south of y -1800 there is nothing above level 0)
```

**Section south–north through x = 0** (not to scale):

```
z 1600  ------------------------------------------------------------  atrium ceiling (oculus over the dais)
z 1200  --------------+
        South aisle   | wall, shrine window z 640..1000 at x -500..-100
z  800                |                                               +--------------  aisles N/W/E 800
z  640                |[== rail loft ==]            Quad Flight arc
z  320                |[= S gallery ==]     (open well)      [= N gallery =]|  wall
z    0  __pad^________|__arcade__________Q dais____pad^________arcade______|__North aisle____
     y -1900      -1300 -1200      -800      0      400      800     1200 1300        1900
```

**Graph (binding).** Cloister ring at level 0: Red Chapel — North aisle — Armoury — East aisle — Mega Chapel — South aisle — Crypt — West aisle — Red Chapel; every aisle also opens into the atrium through one arch at its middle. Gallery ring at level 1 inside the atrium, joined to the outside by three corner landings (NW → RA ledge, NE → top of the east ramp and the Armoury drop window, SE → Mega ledge). Loft at level 2 above the south gallery.

## 5. Areas

All walls 100 cm. Ground doors and arches are 400 wide and 300 high; upper openings are 300–400 wide and 300 high.

| Id | Area | Bounds (cm) | Heights | Cover and sightlines |
|---|---|---|---|---|
| A | **Atrium** | x ±1200, y ±1200; open well x ±800, y ±800 | floor 0, ceiling 1600 | The well is open ground: only the Quad dais (400 × 400, top +40, one 20 cm step ring 500 × 500) and 16 columns on the well's edge. The 400 cm deep **arcade** under the galleries (300 clear) is the covered way round the well and hides you from the loft on the south side. Four arches at the mid-sides (x or y −200..200) give two ground axes that cross at the dais. |
| T | **Galleries** (the inner ring) | ring between ±800 and ±1200, z 320; decks 300..320 | 1280 clear (300 under the loft) | 4 m wide, no railing in the clip (visual balustrade stubs only where no drop link starts). Sees the whole well and the opposite galleries (16–24 m); the north, west and east galleries are in view of the loft. Items: 50 HP north, YA east, shards west, slugs south (under the loft). |
| L | **Rail loft** | x ±1200, y −1200..−800, z 640; deck 620..640 | 960 clear | 24 × 4 m balcony, open to the north. Sees the well, the three open galleries and the dais; sees no aisle or chapel floor through the arches (the gallery decks cut those lines, §10). No health, no armour, no ammo up here. Back wall holds the shrine window (x −500..−100, z 640..1000). |
| N | **North aisle "Via Longa"** | x ±1300, y 1300..1900 | 0, ceiling 800 | Flat, empty, 26 m; with the two chapels it is a 54 m straight. West end is roofed by the NW landing (300 clear). SG and a row of 5 HP. |
| W | **West aisle "Shrine Walk"** | x −1900..−1300, y ±1300; niche x −2100..−1900, y ±200 | 0, ceiling 800 | Flat 26 m; the **West Shrine** pad sits in the niche opposite the west arch and throws you through the window above the arch (z 320..700) onto the west gallery. LG. |
| S | **South aisle "Chancel"** | x ±1300, y −1900..−1300 | 0, ceiling 1200 | Tall hall behind the loft. The **South Shrine** pad stands against the south wall at (−300, −1850) and throws you through the shrine window onto the loft. SG. |
| E | **East aisle "Ascent"** | x 1300..1900, y ±1300 | 0; ramp x 1600..1900 from (y −294, z 0) up to (y 900, z 320), 15°, sealed underneath; ceiling 800 | The only walk-up to the galleries besides the RA stairs. Beside the ramp the ground lane is 300 wide; landings roof both ends. PG, cells, 25 HP, 5 HP trail on the ramp. |
| C1 | **Red Chapel** (NW) | x −2700..−1400, y 1400..2700 | 0; **RA ledge** x −1800..−1400, y 1400..2700 at 320 (deck, 300 clear below); stairs x −2600..−1800, y 2300..2700 (16 × 20 cm risers, 50 cm treads, rising east); ceiling 800 | Both ground doors (to N and to W) are under the ledge. RA at the ledge's middle between its two entrances (stairs top north, landing door south). |
| C2 | **Armoury** (NE) | x 1400..2700, y 1400..2700 | 0, ceiling 800 | Open square room, RL in the middle; the NE landing's window (z 320..620) looks and drops into it. |
| C3 | **Mega Chapel** (SE) | x 1400..2700, y −2700..−1400 | 0; **Mega ledge** x 1400..2700, y −1800..−1400 at 320 (deck); ceiling 800 | **Mega pad** on the floor, Mega at the ledge's east end, 9 m from the ledge's only door. Both ground doors are under the ledge. |
| C4 | **Crypt** (SW) | x −2700..−1400, y −2700..−1400 | 0, ceiling 480 | Low, four pillars at (−2350 / −1750, −2350 / −1750) break every line across the room. YA in the centre. The only close-quarters room. |
| X1–X3 | **Corner landings** | NW: x −1300..−900, y 1300..1900 · NE: x 1300..1900, y 900..1300 · SE: x 1300..1900, y −1300..−900; decks at 320 | 480 clear | 4 × 6 m decks over an aisle end; each joins a gallery corner to an outer place and has one free edge that drops into its aisle. |

Openings at level 1: NW landing ↔ north gallery (x −1200..−900 in the atrium's north wall) and ↔ RA ledge (y 1500..1800 in the chapel's east wall); NE landing ↔ east gallery (y 900..1200) and → Armoury window (x 1400..1800, drop only); SE landing ↔ east gallery (y −1200..−900, under the loft) and ↔ Mega ledge (x 1500..1800); west window (y −200..200, z 320..700).

Flow rules kept from ADR 0006 §3: every room ≥ 2 exits; the three major-item places have ≥ 3 ways in or out counting drops (RA ledge: stairs, landing door, drop; Mega ledge: pad, landing door, drop; loft: two pads and two rocket jumps in, two gallery drops and the free drop into the well out); no dead ends; doors 400; walkable widths ≥ 300; pad landing zones ≥ 300 × 300; rocket-jump take-offs with ≥ 900 clearance.

## 6. Items

Definitions are `/Game/Items/DA_Item_<Type>_<Name>`. Respawn times from `create_item_content.py`: health 35 s, armor 25 s, ammo 40 s, powerups 120 s, weapons 5 s (FFA game mode).

| Id | Item | x, y, z | Where |
|---|---|---|---|
| Quad | `Powerup.Quad` (disabled in `Velox.Mode.Duel`) | 0, 0, 40 | dais, atrium centre |
| RA | `Armor.Red` | −1600, 2100, 320 | RA ledge, Red Chapel |
| Mega | `Health.Mega` | 2550, −1600, 320 | Mega ledge, east end |
| YA_E | `Armor.Yellow` | 1000, 0, 320 | east gallery, mid |
| YA_SW | `Armor.Yellow` | −2050, −2050, 0 | Crypt centre |
| RG | `Weapon.Railgun` | −900, −1000, 640 | loft, west part |
| RL | `Weapon.RocketLauncher` | 2050, 2050, 0 | Armoury centre |
| LG | `Weapon.LightningGun` | −1600, 400, 0 | West aisle |
| PG | `Weapon.PlasmaGun` | 1450, −100, 0 | East aisle, beside the ramp foot |
| SG_N / SG_S | `Weapon.Shotgun` × 2 | 0, 1600, 0 / 0, −1600, 0 | North and South aisle centres |
| Slugs | `Ammo.Slugs` | 0, −1000, 320 | south gallery, under the loft (9 m from RG, one level down) |
| Rockets | `Ammo.Rockets` | 1100, 1600, 0 | North aisle, east end (10.5 m from RL) |
| Lightning | `Ammo.Lightning` | −1600, −700, 0 | West aisle, south (11 m) |
| Cells | `Ammo.Cells` | 1450, −1000, 0 | East aisle, under the SE landing (9 m) |
| Shells | `Ammo.Shells` | −1100, 1600, 0 | North aisle, under the NW landing (11 m from SG_N) |
| Bullets | `Ammo.Bullets` | −2500, −1700, 0 | Crypt |
| H50 | `Health.Large` | 0, 1000, 320 | north gallery, mid |
| H25 × 6 | `Health.Medium` | (−2400, 1700, 0) · (2500, 2500, 0) · (1450, 500, 0) · (1000, −1600, 0) · (−1000, −600, 0) · (−2500, −2500, 0) | Red Chapel floor · Armoury · East aisle lane · South aisle · west arcade · Crypt |
| H5 × 8 | `Health.Small` | ramp: (1750, 0 / 250 / 500 / 750, on the ramp surface) · North aisle: (300, 1450) (500, 1750) (700, 1450) (900, 1750) | trail up the east ramp · slalom in the North aisle |
| Shard × 6 | `Armor.Shard` | west gallery (−1000, 400 / 600 / 800, 320) · RA stairs (−2500 / −2250 / −2000, 2500, on the steps) | from the West Shrine landing toward RA · up the stairs toward RA |

38 pickups (37 in Duel): 1 Mega, 1 RA, 2 YA, 6 shards, 1 × 50, 6 × 25, 8 × 5, 6 weapons, 6 ammo, Quad. Each ammo is 6–12 m (horizontal) from its weapon as the DM1 validator demands; the second Shotgun has no ammo box of its own. There is no Machine Gun pickup (spawn weapon); one Bullets box exists because eight players burn MG ammo.

**Timing rhythm and control logic.**

- **Three clocks, three corners of a triangle.** RA (25 s, NW), Mega (35 s, SE), Quad (120 s, centre). RA → Quad ≈ 28 m (3.5 s at ground speed: landing, north gallery, drop). Mega → Quad ≈ 33 m (4 s). RA ↔ Mega ≈ 65–70 m on the upper route (ledge, landing, two gallery sides, landing, ledge): ≈ 8.5 s running, ≈ 6 s strafe-jumping. A player cannot hold both armours without crossing the atrium in the open every cycle.
- **The loft is a position, not a stack.** RG is on the loft; its ammo, every health and every armour are below it. A loft camper is at 125 spawn health and falling, seen from three galleries, and reachable by four routes (§8).
- **Quad is a public event.** The dais is on both ground axes, under all four galleries and in front of the loft. Taking it is easy; leaving with it is the fight. The fastest exit is the Quad Flight pad 4 m north of the dais, which flies the carrier over the well onto the loft in 1.35 s, in everyone's view.
- **Weapon logic.** RL is in the far corner from the RG (NE vs. south): a rocket jump to the loft costs a trip. LG on the long west straight, PG where the ramp fight happens, Shotguns on the two aisles that face the atrium's main axis. Rockets ammo sits on the North aisle straight, so an RL player is pulled onto the cloister ring.
- **Yellow armours split the classes of player.** YA_E is on the exposed east gallery (fast, risky, on the Mega route); YA_SW is in the Crypt (safe, slow, off the upper ring).

## 7. Spawns

Nine `PlayerStart`s, DM1 rules: ≥ 10 m apart, a weapon ≤ 8 m away (horizontal), none with line of sight to Mega or RA, facing into the room. `VeloxSpawn::Select` (farthest half plus avoid radius) is unchanged.

| Id | x, y (z 0) | Yaw | Nearest weapon | Notes |
|---|---|---|---|---|
| S1 | 2500, 2300 | 225 | RL 5.2 m | Armoury corner |
| S2 | 780, 1600 | 180 | SG_N 7.8 m | North aisle east |
| S3 | −780, 1600 | 0 | SG_N 7.8 m | North aisle west |
| S4 | −1600, 1000 | 270 | LG 6.0 m | West aisle north; RA is above the chapel deck, the deck cuts the line (validator must confirm) |
| S5 | −1600, −350 | 270 | LG 7.5 m | West aisle south |
| S6 | −780, −1600 | 0 | SG_S 7.8 m | South aisle west |
| S7 | 780, −1600 | 180 | SG_S 7.8 m | South aisle east |
| S8 | 1450, −700 | 90 | PG 6.0 m | East aisle; the Mega is behind the chapel's north wall |
| S9 | 0, −950 | 90 | SG_S 6.5 m | south arcade, under the gallery and the loft: faces the dais, invisible from the loft |

No spawn in the open well, on a gallery, in the Crypt (no weapon within 8 m) or in the Mega and Red chapels. Nine spawns for eight players is the minimum I am comfortable with; a tenth needs either a weapon rule waiver or a seventh weapon pickup (open question 4).

## 8. Movement routes

Velox numbers used (from `UVeloxMovementSettings` defaults and the combat settings): ground speed 800 cm/s, jump velocity 675 cm/s, gravity 2000 cm/s² → jump apex 114 cm, air time 0.675 s, one hop covers 540 cm at 800 cm/s and 810 cm at 1200 cm/s; step height 45 cm. `StrafeRun` reaches ≥ 1.4 × ground speed within four hops in a 51 m corridor. Rocket splash 100 → self-knockback up to 1250 cm/s on top of the jump; the project's measured ceiling for a rocket-jump apex is ≈ 880 cm (task 07f, ADR 0006 §3). Bots already perform rocket-jump links of 320 cm (DM1) and 350 cm (`L_ItemTest`) rise.

**Strafe lanes** (flat, no step, ≥ 400 wide, ≥ 300 clear):

| Lane | Line | Length | Notes |
|---|---|---|---|
| North | y 1400..1800, x −2700..2700 | 54 m | two 400 cm doors, NW landing overhead for 4 m; SG_N on the centre line |
| South | y −1800..−1400, x −2700..2700 | 54 m | runs under the Mega ledge at the east end; shrine pad is outside the lane (y −1850) |
| West | x −1800..−1400, y −2700..2700 | 54 m | passes the niche and the west arch; ends in the Crypt (pillars: you must leave the centre line) |
| Atrium axes | x or y −200..200 through two opposite arches | 38–40 m | across the well past the dais (the dais step is 20 cm, inside step height) |

**Bunny-hop lines.**

| Line | Path | Length | Why it is a line |
|---|---|---|---|
| Cloister lap | the four aisles through the inner corners of the four chapels | ≈ 132 m (≈ 16.5 s at ground speed) | each chapel is a 13 m square room: a 90° turn with a radius of several metres, no door on the turn's inside |
| **Ramp line** | NE landing (1750, 1100, 320) → down the east ramp (11.9 m run, 15°) → under the SE landing → Mega Chapel door (x 1400..1800) → Mega pad (1700, −2300) | 34 m, straight on x ≈ 1700 | downhill hops on a descending slope land lower than they started, so each hop is longer; the line ends on the pad and the pad puts you in front of the ledge door. Expected to keep or gain speed; the blockout's move script measures it |
| Gallery lap | the four galleries | ≈ 80 m | 4 m wide, three corners in the open, one under the loft |
| Arcade weave | under the galleries around the well | ≈ 80 m | 300 clear: a hop (apex 114 + capsule 140) fits with 46 cm to spare |

**Jump pads** (all AimAtTarget; target = apex; launch speed cap is 4000 cm/s):

| Pad | Pad x, y, z | Target (apex of the capsule centre) | Landing | Apex of the feet | Launch speed, flight | Clearance checks |
|---|---|---|---|---|---|---|
| **Quad Flight** | 0, 400, 0 | 0, −520, 900 | 0, −960, 640 (loft) | 830 | 2083 cm/s, 1.35 s, 13.6 m | passes the dais at feet z 565; crosses the loft edge (y −800) at feet z 753 (113 over the deck); head at most 970 under a 1600 ceiling |
| **South Shrine** | −300, −1850, 0 | −300, −1250, 850 | −300, −996, 640 (loft) | 780 | 1892 cm/s, 1.26 s | through the shrine window (z 640..1000): feet 775 over the 640 sill, head 915 under the 1000 lintel |
| **West Shrine** | −2000, 0, 0 (in the niche) | −1250, 0, 440 | −974, 0, 320 (west gallery) | 370 | 1732 cm/s, 0.83 s | through the west window (z 320..700): feet 368 over the 320 sill |
| **Mega pad** | 1700, −2300, 0 | 1700, −1780, 420 | 1700, −1628, 320 (Mega ledge) | 350 | ≈ 1470 cm/s, 0.76 s | feet ≈ 350 at the ledge edge (y −1800), the DM1 PadA margin |

The four landing zones are ≥ 300 × 300 clear deck. The two loft landings are 300 apart (x 0 and x −300) and 6–9 m east of the Railgun.

**Rocket jumps** (plasma: see the note below the table):

| Jump | From → to | Rise needed | Versus Velox | Bot link |
|---|---|---|---|---|
| **RJ West / RJ East** | west / east gallery (∓1000, −350, 320), run south 450 cm → loft (∓1000, −1000, 640) | 320 | proven class (DM1's two links are 320); clearance above take-off 1280 ≥ 900 | `ROCKET_JUMP` × 2 |
| Floor → gallery | anywhere on the well's edge, 0 → 320 | 320 | same class; skips stairs, ramp and pads | none (the pads and landings are cheaper for bots) |
| North aisle → NW landing; East aisle → SE landing | 0 → 320 onto a landing's free edge | 320 | same class; aisle ceiling 800 gives 800 over the floor, below the 900 rule, so **no link** — a human trick only | none |
| **Expert: floor → loft** | atrium floor near (0, −600, 0) → loft edge, 0 → 640 | 640 | inside the ≈ 880 cm ceiling, but needs most of the full splash (by the damage arithmetic about 75 % or more); **unverified** — test in the blockout with a `RocketJumpRun` variant before advertising it | none |
| Expert: South aisle → shrine window | 0 → 640 through a 400 × 360 window | 640 | as above plus aim; probably a show-off jump only | none |

Self-damage for a full-splash rocket jump is 50 (half of 100) before armour, so two loft jumps from spawn health are not free. **Plasma:** PG splash is 15 damage at a 50 cm radius, i.e. ≈ 190 cm/s per cell; one cell under a jump raises the apex from 114 to about 185 cm. No route needs it and no item is placed for it; plasma-climbing the 320 cm gallery face is a possible discovery for players, not a designed route.

**Ramp slide.** The east ramp is the map's one slope (15°, walkable). Downhill it is the Ramp line above; uphill it is a plain walk-up (Velox sets the jump velocity, so a ramp jump gives no extra height — if `bRampExitCatchAir` makes the top of the ramp launch a fast runner onto the landing, that is a bonus to observe, not a requirement).

**Drops.** Gallery → well (320) from every inner edge; landing → aisle (320); ledge → chapel floor (320); NE landing → Armoury through the window (320); loft → gallery at x ±1000 (320); loft → well (640, humans; no link; whether a 640 cm fall costs health is open question 3).

## 9. Loops and control

1. **Cloister** (level 0, ≈ 132 m): N aisle (SG, rockets) → Armoury (RL) → E aisle (PG, cells) → Mega Chapel (pad) → S aisle (SG, shrine pad) → Crypt (YA) → W aisle (LG, shrine pad) → Red Chapel (stairs). The weapon loop; never in view of the loft.
2. **Gallery ring** (level 1, ≈ 80 m): 50 HP → YA_E → slugs → shards. The loop of whoever wants to deny the loft and watch the Quad.
3. **East vertical loop** (≈ 75 m): NE landing → ramp line → Mega pad → Mega → SE landing → east gallery (YA_E) → NE landing. Health, armour and PG in one loop, at the price of the most exposed gallery.
4. **Rail loop** (≈ 95 m): South aisle → South Shrine → loft (RG) → drop to the west gallery → shards → NW landing → RA → stairs or drop → West aisle (LG) → Crypt or South aisle. The stack loop; it touches the loft for seconds only.
5. **Quad dive**: any gallery → drop → dais → Quad Flight → loft → drop either side. Ten seconds, all of it in public.

**How control flows.** The rings cross only in the atrium, so every loop change is visible there. A leader's natural cycle is RA → north gallery (50 HP) → Quad check → east gallery (YA) → Mega, i.e. the gallery ring plus two landings; the counter is the loft (rail across the well onto exactly those galleries) and the arcade (rockets up through the well's edge). The loft's counter is the galleries themselves (three sides at 16–24 m, LG and rockets reach) and the two rocket jumps that land at the loft's ends. Nobody holds the loft and an armour at once. With 6–8 players the cloister carries the newly spawned (weapons, small health, long straights), the galleries carry the stacked, and the dais brings both together every two minutes.

## 10. Sightlines and chokes

| Line | Length | Risk | Why it is acceptable / mitigation |
|---|---|---|---|
| Loft → well, north gallery, west and east galleries | 16–28 m | the designed rail position | no health/armour/ammo on the loft; four ways up; arcade and south gallery are blind spots directly below; S9 spawns in that blind spot |
| Loft → aisles or chapels | slivers only | low | through the ground arches: none — a line from the loft (eye ≈ z 765) that would pass under an arch's 300 cm lintel has to drop below z 320 while it is still over the gallery deck in front of that arch, and the deck stops it (north: at y ≈ 1110 of 800..1200). Through level-1 openings: the loft sees a strip of the NW and NE landings, and through the west window the far side of the West aisle around the shrine niche (down to about capsule height). These are known, small, and listed so a blockout can measure them |
| Atrium axes (arch → dais → arch, continuing across both aisles) | 38–40 m, 4 m wide | rail lane across the Quad at ground level | intended: it is the Quad's price. The dais gives no cover; the columns (every 400 cm) cover everything off the axis. If playtests say too strong: offset the north and south arches by 400 cm (x −600..−200 / 200..600) so no line crosses the whole building |
| North / South / West lanes | 54 m, 4 m wide at the doors | longest lines on the map; rail down a strafe lane | the RG is two levels and ≥ 25 m away from any lane, slugs are on the south gallery only; a lane is a line for one door width, and both ends are 13 m rooms to turn into. Fallback: shift one chapel door per lane by 200 cm |
| Galleries lengthwise | 24 m | LG / rail along a 4 m deck | intended gallery duel; dropping into the arcade breaks it at any point |
| West window ↔ east gallery | 24 m | a shot from the West Shrine flight | harmless, flight is 0.8 s |

**Chokes and camping.**

- *Mega ledge:* one door plus the pad. A camper hears the pad (pad sound is audible through the chapel) and faces a door 9 m away; but rockets from the chapel floor reach the whole ledge edge and the drop makes a third way. Watch item in soaks: Mega camp episodes vs. DM1.
- *RA ledge:* two entrances 6 m either side of the RA and a 13 m open edge to the chapel floor; the least campable major.
- *Loft:* covered above.
- *Crypt:* two doors, low ceiling, pillars — the natural shotgun ambush. It holds only a YA and no route passes *through* it except the cloister lap's corner; acceptable and intended as the one tight room.
- *East aisle beside the ramp:* 300 cm lane, 12 m long, splash-prone. It is short, has the ramp itself as the alternative, and the PG sits at its mouth.
- *Corner landings:* 4 × 6 m junction decks with two openings each; rockets from the aisle below hit the deck's underside, not the player. Could become grenade-less safe spots; they hold no items on purpose.

## 11. Readability, palette, lighting

**Palette** (style guide: environment V ≤ 0.50, S ≤ 0.25; players V ≥ 0.60, S ≥ 0.70):

| Surface | Colour (HSV) | Use |
|---|---|---|
| Basalt | 220°, 0.10, 0.22 | walls, columns, ceilings |
| Sandstone | 35°, 0.22, 0.46 | floors, stairs, ramp, dais (floors are the lightest surface so feet and shadows read) |
| Worn limestone trim | 40°, 0.12, 0.50 | arch edges, skirting, ledge edges, balustrades (outlines every opening and drop) |
| Bronze | 160°, 0.25, 0.32 | pad shrines, pedestals, brazier bowls |
| Banner cloth | red 5° / blue 215° / ochre 45° / green 140°, all 0.25, 0.40 | quarter coding: NW red (RA), SE blue (Mega), NE ochre (RL), SW green (Crypt); hung above 250 cm |

Nothing in the environment exceeds S 0.25, so every skin of the bright set is the most saturated thing in frame, as the style guide demands. The one colour risk is *light*, not paint: orange torchlight lowers the contrast of the orange, yellow and red skins. Rule for this map: brazier **flames** are saturated emissive geometry (small), brazier **lights** are only mildly warm (≈ 1.0, 0.86, 0.68, the DM1 `WARM`), and the gallery and loft levels get neutral fill so the rail fight is never read against orange. Characters keep their emissive floor (0.35) from the character master material.

**Landmarks** (orientation without a HUD): the oculus light shaft on the dais; the loft's row of three lancet windows; banner colour per quarter; the ramp (only slope); the Crypt's low ceiling; the two shrine niches (bronze, rune ring, the only glowing floor elements besides pickups).

**Lighting plan** (ADR 0006 §5: static only, CPU Lightmass, bake on the Mac path B until the Mac Studio is the build host):

- ≈ 46 static point lights, one per brazier or hanging lamp: atrium 12 (four corners at gallery height, four on the well's columns, four under the loft and north gallery), aisles 20 (five each), chapels 12, loft 2. Warm in the cloister, neutral on galleries and loft, one cold (0.85, 0.92, 1.0) static spot down the oculus onto the dais.
- Static `SkyLight` with a new 128 px cubemap `TC_Sky_Sacrarium` (dusk gradient: deep blue zenith, pale amber horizon); visible only through the oculus and the lancet windows, which are emissive panes — there is no outdoor geometry.
- 9 sphere reflection captures at 128 px (atrium floor, atrium upper, loft, four aisles, two for the chapels' ledges).
- Lightmap resolutions as DM1 (floor 128, walls 64, trims/columns 32); volumetric lightmap cell 200 cm; 3 bounces. Bake estimate on 16 vCPUs: Preview 15–50 min, Production 1.5–4 h (about 1.4 × DM1's surface).
- No dynamic or stationary light; flames do not flicker in the lighting. A flame may wobble through world-position offset in an opaque emissive material (no translucency, no particles) — open question 6.

## 12. Kit

Collision stays clip-first (ADR 0006 §2): the layout's boxes are the collision, kit meshes are `NoCollision`, nothing protrudes more than 5 cm into walkable space below 250 cm.

**Reused from the DM1 kit (12 of 18 meshes, re-skinned by material instance):** `Floor_400`, `Ceiling_400`, `Wall_400`, `Wall_Trim`, `Wall_Half`, `Ramp_15`, `Stairs_5x20`, `Ledge_Edge`, `Bridge_400` (decks), `Pedestal`, `Skirting_400`, `Ceiling_Beam`. Not used: `Wall_Door300/400`, `Pillar`, `Railing_Stub`, `Pad_Ring`, `Light_Fixture` (replaced by the gothic pieces below).

**New pieces (9), each ≤ 600 triangles, one material slot, hard edges, same pivot conventions:**

| Mesh | Size (cm) | Replaces / purpose |
|---|---|---|
| `SM_Kit_Arch_400` | 400 × 30 × 400, pointed opening 300 high | door and arch pieces (the clip opening stays a 400 × 300 rectangle; the arch's shoulders are above 250 cm) |
| `SM_Kit_Column_300` | 100 × 100 × 300, octagonal with capital | arcade columns and Crypt pillars (clip is the 100 × 100 box) |
| `SM_Kit_Balustrade_400` | 400 × 20 × 100 | visual rail on deck edges that carry no drop link |
| `SM_Kit_Wall_Lancet` | 400 × 25 × 400, emissive pane | upper atrium tiers, loft back wall, the two flight windows' frames |
| `SM_Kit_Vault_400` | 400 × 400 × 60, four-facet vault | ceilings of atrium and chapels, entirely above the clip ceiling |
| `SM_Kit_Brazier` | 80 × 80 × 120 bowl on a stem + opaque emissive flame | standing light fixture (floor-standing only against walls and columns, inside the clip of its column or wall niche) |
| `SM_Kit_Lamp_Hanging` | 60 × 60 × 80, hangs from the ceiling | light fixture over decks and lanes (above 250 cm) |
| `SM_Kit_Pad_Shrine` | 300 × 300 × 5 rune ring + 400 × 30 × 400 niche frame variant | jump-pad base and the two shrine niches |
| `SM_Kit_Banner` | 100 × 4 × 300, opaque | quarter colour coding, above 250 cm |

The Quad dais is two scaled `Floor_400` boxes (no new mesh). Per map: 12 + 9 = **21 distinct meshes ≤ 25**. Materials: a second set of ≤ 8 instances of `M_Kit_Master` (`MI_KitG_Basalt`, `_Sandstone`, `_Limestone`, `_Bronze`, `_Cloth` (tinted per banner through a per-instance parameter or four instances — then drop one), `_Pane` (emissive), `_Flame` (emissive)); 4 new 512 px textures.

**Tooling consequences (for the task files, not decided here):** `kit.py` asserts ≤ 25 pieces and ≤ 8 materials globally; with a second theme that becomes a per-theme assertion (`kit.py --theme gothic`) and the map audit keeps the per-map cap. `create_dm1_map.py` holds the layout validator and the art-mode mapping for DM1 only; a second map wants them moved to a map-independent `create_map.py --layout maps/<slug>_layout.py`. The `Ramp` record rises along +X only; the east ramp rises along +Y, so the record gains a direction. None of this is new engine or gameplay code.

## 13. Budget estimate

The audit counts **components** as draw calls (DM1: 518 kit instances + pickups + pad discs = 568 / 600, with walls scaled up to 1000 cm, floors on an 800 cm grid, ceilings on 1200 cm). Same scaling assumed here.

| Group | Estimate |
|---|---|
| Floors (level 0 on the 800 grid, partials at doors and arcade) | 55 |
| Decks (galleries, loft, landings, ledges) | 26 |
| Walls and wall tiers (atrium 4 tiers, S aisle 3, rest 2), arches, lancets | 185 |
| Ceilings / vaults (1200 grid) | 32 |
| Columns and pillars | 20 |
| Braziers and lamps | 46 |
| Stairs, ramp, dais, pedestals, pad shrines, banners, balustrades, skirting | 75 |
| **Kit instances** | **≈ 440** |
| Pickups 38, pad discs 4 | 42 |
| **Draw-call estimate** | **≈ 480 / 600** (± 60) |

Triangles: ≈ 440 instances at an average well under 150 → **< 60 k / 300 k** in the whole map. Lightmaps: DM1 is 12.35 MB; with ≈ 1.4 × the lit surface and the same resolutions **≈ 17–20 MB / 48 MB**; components above lightmap res 64 ≈ 80 / 120 (floors and decks). Textures < 3 MB / 256 MB. Static lights ≈ 47, captures 9. The worst view is from the loft across the atrium: all four 16 m walls, three galleries and the well — it is also the view with the fewest distinct meshes. The margin against the 600 cap is about 120 components; if the art pass wants more wall tiers or trims, the atrium's upper two tiers can be 1200 cm pieces.

## 14. Bots and navigation

Nothing here needs new bot code; the map uses the DM1 link vocabulary.

- **Nav links (22):** `JUMP_PAD` × 4 (link end = the computed landing, within 60 cm as `FT_*_Reachability` checks); `ROCKET_JUMP` × 2 (RJ West / East, start 450 cm before the loft edge, end 200 cm onto the loft); `DROP` × 16: gallery → well × 8 (two per side: W/E galleries at y ±500, N/S galleries at x ±500; ends 350 cm out, all ≥ 400 cm from the Quad Flight pad and ≥ 200 cm from the dais), loft → gallery × 2 (x ±1000), RA ledge → chapel floor, Mega ledge → chapel floor (at x 2500, 800 cm from the Mega pad), NW / NE / SE landing → aisle × 3, NE landing → Armoury window × 1.
- **The E3 (d) drop rule is applied from the start:** every drop link lands on ≥ 300 × 300 of clear floor, ≥ 500 cm from the wall ahead, never on a pad trigger. For that reason there is **no** drop link out of the west window (it lands 2–3 m from the West Shrine pad: the drop → pad → up loop DM1 had with pad D) and none from the loft into the well beside the Quad Flight pad. Humans can do both.
- **Stacked nav:** the south side has three layers (arcade 0, south gallery 320, loft 640) and every deck has walkable nav under it at 300 clear — the DM1 bridge case (D41). Decks are `bridge` kind (not filled); walls, columns, stairs and the ramp's seal are filled kinds. The ramp is sealed underneath with stepped fill boxes like `A_Ramp_Fill_*`.
- **Hop headroom:** under decks the clearance is 300; the D18 guard sweeps the capsule up by the 114 cm apex, which fits (254 < 300), so bots may bunny-hop the arcade and the lane sections under landings.
- **Column erosion:** arcade columns are 100 × 100 on the well's edge every 400 cm; the walkable arcade behind them stays 300 wide. Risk: nav erosion pinches at columns next to drop-link landings; the gallery drops therefore land at x/y ±450, a full 350 cm inside the well.
- **Rocket-jump eligibility** is the existing rule (launcher owned, rockets ≥ 1, health above the link's minimum). Only Hard/Expert presets take the two links; all presets reach the loft by the two pads.
- **Quad** is destroyed on the server at `BeginPlay` in Duel (existing code); bots never route to it there.
- **Soak expectations:** no band file at first → report-only, with the map-independent invariants hard from the first run (`left_arena 0`, `world_kill 0`, `no_nav_point 0`, `giveup 0`, stuck strikes ≤ 5 over the FFA set, dwell ≤ 5 s, nav build ≤ 2 s, link success ≥ 90 % per type, item coverage 38/38). The standard FFA set runs three bots; on ≈ 2 500 m² that will give a kill rate at or below DM1's 4.65 per run. Recommendation: derive this map's FFA band from a six-bot set (`?Bots=6`), which is also what the map is for.

## 15. Hazards

**None by design.** No void, no lava, no crushers, no water, no teleporters; `KillZ −1000` and the perimeter/roof clips are safety nets that a soak must never trigger (`left_arena 0`, `world_kill 0`). The only ways the map hurts you are self-inflicted splash (rocket jumps: 50 self-damage at full splash) and, if Velox has or later gains fall damage, the 640 cm step off the loft into the well (open question 3). This is deliberate: the other four concepts cover void, lava and foliage; the nostalgic map is the control case that needs no new rules.

## 16. Acceptance ideas for a blockout task

1. `--validate-only` (plain Python on the layout table): DM1's rules parameterised — walkable widths, doors 240–400, ceilings per room class, pad arcs simulated and landing within 60 cm of the authored landing with ≥ 300 × 300 zones, the two flight windows cleared by ≥ 30 cm at sill and lintel, rocket-jump clearance ≥ 900 on both links, 9 spawns ≥ 10 m apart / weapon ≤ 8 m / no line of sight to Mega or RA, ammo 6–12 m, item mix 38 (37 in Duel), every item on a solid top, drop-link landings ≥ 500 cm from the wall ahead and ≥ 300 cm from any pad trigger.
2. `navlint.sh L_Sacrarium_Blockout`: `clusters=0`, `lowclear polys=0`.
3. `FT_Sacrarium_Reachability` in `fts.sh`: `FindPath` (Full filter) from every spawn to every pickup and between every pair of majors; with the Walk filter (no rocket jumps) the loft is still reachable (pads); every `JUMP_PAD` link end within 60 cm of `ComputeLandingPoint`.
4. Move script `SacrariumRun.json` in the net soak (100 ms / 2 % loss): North lane strafe (peak ≥ 1.4 × ground speed, as `StrafeRun`), the four pads to their landing markers (Σ strays ≤ 2.5 cm, as `PadRun`), the Ramp line with the speed at the ramp foot ≥ the speed at the ramp top and a check box on the Mega ledge at the end.
5. A `RocketJumpRun` variant on the map for RJ West (lands in a 300 × 300 box on the loft) and, report-only, the expert floor → loft jump (records the apex; decides whether the route is real).
6. Report-only soak sets on a quiet host under the soak lock: BotRun × 3, FFA × 10 with three bots and FFA × 10 with six; all invariants PASS; per-record link table (`links-by-record.txt`) with no record under 90 %; item coverage 38/38; then an architect ruling writes `infra/ci/bands/L_Sacrarium.sh`.
7. A sightline report from the layout table (plain Python ray casts between sample points at eye height): the longest line into each spawn point, the assertion "no line from the loft passes a ground arch", and a list of what the loft sees through the level-1 openings and the west window (expected: landing strips and the shrine niche only).
8. After the art pass: `audit_map.py --diff-collision` empty, `--protrusion` ≤ 5 cm, draw calls ≤ 600, the confirmation soak under the bands, bake and `Velox.Maps.LightingBuilt`.

## 17. Risks and open questions

**Risks, largest first.**

1. **The loft may dominate FFA.** A 24 m rail balcony over the Quad is the map's identity and its biggest balance risk. Levers, in order: move the RG to the loft's east end (further from RJ West, nearer both pad landings), shorten the loft to x ±800, raise the north gallery's balustrade into real cover (a clip rail 100 cm high on its inner edge), move the slugs off the south gallery. None changes the graph.
2. **Rail lanes on the three 54 m straights and the atrium axes** (§10). The fallback offsets (doors by 200 cm, arches by 400 cm) trade strafe-lane purity for cover; only a human playtest can rule.
3. **Bot link reliability with 22 links** (DM1 has 16 and needed two iteration tasks). The landings' drops and the Armoury window land near door jambs — the DM1 "dock N" erosion-margin class. Expect one layout iteration.
4. **Warm light versus warm skins** (§11). Needs the Mac to judge; the mitigation is in the lighting rules, not the layout.
5. **Size.** 132 m of cloister for four players may feel empty; the map is honest only from five players up. With bots filling, fine; as a four-human map DM1 is better.
6. **Draw calls** depend on the same tile scaling DM1 needed; the gothic pieces add components (arches, lancets, columns, braziers) that a concrete bunker did not need. Estimated margin ≈ 120.
7. **Symmetry and orientation.** The plan is a square with fourfold bones; orientation rests on the loft, the ramp, the Crypt's ceiling and the banner colours. If playtesters get lost, the cheap fix is light colour per quarter.

**Open questions for the human.**

1. Is the Quad Flight pad (a pad 4 m from the Quad that lands on the Railgun loft) the kind of bold classic you want, or too generous? The alternative keeps the pad but lands it on the north gallery.
2. Arch axes: keep the straight 38–40 m ground lines through the dais (classic, harsh), or offset the arches now?
3. Does Velox have, or want, fall damage? The design does not rely on it; with it, the loft → well step (640 cm) becomes a decision instead of a free exit.
4. Nine spawns under the DM1 rules, or a tenth with either a waiver of the "weapon within 8 m" rule or a seventh weapon pickup (a second Plasma Gun in the Crypt)?
5. Quad only, or should a second powerup (Haste is implemented) alternate on the dais? My recommendation: Quad only.
6. Brazier flames: static emissive geometry, or a cheap world-position-offset wobble (opaque, no particles)? It is the difference between "still image" and "alive", at a small vertex-shader cost.
7. Display name: Sacrarium, or one of the alternates — *Atrium Ignis* ("hall of fire"), *Fanum* ("the temple")?
