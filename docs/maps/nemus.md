# Nemus — map concept (theme 4 "Forest")

**Display name:** Nemus (Latin: a sacred grove, woodland with clearings) · **Slug:** `nemus` · **Proposed `MAP_ID`:** `NEMUS` (map `L_Nemus_Blockout` / `L_Nemus`, layout table `Tools/editor/maps/nemus_layout.py`; numbering is the lead's call)
**Status:** concept, docs only (BRIEF.md, 2026-11-12). Nothing here is built or measured; every number is derived from the movement asset, the pad code and the DM1 conventions (ADR 0006 §2/§3/§5/§8, ADR 0005 §4) and must be re-validated by the blockout generator.

## Pitch

- A ruined sanctuary swallowed by a forest of giant trees: a sunken, four-sided stepped amphitheatre in the middle, a temple terrace in the west, the Mother Tree in the east, an aqueduct and a fallen giant log closing the ring north and south.
- FFA for 4–8 players, 68 × 56 m, four height layers (pit 0 / forest floor 320 / ruin ring 640 / tree crown 960).
- Signature 1: **Radix Magna**, one 17.6 m root ramp (20°) from the Mother Tree's gallery straight down into the pit — the map's slide line and its spine.
- Signature 2: an **upper ring with skill gates** (two 400 cm hop gaps on the log, one 700 cm strafe gap on the aqueduct) around a pit that holds the Quad in full view of everything.
- Signature 3: the **Corona**, a small crown platform at +960 with the Railgun — reached only by a pad or a rocket jump, strong view, no cover, no health.

## 1. Theme, mood and why it fits Velox

Early morning in a deep, old forest. Pale limestone ruins (a temple podium, a broken aqueduct, a stepped theatre court) sit between trunks five metres thick; one of the giants has fallen across the south side and was hewn flat into a walkway long ago. Low warm sun breaks through a few gaps in the crown and lies in hard pools on stone and moss; everything below the forest floor is cool and pale as if mist stood in the pit. The mood is quiet and monumental, not jungle-busy: few, large, simple shapes.

Why it fits: the theme gives natural excuses for exactly the geometry Velox movement wants. Trunks are pillars that break rail lines without closing rooms; roots are long ramps (Velox has ramp slide and ramp-exit catch-air, ADR 0002 §2); a stepped pit is a bowl of 80 cm ledges that reward hopping; an aqueduct and a log are narrow elevated strafe lanes with gaps. It is also the visual opposite of DM1 Praesidium (enclosed concrete bunker), so the two maps exercise different things: Nemus is open-sky, ring-shaped and vertical around a central low point, where DM1 is corridor-and-atrium.

## 2. Mode and player count

**FFA, 4–8 players** (design point 6). Not offered for Duel: the upper ring is ~180 m and the map has five major items; a 1v1 would be hide-and-seek. TDM would fit later without layout changes (two natural sides: Templum/west, Arbor/east); CTF does not (no symmetry).

## 3. Size, coordinates and height layers

Centimetres, X east, Y north, 100 cm grid, 400 cm module, lowest walkable floor top at Z = 0 (ADR 0006 E0.3 e). Footprint **X −3400..3400, Y −2800..2800** (68 × 56 m, perimeter walls 100 cm inside that). `KILL_Z −1000` as a safety net only. Open sky: the clip ceiling is at **Z 2000** over the whole map (Corona 960 + the 880 cm knockback apex cap + margin; `veloxmap.CLIP_H` 1800 is too low here), perimeter clip walls run up to it.

| Layer | Z (floor top) | What is there |
|---|---|---|
| L0 Orchestra | 0 | pit floor of the Cavea, 8 × 8 m |
| tiers | 80 / 160 / 240 | three seat tiers, each 200 cm deep, ring-shaped |
| L1 Forest floor | 320 | everything at ground level: Via Sacra, Rivus, forecourt, groves, rim |
| L2 Ruin ring | 640 | Templum terrace, aqueduct deck, the log, Pons N/S, Tabulatum |
| L3 Corona | 960 | crown platform around the Mother Tree |

Level step 320 cm as on DM1, so the DM1 kit walls, the 320 cm rocket-jump rise and the bot rocket-jump link definition carry over unchanged.

## 4. Top-down layout

One character = 100 cm in X, one row = 200 cm in Y (row label = row centre). North is up.

```
     x:     -3000     -2000     -1000     0         1000      2000      3000
            |         |         |         |         |         |         |
  2800  +####################################################################+
  2700  |#..................................................................#|
  2500  |#...................oooo...........................................#|   VIA SACRA (L1)
  2300  |#..@.........................................oooo..................#|   y 2000..2700
  2100  |#..................................................................#|
  1900  |#TTTTTTTTAAAAnnnnAAAAAAAAAAAAAAAnnnnAAAAA:::::::AAAAAAAAAAAAAA.....#|   AQUAEDUCTUS (L2)
  1700  |#TTTTTTTTAAAAnnnnAAAAAAAAAAAAAAAnnnnAAAAA:::::::AAAAAAAAAAAAAA.....#|   y 1600..2000
  1500  |#TTTTTTTT.................................................PPPP.....#|
  1300  |#TTTTTTTT.................................................PPPP...@.#|   NE grove
  1100  |#TTTTTTTW.................................................PPPP.....#|
   900  |#TTTTTTTW...............333333333==333333333...oooo.......PPPP.....#|
   700  |#TTTTTTTW........oooo...332222222==222222233..............PPPP.....#|
   500  |#TTTTTTTW...............332211111==111112233............MMMCCCCCCCC#|
   300  |#TTTTTTTW...............332211        112233............MMMCCCCCCCC#|
   100  |#TTTTTTTT======.........332211@@      //////////////////MMMCOOOOOOC#|   TEMPLUM | CAVEA | ARBOR MATER
  -100  |#TTTTTTTT======.........332211@@      //////////////////MMMCOOOOOOC#|
  -300  |#TTTTTTTT======.........332211        112233............MMMCCCCCCCC#|
  -500  |#TTTTTTTW...............332211111==111112233............MMMCCCCCCCC#|
  -700  |#TTTTTTTW........oooo...332222222==222222233..............PPPP.....#|
  -900  |#TTTTTTTW...............333333333==333333333...oooo.......PPPP.....#|
 -1100  |#TTTTTTTW.................................................PPPP.....#|
 -1300  |#TTTTTTTT.................................................PPPP.....#|   SE grove
 -1500  |#TTTTTTTT............/////////LL..........................PPPP.....#|
 -1700  |#TTTTTTTT....LLLLLLLLLLLLLLLLLLL....LLLLLLLLLLLLLLLLLLLLLLLLLL.....#|   TRABS (L2)
 -1900  |#TTTTTTTT....LLLLLLLLLLLLLLLLLLL....LLLLLLLLLLLLLLLLLLLLLLLLLL.....#|   y -2000..-1600
 -2100  |#..................................................................#|
 -2300  |#..@...............................................................#|   RIVUS (L1)
 -2500  |#...........^^^^^^^^....^^^^^^^^....^^^^^^^^.......@@..............#|   y -2700..-2000
 -2700  |#...........^^^^^^^^....^^^^^^^^....^^^^^^^^.......................#|
 -2800  +####################################################################+
```

Legend: `.` forest floor L1 (320) · blank = Orchestra (0) · `1 2 3` tiers (80/160/240) · `=` stairs (20 cm risers) · `/` root ramp (20°) · `T` Templum terrace (640) · `W` gate wall wings (to 1040) · `A` aqueduct deck (640), `n` arch below it (L1 passage), `:` collapsed span (700 cm gap, open at L1) · `L` log walkway (640) · `P` Pons N/S (bridge, 640, open below) · `M` Tabulatum (640) · `C` Corona (960, above the Tabulatum) · `O` Mother Tree trunk · `o` minor giant trunk (300 cm) · `^` root humps (15°) · `@` jump pad · `#` perimeter.

### Named areas (bounding boxes)

| Id | Area | X | Y | Floor Z | Clip above |
|---|---|---|---|---|---|
| CAV | Cavea (pit + tiers; rim at ±1000) | −1000..1000 | −1000..1000 | 0..240 | sky |
| ORC | Orchestra (pit floor) | −400..400 | −400..400 | 0 | sky |
| FOR | Forecourt (rim west) | −2500..−1000 | −1600..1600 | 320 | sky |
| RIM | Rim strips N / S / E | −1000..2160 | ±1000..±1600; E: 1000..2160 × −1000..1000 | 320 | sky |
| TEM | Templum terrace (solid podium) | −3300..−2500 | −2000..2000 | 640 | sky |
| AQU | Aquaeductus deck (wall with arches below) | −2500..2800 | 1600..2000 | 640 | sky |
| TRA | Trabs, the log (W segment −2100..−200, E segment 200..2800) | −2100..2800 | −2000..−1600 | 640 | sky |
| VIA | Via Sacra (north lane) | −3300..3300 | 2000..2700 | 320 | sky |
| RIV | Rivus (south lane, dry stream bed) | −3300..3300 | −2700..−2000 | 320 | sky |
| GNE / GSE | North-east / south-east grove | 1000..3300 | ±600..±1600 | 320 | sky (Pons overhead) |
| PON / POS | Pons N / Pons S (bridges, underside 620) | 2400..2800 | ±600..±1600 | 640 | sky |
| TAB | Tabulatum (solid podium around the trunk) | 2160..3300 | −600..600 | 640 | Corona underside 940 |
| COR | Corona (bridge-kind slab, underside 940) | 2460..3300 | −550..550 | 960 | sky (2000) |
| ARB | Mother Tree trunk, centre (2880, 0): Ø 600 up to Z 940, Ø 400 above | — | — | — | to clip ceiling |

## 5. Per-area description

**Cavea / Orchestra (centre).** A square stepped bowl, the "amphitheatre in the round": 8 × 8 m floor at Z 0, three concentric tiers of 80 cm (each one jump: jump apex is 114 cm; not walkable, step height is 45 cm), rim at ±1000 on the forest floor. Four fast exits, one per side: stairs north and south (X −150..150, 16 risers of 20 cm, 40 cm treads, |Y| 360..1000, flush with the tiers at risers 4/8/12 so the tiers are nav-connected), the Radix Magna east, pad P1 west. The Quad stands on a low altar in the exact centre. Cover: none on the floor; the tier faces give 80 cm of cover against shots from the same side only. Sightlines: every point of the Orchestra is visible from the whole upper ring and from the Corona (640–960 cm above, 16–35 m away). It is a place to pass through, not to hold.

**Forecourt (west rim).** 15 × 32 m of flat forest floor between the Cavea and the temple, the largest L1 fighting space. Temple stairs in the middle (X −2500..−1860, Y −300..300, 16 risers). Two 300 cm trunks at (−1500, ±700) flank the stairs and cut the diagonals between the temple gate and the rim corners. RL here. Exits: stairs up, arches north (X −2100..−1700) into the Via Sacra, the passage between terrace and log south (X −2500..−2100) into the Rivus, rim strips north and south, Radix Australis up to the log.

**Templum (west, L2).** A 8 × 40 m podium, 320 cm above the forecourt, open to the sky. Along its front edge two gate wall wings (X −2600..−2500, Y 300..1200 and −1200..−300, 400 cm high, no lintel) leave a 600 cm gate at the stairs and open wings north and south of |Y| 1200. Six broken column stubs (80 × 80, 120–200 cm high) stand 300 cm behind the gate as low cover. **RA** behind the south wing at (−3000, −1100), invisible from the Corona, the Cavea and the stairs. Ways in/out: stairs, pad P1 from the pit (lands in the gate), pads P2/P3 from the lane ends onto the north/south wings, the aqueduct at the north end (walk), the log at the south end (400 cm hop), and a drop off the whole east edge.

**Aquaeductus (north, L2) and its wall (L1).** A 400 cm wide deck at 640 on a solid wall, 53 m long, the dividing wall between the Via Sacra and the centre. Two arches (400 wide, 300 high) at X −2100..−1700 and −200..200 are L1 passages. Between X 700 and 1400 the span has collapsed: a **700 cm gap** at L2 (strafe speed needed, ≥ 1100 cm/s; `JUMP_FAST`), full-height open at L1. West of the gap the deck is a 32 m straight run-up with **YA** at (−1200, 1800); east of it 14 m with four 5 HP lead to Pons N. No railings: a miss is a 320 cm drop, no damage (Velox has no fall damage).

**Trabs, the fallen log (south, L2).** A 400 cm wide hewn walkway at 640 on a solid trunk lying on the forest floor; it is the wall between the Rivus and the centre. It is broken twice: a 400 cm gap to the terrace (X −2500..−2100) and a 400 cm break in the middle (X −200..200); both are ordinary jumps (range at ground speed is 540 cm) and both are L1 passages underneath. Four shards in a line on the west segment (a bunny-hop line), **Mega** on the east segment at (500, −1800), 300 cm from the break. Fully exposed to the aqueduct opposite (36 m across the pit) and to the Corona.

**Via Sacra (north lane, L1).** 66 × 7 m, dead flat, the pure strafe lane. Two 300 cm trunks at (−1200, 2500) and (1300, 2300) stand on alternating sides so the racing line weaves but always keeps ≥ 400 cm clear. LG at mid, PG at the west end, pad P2 in the west pocket, open to the NE grove at the east end (X 2800..3300).

**Rivus (south lane, L1).** 66 × 7 m. The southern 400 cm (Y −2700..−2300) carries three root humps (15° up 400 cm, 15° down 400 cm, crest +107 cm) at X −1800, −600, 600 — the ramp-jump line; the northern 300 cm (Y −2300..−2000) stays flat as the walking/bot line. PG and SG, YA at the east end, pads P3 (west pocket) and P5 (to the log).

**Groves (NE / SE, L1).** 10 × 23 m each, between the rim, the Arbor podium and the lanes, under Pons N/S (300 cm clearance). One 300 cm trunk each at (1500, ±900). SG north, RL south, pad P4 (north) to the Corona. These are the map's spawn-friendly back rooms.

**Radix Magna (east).** One root, 400 cm wide (Y −200..200), 20°: from the Orchestra floor (X 400, Z 0) through the east tiers and the rim (X 1280, Z 320) up to the Tabulatum (X 2160, Z 640). Horizontal run 1760 cm, surface length 1873 cm. West of X 1280 it lies in the bowl (the tiers are its stepped sides); between X ≈ 1160 and ≈ 1400 it can be crossed on foot (within 45 cm of the floor level: a shallow trench west of 1280, a low hump east of it; this 2.4 m strip is the only walking link between the two groves, hopping works up to X ≈ 1590); east of that it is a rising wall across the east rim, sealed underneath (fill, ADR 0006 E0.2 d).

**Arbor Mater: Tabulatum (L2) and Corona (L3).** The Tabulatum is a stone podium (solid) around the trunk: a walkway 420 cm deep in the west (root head, 25 HP; 300 cm of it open to the sky) and 300 cm north and south, joined to Pons N/S. The Corona 320 cm above it is a slab around the thinner upper trunk: north and south strips 350 cm deep, west and east strips 220 cm. **RG** on the east strip at (3190, 0) — behind the trunk as seen from the pit, so the holder must step around to use the view. From the west strip one sees the root, the Cavea, the forecourt and both the aqueduct and log decks; one does not see the RA, the lanes, or the groves below.

## 6. Items

Respawn times from `Tools/editor/create_item_content.py` (ADR 0004 §1): health 35 s (Mega included), armor 25 s, ammo 40 s, weapons 5 s (`DA_GameMode_FFA.weapon_respawn_seconds`), powerups 120 s with the first appearance 30–60 s after the start. Z = floor top under the item. Definition names are `DA_Item_<Type>_<Name>`.

| Item | Definition | X, Y, Z | Area | Respawn |
|---|---|---|---|---|
| Quad Damage (FFA; `DisabledInModes` any future Duel) | `Powerup_Quad` | 0, 0, 0 | Orchestra centre | 120 s |
| Red Armor | `Armor_Red` | −3000, −1100, 640 | Templum, behind the south wing | 25 s |
| Mega Health | `Health_Mega` | 500, −1800, 640 | Trabs east segment | 35 s |
| Yellow Armor 1 | `Armor_Yellow` | −1200, 1800, 640 | Aqueduct deck west | 25 s |
| Yellow Armor 2 | `Armor_Yellow` | 2400, −2350, 320 | Rivus east | 25 s |
| Railgun | `Weapon_Railgun` | 3190, 0, 960 | Corona east strip | 5 s |
| Rocket Launcher 1 | `Weapon_RocketLauncher` | −1650, 0, 320 | Forecourt, stair foot | 5 s |
| Rocket Launcher 2 | `Weapon_RocketLauncher` | 2100, −1150, 320 | SE grove | 5 s |
| Lightning Gun | `Weapon_LightningGun` | 0, 2350, 320 | Via Sacra mid | 5 s |
| Plasma Gun 1 | `Weapon_PlasmaGun` | −900, −2150, 320 | Rivus flat line | 5 s |
| Plasma Gun 2 | `Weapon_PlasmaGun` | −2300, 2350, 320 | Via Sacra west | 5 s |
| Shotgun 1 | `Weapon_Shotgun` | 2100, 1150, 320 | NE grove | 5 s |
| Shotgun 2 | `Weapon_Shotgun` | −2300, −2150, 320 | Rivus west | 5 s |
| 50 Health | `Health_Large` | 0, 1300, 320 | North rim, in front of the central arch | 35 s |
| 25 Health × 6 | `Health_Medium` | (−2100, 1300, 320) (−2100, −1300, 320) (2300, 0, 640) (2400, 2350, 320) (−700, 0, 160) (−2900, 1300, 640) | forecourt N/S, root head, Via Sacra east, west tier 2, Templum north wing | 35 s |
| 5 Health × 7 | `Health_Small` | (1600 / 1900 / 2200 / 2500, 1800, 640); (−1800 / −600 / 600, −2500, 427) | aqueduct east deck; the three hump crests | 35 s |
| Armor Shard × 8 | `Armor_Shard` | (−1700 / −1300 / −900 / −500, −1800, 640); (±900, ±900, 240) | log west segment; tier-3 corners | 25 s |
| Rockets | `Ammo_Rockets` | −1300, 900, 320 | forecourt north-east | 40 s |
| Slugs | `Ammo_Slugs` | 2600, −1000, 640 | Pons S | 40 s |
| Lightning | `Ammo_Lightning` | 1000, 2350, 320 | Via Sacra | 40 s |
| Cells | `Ammo_Cells` | 200, −2150, 320 | Rivus | 40 s |
| Shells | `Ammo_Shells` | 2700, 1450, 320 | NE grove | 40 s |
| Bullets | `Ammo_Bullets` | 1300, 500, 320 | east rim | 40 s |

No MG pickup (spawn weapon). No Haste/Regen/Battle Suit (see open question 1). 41 pickups.

**Timing rhythm and control logic.**

- The three things worth timing sit at three corners of the upper ring and never share a line of sight: RA west (25 s), Mega south-centre (35 s), RG/Corona east. RA → Mega along the terrace and the west log is ≈ 40 m including two hop gaps (≈ 5 s running, ≈ 3.5 s at strafe speed); Mega → Corona via Pons S and the rocket jump is ≈ 30 m. One player can cycle RA and Mega (the 25/35 s periods drift against each other, common multiple 175 s), but doing so keeps them on the log and terrace, the two most exposed walkways of the map.
- The Quad (120 s) is at the lowest, most visible point; all four fast exits take ≥ 1 s in the open. P1 delivers the Quad carrier to the temple gate 12 m from the RA, announced by a 1.6 s arc over the forecourt — deliberate: the classic "Quad run" has a strong reward and a loud signature.
- The second-tier armor is placed on the opposite diagonal to the RA (YA1 north-west upper, YA2 south-east lower), so a player denied the west still stacks. The RG holder has no health or armor within 10 m and must drop to refill.
- Weapons are doubled where 8 players need them (RL, SG, PG); LG and RG are single and central/high.

## 7. Spawn points

11 spawns, all on L1, each 5–8 m from a weapon, ≥ 10 m apart, facing into open space, none with a line of sight to Quad, RA or Mega (the generator asserts this with traces from eye height; S7/S9 are the tight ones, the rim occludes the pit by ≈ 10 cm at their distance). `VeloxSpawn::Select` (farthest half + avoid radius) does the rest.

| Id | X, Y (Z 320) | Yaw | Nearest weapon |
|---|---|---|---|
| S1 | −700, 2400 | 0 | LG |
| S2 | 700, 2400 | 180 | LG |
| S3 | −2000, 600 | −45 | RL 1 |
| S4 | −2000, −600 | 45 | RL 1 |
| S5 | −2650, −2500 | 0 | SG 2 |
| S6 | −300, −2150 | 180 | PG 1 |
| S7 | 1600, −1450 | 0 | RL 2 |
| S8 | 2850, −1100 | 180 | RL 2 |
| S9 | 1600, 1450 | 0 | SG 1 |
| S10 | 2850, 1100 | 180 | SG 1 |
| S11 | −2800, 2500 | 0 | PG 2 |

## 8. Movement routes

Reference numbers (`UVeloxMovementSettings` defaults, `DA_MovementSettings_Default`): ground speed 800 cm/s, jump velocity 675 cm/s, gravity −2000 → jump apex **114 cm**, air time 0.675 s, jump range 540 cm at 800, 742 cm at 1100, 945 cm at 1400; step height 45 cm; walkable up to 45.5°. Bot link classes (ADR 0005 §4): `JUMP` ≤ 500 cm at ground speed, `JUMP_FAST` 700 cm at ≥ 1100 cm/s, nothing at 900. Rocket jump: jump + self-splash ≈ 1500 cm/s upward → **≈ 560 cm apex** for a plain floor rocket jump (ADR 0005 §4), absolute knockback apex cap 880 cm (m4-bots/07f); bot links are used for a 320–350 cm rise (DM1, `L_ItemTest`). Ramp exit (ADR 0002 A9): leaving the top of a ramp at horizontal speed v keeps V.z = v·tan(angle).

### Strafe lanes and bunny-hop lines

| Route | From → to | Length × width | Notes |
|---|---|---|---|
| Via Sacra | (−3300, 2350) → (3300, 2350) | 66 m × 7 m, flat | two trunks to weave around, ≥ 400 cm clear; the top-speed lane |
| Rivus flat line | (−3300, −2150) → (3300, −2150) | 66 m × 3 m, flat | narrow; bots walk here |
| Aqueduct west run | (−2500, 1800) → gap edge (700, 1800) | 32 m × 4 m | run-up for the 700 cm gap; needs ≥ 1100 cm/s at the edge (`StrafeRun` reaches ≥ 1.4 × 800 inside its 51 m corridor; the blockout must record the speed reached after 32 m, see §17) |
| Aqueduct east run | (2800, 1800) → gap edge (1400, 1800) | 14 m × 4 m | too short for a standing start; only with speed carried in over Pons N. Link is west → east only |
| Log line | terrace (−2700, −1800) → hop 400 → west log 19 m → hop 400 → east log 26 m → Pons S | 53 m × 4 m with two gaps | bunny-hop line with four shards; gaps clear at ground speed |
| Rim circuit | around the Cavea at x, y ≈ ±1300 | ≈ 100 m loop, 3–6 m wide | circle-strafe loop; crossing the root at X 1160..1400 |
| Tier hopping | any tier ring | 200 cm ledges, 80 cm steps | up: one jump per tier (≈ 0.5 s each); down: free |

### Jump pads

All `AimAtTarget` (the target is the capsule-centre apex, `VeloxJumpPad.h`), launch origin = trigger centre 70 cm above the pad floor, landing computed with the DM1 validator's ballistic model. Landing zones are ≥ 300 × 300 cm clear.

| Pad | Pad X, Y, floor Z | Apex target X, Y, Z (centre) | Launch speed | Lands at X, Y, Z | Horizontal / flight | Feet apex above pad |
|---|---|---|---|---|---|---|
| P1 Orchestra → Templum gate | −300, 0, 0 | −1950, 0, 1050 | 2588 cm/s | −2922, 0, 640 | 2622 cm / 1.57 s | 980 cm |
| P2 Via Sacra west → Templum north wing | −3050, 2350, 320 | −2950, 2000, 820 | 1424 cm/s | −2899, 1823, 640 | 548 cm / 0.99 s | 430 cm |
| P3 Rivus west → Templum south wing | −3050, −2350, 320 | −2950, −2000, 820 | 1424 cm/s | −2899, −1823, 640 | 548 cm / 0.99 s | 430 cm |
| P4 NE grove → Corona north strip | 3110, 1300, 320 | 3110, 640, 1160 | 1909 cm/s | 3110, 369, 960 | 931 cm / 1.24 s | 770 cm |
| P5 Rivus → east log | 1800, −2450, 320 | 1800, −2000, 800 | 1461 cm/s | 1800, −1789, 640 | 661 cm / 0.94 s | 410 cm |

All speeds are well under `MaxLaunchSpeed` 4000. P1 crosses the tiers, the rim and the temple stairs at ≥ 200 cm clearance and passes between the gate wings (600 cm opening, no lintel). P4 passes the Tabulatum and Corona north edges ≥ 100 cm above the Corona floor. Pad triggers use the low m6-16 trigger extent (a pawn hopping through must not launch from mid-air).

### Rocket and plasma jumps

| Id | Take-off → landing | Rise needed | vs Velox rocket jump (≈ 560 cm plain, cap 880) | Bots |
|---|---|---|---|---|
| RJ1 | Pons N (2600, 1000, 640) → Corona north strip (2600, 375, 960) | 320 cm | comfortable (240 cm spare); 450 cm run-up to the Corona edge; open sky above | `ROCKET_JUMP` link |
| RJ2 | Pons S (2600, −1000, 640) → Corona south strip (2600, −375, 960) | 320 cm | as RJ1 | `ROCKET_JUMP` link |
| RJ3 | Via Sacra (−900, 2450, 320) → aqueduct deck (−900, 1800, 640) | 320 cm | comfortable; the only L1 → aqueduct route besides P2 and the terrace | `ROCKET_JUMP` link |
| RJ4 | any L1 floor → any free edge of terrace wings, aqueduct, log, Pons | 320 cm | comfortable everywhere; the upper ring is rocket-jumpable along its whole length | none (humans) |
| RJ5 | Orchestra floor → rim directly | 320 cm | comfortable; the Quad escape that skips the four exits, costs ≈ 35–50 health with the 0.5 self-damage scale | none |
| RJ6 | grove or rim floor (320) → Corona (960) | 640 cm | **not reachable** with one plain rocket jump (560); inside the 880 cap only with stacked knockback. Not a designed route; the pad and RJ1/RJ2 are | none |
| PJ1 | plasma climb on the aqueduct wall, the log side or the Templum front wall | 320 cm | plasma climbing is in the M2 playtest checklist (ADR 0003, task 09 playtest list); flat vertical faces ≥ 400 cm wide are provided at all three | none |

Every rocket-jump take-off has ≥ 900 cm of clearance (open sky, clip at 2000). The Tabulatum is under the Corona (300 cm clear) and is deliberately not a take-off.

### Ramp slides and ramp jumps

| Route | Geometry | What it does |
|---|---|---|
| Radix Magna, downhill | 20°, 1760 cm run, Z 640 → 0 | the slide line: enter from the Tabulatum, hop down into the Orchestra, carry speed across the 8 m floor into the west tiers or onto P1. Whether downhill hops gain speed in Velox as in Q3 is unmeasured (open question 5); the route works either way |
| Radix Magna, uphill | same | walking route L0 → L1 → L2 for everyone including Walk-filter bots. Ramp exit at the head: 800 cm/s → V.z 291 (21 cm of extra height), 1200 cm/s → V.z 437 (48 cm, 520 cm of flight): a fast player leaves the head airborne and lands past the 25 HP on the west walkway (300 cm deep; overshoot ends against the trunk) |
| Radix Australis | 20°, 880 cm run, (−1280, −1450, 320) → (−400, −1450, 640), 300 cm wide, landing pad X −400..−200 joined to the west log | walkable L1 → L2 in the south-centre; downhill it throws a hopping player west along the south rim into the forecourt |
| Rivus humps × 3 | 15° up 400 / down 400, crest +107, 1200 cm apart | ramp-jump chain: exit at 1000 cm/s → V.z 268 (268 cm of flight), at 1400 → V.z 375 (525 cm). A 5 HP on each crest. Airborne arrivals with V.z > 0 slide up the next face (ramp-slide rule) |
| Tier faces | 80 cm vertical | not ramps; listed so nobody adds slopes there (they would turn the bowl into a funnel) |

## 9. Main loops and how control flows

1. **Upper ring (L2), ≈ 180 m.** Templum → aqueduct (700 gap or drop and re-climb) → Pons N → Tabulatum → Pons S → east log (Mega) → break → west log → gap → Templum (RA). Holds RA, YA1, Mega and both Corona rocket jumps. Whoever runs it is silhouetted against the sky the whole time.
2. **Lower ring (L1), ≈ 200 m.** Via Sacra → NE grove → SE grove (crossing the root foot at X 1160..1400) → Rivus → west passage → forecourt → west arch → Via Sacra. Holds all weapons except the RG, YA2 and most health. This is where a fresh spawn arms.
3. **Pit cross (E–W), ≈ 60 m.** Tabulatum → Radix Magna down → Orchestra (Quad) → P1 → Templum → stairs → forecourt → rim → root. The Quad run; also the fastest east → west crossing (≈ 5 s).
4. **South figure-eight.** Forecourt → Radix Australis → west log (shards) → break → Mega → Pons S → RJ2 → Corona (RG) → drop west onto the root → down or around.
5. **Short ladders.** Each lane end has a pad to a terrace wing (P2/P3), so the two lower-ring west corners are 1 s from the upper ring; P5 does the same in the south-east, RJ3 in the north.

**Control.** The map has no single position that sees two major items. West control (RA + RL + gate walls) is the sturdy position but blind to the east; east control (RG + Corona view) sees the Quad, the root and both decks but has no armor nearby and three entrances behind the holder's back. The pit belongs to nobody for longer than a pickup. In an 8-player FFA the expected rhythm is: lanes and groves churn constantly, the upper ring is contested by the two or three strongest players, and every 120 s everything collapses toward the Cavea.

## 10. Sightlines and chokes

| Line | Length | Assessment |
|---|---|---|
| Aqueduct deck ↔ log deck across the Cavea | 36–40 m | the designed rail duel: symmetric, both sides equally exposed, nothing in between. Accepted |
| Corona west strip → Orchestra / root / forecourt | 16–50 m, looking down 640–960 | the strongest view of the map. Mitigations: 220 cm strip without cover, visible from three L2 directions and from the whole forecourt, no health/armor on L3, slugs on Pons S (a drop away), two rocket-jump entrances plus the pad behind the holder, and the trunk at their back makes every rocket a knock-off. Watch item for the soak (dwell on L3) and the playtest |
| Corona → Templum gate along Y ≈ 0 | 52–58 m | open through the 600 cm gate only; RA and the wings are hidden by the gate walls (checked on paper for Corona Y −550..550: the sight line to the RA crosses X −2550 at Y −960..−1050, inside the south wing −1200..−300) |
| Via Sacra end to end | 66 m | broken by the two trunks into ≤ 40 m segments unless standing exactly on the weave line. LG lane by design, rail possible |
| Rivus end to end | 66 m | hump crests (+107) hide a standing player (eye ≈ 125) only partially; flat line fully open. Acceptable: no major item except YA2 at one end |
| Forecourt diagonals | ≤ 35 m | cut by the two forecourt trunks |

Chokes: the two aqueduct arches and the two log passages (each 400 cm wide, 400 cm deep, 300 high) are the only L1 doors between the lanes and the centre besides the collapsed span (700 wide) and the open east ends (500 wide). That is four openings on the north side and three on the south, enough that none can be held; spam through an arch is possible but every arch has a neighbour ≤ 25 m away. The second choke is the 2.4 m root crossing on the east rim (§5): the only walking link between the groves at L1, overlooked by the Tabulatum. It is intentional (the root is the east's dividing wall) and humans can hop the root further east, but it is a watch item for bot traffic. Camping risks ranked: Corona (above), behind the Templum south wing on the RA (answer: three approaches — gate, south wing end, log hop — and splash off the back wall), under Pons at the grove spawns (answer: spawn selection plus four exits per grove).

## 11. Readability and palette

Style guide rule: environment HSV value ≤ 0.50 and saturation ≤ 0.25; player colours value ≥ 0.60 and saturation ≥ 0.70. A forest has to earn this: **no saturated green anywhere**.

| Surface | Material (new `MI_Nemus_*` of `M_Kit_Master`) | Hue / S / V |
|---|---|---|
| Ruin stone (walls, tiers, decks) | `Stone` | 40° / 0.10 / 0.48 |
| Shadowed stone, podium bodies | `StoneDark` | 220° / 0.10 / 0.30 |
| Forest floor (moss flagstones) | `Moss` | 95° / 0.22 / 0.34 |
| Earth, stream bed | `Earth` | 30° / 0.22 / 0.26 |
| Bark (trunks, roots, log) | `Bark` | 25° / 0.20 / 0.22 |
| Leaves (canopy clusters, ferns, ivy) | `Leaf` | 150° / 0.24 / 0.28 |
| Pad rings, pedestals, altar | `Bronze` (verdigris) | 170° / 0.20 / 0.40 |
| Sky dome | `Sky` (unlit, exempt like the emissive fixture) | painted |

Against the six skins (Red, Blue, Green, Yellow, Magenta, Cyan): the walkable surfaces are warm grey stone and dark moss, so every skin wins on saturation by ≥ 0.45 and on value by ≥ 0.1–0.3. The two at risk are **Green and Cyan in front of foliage**; therefore `Leaf` is pushed to a blue-green hue at value 0.28 (a bright green skin is at least 0.32 brighter), foliage never forms a background at player height (rules below), and the "mist" is expressed as a value gradient: L0 surfaces are lit cool and pale, which makes the pit the place where skins read best. Small green items (5 HP, shards, the pad disc) sit on stone or bark, never on moss with ferns around.

**Foliage rules (binding).** Low-poly, flat-shaded, opaque (no alpha cards: ADR 0006 §8 allows no translucency; masked materials are avoided too so the forward renderer does no alpha test on big surfaces). Three shapes only: canopy clusters (crown blobs, underside ≥ Z 1300, i.e. ≥ 200 cm above a head on the Corona), fern clusters (≤ 60 cm tall, only within 100 cm of a wall or trunk foot), ivy panels (flat, ≤ 3 cm proud of walls). Sparse and clustered: ≤ 12 canopy clusters, ≤ 40 fern clusters, ≤ 20 ivy panels. No foliage has collision; nothing between 60 cm and 1300 cm above any walkable surface, so foliage can never hide a player or a part of one except their boots. Trunks and roots collide through clip boxes only: a trunk is two boxes (one rotated 45°) forming an octagon, a root is a ramp box.

## 12. Lighting plan (baked, static only)

- **One static directional sun**, warm (≈ 4300 K), low (pitch ≈ −38°, from the south-east, so the Templum front and the Cavea's north and west tiers catch it and the Corona's west strip looks out of shade into light). The canopy clusters and trunks are static shadow casters: Lightmass bakes the dappled pools and trunk shadows into the lightmaps. **These pools are the god-rays**: hard-edged sun patches on the tiers, the forecourt, the log and the Via Sacra, plus painted shafts in the sky dome behind them. No light-shaft cards, no volumetrics (open question 3).
- **Static sky light** with a new 128 px cubemap `TC_Sky_Nemus` (cool green-grey zenith, pale warm horizon).
- **Mist by baked/static means:** (a) cool, slightly brighter fill in L0 and under the Pons/Corona (static point lights, bluish), so depth reads as haze; (b) the sky dome's lower third is a pale haze band into which painted distant trunks fade; (c) no height fog by default (open question 2).
- **Static fill lights:** ≈ 24 (Orchestra 4 cool, under Corona 3, under Pons 2 + 2, arches and passages 6, Templum gate and wings 4 warm, grove backs 3). With sun and sky ≈ 26 static lights (DM1: 31). 8 sphere reflection captures at 128 px.
- Lightmap resolutions as ADR 0006 §5: ground tiles 800 cm at 256 (same 3.1 cm texel as DM1's floor), walls/decks 64, trunks/roots 64, trims/tiers 32–64. Volumetric lightmap cell 200 cm. Bake on the Mac (path B, E0.1); `Velox.Maps.LightingBuilt` gates the merge.
- **Skybox:** an opaque unlit dome mesh (`SM_Nemus_SkyDome`, ≈ 200 tris, one 1024 × 512 texture): canopy silhouette with gaps, receding trunks, haze band, painted shafts. It is the only visible sky; nothing dynamic.
- An open-sky map with a directional light is new for the pipeline (DM1 is all point lights indoors): `veloxmap.static_sun_and_sky()` exists from the test maps, the Lightmass importance volume covers the full footprint, expected bake time is above DM1's.

## 13. Kit: reused and new pieces

Budget rule (ADR 0006 §8): ≤ 25 distinct meshes × ≤ 8 materials on the map.

**Reused from the DM1 kit (10), re-skinned by material instance only:** `Floor_400` (terrace, decks, Tabulatum, Corona), `Wall_400`, `Wall_Half` (gate wings, podium faces), `Pillar` (column stubs, scaled in Z), `Stairs_5x20` (scaled to the 40 cm tread), `Ramp_15` (humps), `Ledge_Edge`, `Bridge_400` (Pons), `Pad_Ring`, `Pedestal` (also the altar). Not used: door pieces, ceiling, beam, light fixture, railing, skirting.

**New pieces (13), all ≤ 600 tris, one material slot, `NoCollision`:**

| Mesh | Size (cm) | Use |
|---|---|---|
| `SM_Nemus_Ground_800` | 800 × 800 × 20 | forest floor; halves the tile count of an outdoor map |
| `SM_Nemus_Tier_400` | 400 × 200 × 80 | Cavea seat block (straight; corners overlap) |
| `SM_Nemus_Arch_400` | 400 × 400 × 620 | aqueduct pier with a 300-high arch; plain piers are `Wall_400` stacks |
| `SM_Nemus_Trunk_300` | Ø 300 × 400, octagonal | minor trunks, stacked |
| `SM_Nemus_Trunk_600` | Ø 600 × 320, octagonal | Mother Tree lower trunk (the upper trunk is `Trunk_300` scaled to 400) |
| `SM_Nemus_Root_20` | 440 run × 400 × 160 rise | root ramp module with bark flanks; ×4 Radix Magna, ×2 Radix Australis (scaled to 300 wide) |
| `SM_Nemus_Log_800` | 800 × 400 × 320 | hewn log: flat 400 cm top, sides inside the clip box |
| `SM_Nemus_Log_End` | 200 × 400 × 320 | broken end / root plate (four needed) |
| `SM_Nemus_TreeWall_800` | 800 × 100 × 1200 | perimeter: fused trunks and ruin wall |
| `SM_Nemus_Canopy` | ≈ 1200 × 1200 × 500 | crown cluster, ≤ 300 tris |
| `SM_Nemus_Fern` | ≈ 150 × 150 × 60 | fern cluster, ≤ 60 tris |
| `SM_Nemus_Ivy` | 400 × 3 × 300 | flat ivy panel |
| `SM_Nemus_SkyDome` | radius ≈ 20000 | sky |

23 distinct meshes, 8 materials (§11), 6 new textures ≤ 1024² (stone, moss floor, bark, leaf, bronze, sky). The art theme stays the q32 low-poly flat-shaded direction; nothing here needs a technique the DM1 kit did not use.

**Generator (`veloxmap.py`) extensions the blockout task needs:** ramps with a yaw (the hump down-faces rise along −X; today `ramp()` rises along +X only), stairs along Y and along −Y (pit stairs; `stairs()` rises along +X only), an octagon helper (two boxes, one yawed 45°), a per-map clip-ceiling height (2000), and a spawn/major-item line-of-sight assert. No C++ change is required by the layout.

## 14. Draw-call and triangle estimate

DM1's audit counted 568 draw calls for 518 kit instances (estimate ≈ one call per placed component plus ≈ 50). Nemus must therefore stay near **≤ 500 placed components** under the same estimator.

| Group | Instances |
|---|---|
| Ground (`Ground_800`, L1 minus the Cavea; Orchestra) | 62 |
| Tiers | 42 |
| Stairs (pit ×2, temple) | 8 |
| Templum (podium faces, floor, wings, column stubs) | 46 |
| Aqueduct (arches, piers, deck) | 40 |
| Log and ends | 10 |
| Pons N/S (decks, piers, ledge edges) | 14 |
| Arbor: podium, Tabulatum floor, Corona, trunk | 30 |
| Roots and humps | 12 |
| Minor trunks (6 × 4 segments) | 24 |
| Perimeter tree wall | 31 |
| Canopy / fern / ivy | 12 / 40 / 20 |
| Pad rings, pedestals | 5 / 6 |
| Sky dome | 1 |
| **Total** | **≈ 403** |

Estimated **≈ 455 draw calls** by the DM1 estimator (budget 600), **≈ 30 k triangles** in the whole map (budget 300 k in view; DM1 has 18 k), lightmaps ≈ 30 MB (budget 48: 62 ground tiles at 256 dominate with ≈ 21 MB), textures < 40 MB (budget 256). The risk is not the budget but that this map, unlike DM1, can show almost all of itself from the Corona; the estimate above already assumes everything is in view. If dressing grows, the lever is instancing in the generator (one ISM component per mesh/material), which would cut the real figure to a few dozen calls (open question 6).

## 15. Bots and navigation

No new bot capability is required: the map uses only link types that exist (ADR 0005 §4) and has **no void, lava, water or kill volume**. The Rivus is a dry bed, painted, with no water volume.

| Link | Count | Where |
|---|---|---|
| `JUMP_PAD` | 5 | P1–P5, pad floor centre → authored landing |
| `ROCKET_JUMP` | 3 | RJ1, RJ2, RJ3 (rise 320, run-up 450 cm) |
| `JUMP` (both ways) | 2 | terrace ↔ west log (X −2500..−2100), west log ↔ east log (X −200..200); start 60 cm before the edge, end 100 cm onto the far side |
| `JUMP_FAST` (west → east only, 1100 cm/s) | 1 | aqueduct gap X 700..1400 |
| `DROP` | ≈ 22 | terrace east edge (6), aqueduct deck both sides (6), log both sides (6), Pons (2), Corona north → Pons N (2600, 1000) and south → Pons S, Corona west → root at (2000, 0, 582). Only where the landing is a clear ≥ 300 × 300 floor ≥ 500 cm from a wall and not a pad (ADR 0006 E3) |

- **Walk-only reachability.** Every item except the Railgun is reachable with the Walk filter: L2 by the temple stairs, Radix Magna and Radix Australis; the Mega from the east over Pons S (no jump) or from the west over the 400 `JUMP`; YA1 from the terrace. The Corona needs a pad (all filters) or a rocket jump (Full).
- **The Cavea.** Tiers are 80 cm, above the 45 cm step height, so the nav mesh on each tier ring is an island that connects to the pit stairs sideways (the risers are flush at 80/160/240) and to the root's flanks where the height difference is ≤ 45 cm. Bots reach tier items by the stairs; they do not hop tiers (no links, by decision: 24 extra links for eight shards and a 25 HP is not worth it). A bot that lands on a tier after a drop is on nav and walks to the stairs.
- **D41.** No walkable surface without nav unless sealed: the Templum, Tabulatum, aqueduct wall and log are solid `platform`/`wall` kinds (filled); the roots are `ramp` kind with fill boxes below; Pons N/S and the Corona are `bridge` kind with 300 cm clearance. Trunk tops, gate-wing tops, the tree wall and column stubs above 45 cm are clipped to the ceiling or sloped so no isolated nav islands form (navlint `isolated`).
- **Edges instead of hazards.** A fall is never lethal. The cost of the open decks is route failure: the bot's `missed` handling re-queries from where it lands. Expect the aqueduct gap to be the weakest link; if `JUMP_FAST` success is < 90 % it is removed and the gap stays a human route (bots then use Pons N for the east deck).
- **Nav build.** Walkable area ≈ 3 700 m² (DM1: 2 400 m², 0.7–1.2 s). The ≤ 2 s invariant is at risk (§18).
- **Bot count.** FFA soaks at 8 participants; per-map bands start report-only (`infra/ci/bands/L_Nemus.sh` after three sets, ADR 0006 §7).

## 16. Hazards and rules

There are none beyond height. No pits, no damage volumes, no movers, no teleporters (none exist in code). `KillZ −1000` and the clip walls/ceiling are safety nets and must never trigger (`left_arena 0`, `world_kill 0`). Self-inflicted rocket/plasma damage follows the combat settings (self-damage scale 0.5). If fall damage is ever added to Velox, the 640 cm Corona drops need a re-check.

## 17. Acceptance ideas for the blockout

1. `create_nemus_map.py --validate-only` (pure Python, like DM1): walkable widths ≥ 120, ledges ≥ 120, openings 240–700 as tabled, pad arcs simulated with landing zones ≥ 300 × 300 and launch speeds ≤ 4000, rocket-jump take-offs with ≥ 900 clearance, spawns ≥ 10 m apart and ≤ 8 m from a weapon, ammo 6–12 m from its weapon, line-of-sight asserts (spawns ↛ Quad/RA/Mega; Corona ↛ RA), RA ↔ Mega upper-ring distance 38–45 m.
2. `navlint.sh L_Nemus_Blockout` → `clusters=0`, `lowclear=0`, no `NavLintIgnore`; nav build time recorded (target ≤ 2.0 s).
3. `velox.bots.DumpPadLandings`: all five landings within 60 cm of their link ends.
4. `FtNemusReachability` (console FT in `fts.sh`): `FindPath` Full from every spawn to every pickup and between all majors; additionally Walk-filter paths to every pickup except the Railgun.
5. `Content/Data/MoveScripts/NemusRun.json` in the soak list, judged like `PadRun`: the five pads; Via Sacra strafe run with the speed at the 32 m marker logged; aqueduct gap crossed at that speed; Radix Magna top-to-bottom hop run with entry and exit speed logged (answers open question 5); one hump chain pass.
6. Report-only soak sets (BotRun ×3, FFA ×10 at 8 bots): the map-independent invariants (ADR 0006 §7) hold; link success ≥ 90 % per type; every pickup taken at least once; **dwell on L3 ≤ the DM1 camp band** (the Corona watch item); spawn-kill metric (deaths within 2 s of spawning) reported per spawn.
7. Art map later: `audit_map.py` budgets, collision diff 0, protrusion 0, `Velox.Maps.LightingBuilt`; on the Mac a screenshot set of all six skins against moss, stone, bark and foliage, and `stat unit` from the Corona west strip (worst-case view).

## 18. Risks and open questions

**Risks**

1. **Corona dominance.** A crown platform over the central pit is the classic camping problem. The mitigations are listed in §10; if the soak or the playtest shows it anyway, the fallback is cheap: move the RG to the Tabulatum west walkway and leave only the 25 HP and the view upstairs.
2. **Open sky = everything visible.** The estimate fits the budget, but there is no occlusion to hide a mistake, and the first directional-light bake of the project is on this map.
3. **Green/Cyan skins against foliage** — handled by palette and placement rules, but only a GPU screenshot proves it.
4. **Nav size and bot CPU at 8 bots** on 3 700 m²; nav build may exceed 2 s (remedies: 100 cm perimeter insets already exclude the tree wall; shrink the lanes to 6 m; raise the invariant for this map by ruling).
5. **The 700 cm gap** depends on how fast a player actually is after 32 m; if the blockout shows < 1100 cm/s for an average strafe run, shorten the gap to 600 or lengthen the run by moving the gap east.
6. **Quad → RA by P1** may snowball in small lobbies; the lever is P1's landing (retarget to the forecourt stair head) without touching anything else.
7. **Pit stairs at 26.6°** (40 cm treads) are steeper than DM1's; if movement over them stutters, lengthen them 160 cm into the Orchestra (50 cm treads).

**Open questions for the human / lead**

1. Powerup: Quad as specified, or Regeneration (fits the grove, less snowball with 8 players)? One `Item` record either way; no other powerup has been soaked on a real map yet.
2. Height fog: allow a very light static `ExponentialHeightFog` (max opacity ≤ 0.15, start ≥ 30 m) if the Mac measurement shows it costs < 0.2 ms and skins still read at 50 m? Default in this concept: no fog.
3. God-rays: stay with baked sun pools plus painted shafts (default), or amend ADR 0006 §8 to permit ≤ 6 unlit additive shaft cards above head height?
4. Map numbering and `MAP_ID` (`NEMUS` proposed), and whether TDM on this map is wanted in M7.
5. Downhill ramp behaviour: should hopping down a ramp gain speed as in Q3? Today's rules (ADR 0002) define uphill slide and ramp exit; the blockout run will measure what downhill does. If the answer is "no gain", is that the intended feel?
6. May the map generator move to instanced static mesh components per kit mesh (affects `audit_map.py`'s draw-call estimator and the collision diff)? Not needed for this concept's counts, but it is the headroom for dressing.
