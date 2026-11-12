# Orbita — map concept (theme 3 "Space")

**Display name:** Orbita (Latin: track, orbit) · **slug:** `orbita` · **maps:** `L_Orbita_Blockout` / `L_Orbita` · **status:** concept, docs only (BRIEF.md, 2026-11-12)

## Pitch

1. Seven decks of a broken orbital station hang in open void around a stepped central hub; nothing has a wall, every edge is a way to die.
2. Six jump pads throw players across 16–35 m of nothing on 1.2–1.6 s arcs: the fastest way round the map is also the most exposed one.
3. A narrow rail truss (RG) faces an armour orbit (RA, Mega, YA): the player with the stack has to fly, the player with the rail gets to shoot at fliers.
4. Knockback is a weapon: a rocket near an edge is a kill, and the kill is credited to whoever hit last.
5. Duel first, FFA up to 6; normal gravity, baked sun, black sky: bright skins against black space are the most readable picture Velox can draw.

## 1. Theme, mood, fit

A derelict ring station in low orbit: the habitat ring has broken into separate decks that drift, still powered, around the surviving hub. Hard white sunlight from one side, faint blue planetshine from below, a dark starfield everywhere else; each deck has one identity colour and one landmark (antenna dish, grow racks, cargo containers, solar wing, a captured rock). The mood is exposure: you can see every opponent and every item from almost anywhere, and they can see you.

Why it fits Velox: the map is a movement exam with no hiding place for bad netcode or bad air control. Pad arcs, air steering, knockback and strafe speed decide fights more than aim alone; the theme needs no walls or ceilings, so the art cost per square metre is the lowest of any theme and the frame-rate risk is small; and the black background gives the bright skin set (V >= 0.60, S >= 0.70, `docs/art/style-guide.md`) maximum contrast. It is deliberately the opposite of DM1 Praesidium (enclosed, three storeys, no out-of-arena), so the pair covers both classic arena families.

It is an original layout. It shares only the genre idea "platforms over void with pads" with classic void maps: the stepped hub, the rail truss, the armour orbit and the item set are its own.

## 2. Mode, players, size, layers

- **Modes:** Duel (primary), FFA 3–6 (secondary). Not for 8: every extra player on a void map multiplies third-party ring-offs.
- **Footprint:** X −4400..4200, Y −3400..3800 (86 × 72 m). Walkable area about 2 400 m2, of which the hub is 1 024 m2. The rest of the footprint is void.
- **Gravity:** the project default (`GravityZ −2000`). See open question Q1.
- **Height layers (feet level, cm):** −320 Hortus · 0 hub lower deck, Armamentarium, Horreum, bridges · +160 Scopulus · +320 Corona and Trabs · +640 Specula. `KillZ −1400`. No clip walls, no clip ceiling (nothing to stand on above +640; projectiles end by lifetime, 10 s for plasma).
- **Grid:** 100 cm grid, 400 cm kit module, all coordinates below are multiples of 100 (ADR 0006 section 2).

## 3. Layout

Top-down, north up. One column = 200 cm in X, one row = 400 cm in Y (row label = centre of the row). The drawing is schematic at half-module accuracy; **the area table is binding**. Digits are pad launch points.

```
   X:  -4000 -3000 -2000 -1000     0  1000  2000  3000  4000
           |    |    |    |    |    |    |    |    |
  3600  .......QQQQQQ.....GGGGGGGG..................
  3200  .......QQQQQQ...jjG4GGGGGG..................
  2800  .......QQQQQ3.....GGGGGGGG..................
  2400  .....................//.....................
  2000  .....................//.....................
  1600  .....................//................T6T..
  1200  ..............HHHHHHHHHHHHHHHH.........TTT..
   800  ..............HHHHH//////HHHHH.........TTT..
   400  AA2AAA........HHHHCCCCCCCCHHHH........TTTTT.
     0  AAAAAA===..===HHHHCCSSSCCCHH1H........TTTTT.
  -400  AAAAAA........HHHHCCCCCCCCHHHH........TTTTT.
  -800  AAAAAA........HHHHCCCCCCCCHHHH.........TTT..
 -1200  ..............HHHHH//////HHHHH.........TTT..
 -1600  ..............HHHHHHHHHHHHHHHH.........TTT..
 -2000  .....................==................//...
 -2400  .....................==................//...
 -2800  ...............RR5RRRRRRRRRRRRRR.......//...
 -3200  ...............RRRRRRRRRRRRRRRRRsssssssss...

 H Nucleus (hub, Z 0)   C Corona (Z 320)   S Specula (Z 640)   A Armamentarium (Z 0)
 G Hortus (Z -320)      j jetty (Z -320)   Q Scopulus (Z 160)  R Horreum (Z 0)
 s Semita (Z 0)         T Trabs (Z 320)    = bridge            / ramp (15 deg)    . void
```

### 3.1 Area table (binding; cm; all decks are solid clip boxes 200 thick below the stated top)

| Id | Name | Extent X | Extent Y | Top Z | Role |
|---|---|---|---|---|---|
| N0 | **Nucleus** (hub lower deck) | −1600..1600 | −1600..1600 | 0 | Main arena, an 800 cm wide band around the Corona block |
| N1 | **Corona** (hub upper deck, solid block) | −800..800 | −800..800 | 320 | 500 cm band around the spindle; blocks every sightline across N0 |
| N2 | **Specula** (spindle top) | −300..300 | −300..300 | 640 | 600 × 600 lookout; Quad in FFA |
| N2d | Antenna dish (backstop, solid) | −300..300 | −300..−250 | 640..940 | Stops the P4 arc; hides Specula from the south |
| RN | Hub ramp north | −600..600 | 800..1200 | 0 → 320 rising to +X | 15 deg, 1200 run, landing X 600..800 |
| RS | Hub ramp south | −600..600 | −1200..−800 | 0 → 320 rising to −X | point-mirror of RN, landing X −800..−600 |
| PA | **Pons Australis** (south bridge) | −200..200 | −2400..−1600 | 0 | 8 m, the only gap-free link to the hub |
| HR | **Horreum** (cargo deck) | −1400..2000 | −3400..−2400 | 0 | RL; containers for cover |
| SE | **Semita** (catwalk) | 2000..3900 | −3200..−2800 | 0 | 19 m, 4 m wide; YA at the elbow |
| CO | Clivus Orientis (east ramp) | 3500..3900 | −2800..−1600 | 0 → 320 rising to +Y | the walkable way onto Trabs |
| TR | **Trabs** (rail truss) | 3400..4000 | −1600..2000 | 320 | 36 m × 6 m; RG at the north tip |
| TRn | Trabs node (widening) | 3200..4200 | −500..500 | 320 | P1 landing; backstop X 4150..4200, Y −400..400, Z 320..620 |
| PFw / PFe | **Pons Fractus** (broken west bridge) | −3200..−2600 / −2200..−1600 | −200..200 | 0 | 400 cm gap at X −2600..−2200 |
| AR | **Armamentarium** (west dock) | −4400..−3200 | −800..800 | 0 | RA |
| CL | **Clivus** (north ramp) | −200..200 | 1600..2800 | 0 → −320 falling to +Y | 12 m downhill into Hortus |
| HO | **Hortus** (grow deck) | −800..800 | 2800..3800 | −320 | Mega; lowest deck, overlooked from N0 and Trabs |
| HOj | Hortus jetty | −1200..−800 | 3000..3400 | −320 | landing for the Scopulus jump |
| SC | **Scopulus** (captured rock) | −3000..−1800 | 2600..3800 | 160 | 12 × 12 m; rock backstop X −2200..−1800, Y 3400..3800, Z 160..460 |

### 3.2 Per-area notes

- **Nucleus (N0).** Four straight bands, each 32 m long and 8 m wide (4 m beside a ramp). Standing players on opposite bands cannot see each other (Corona is 320 high, eye height about 125). Cover: one pylon 200 × 200 × 400 per corner at (±1100, ±1100). Open to the void on all four outer edges; sees Armamentarium (16 m west), Horreum (8 m south), Hortus (12 m north, 320 below) and Trabs (16 m east, 320 above).
- **Corona (N1).** A 5 m wide ring walk at +320. Reached by RN/RS or a rocket jump; left by walking off any side. The spindle (X/Y ±300, Z 320..640) and the dish cut it into four mutually hidden sides. A run-jump off a Corona edge at 800 cm/s lands exactly on the N0 lip (reach 797 cm for a 320 drop): faster than that it overshoots the hub. On the four axes something catches you (north: Clivus, south: Pons Australis, west: Pons Fractus east stub, east: pad P1); on the diagonals nothing does. This is intended and is the map's first lesson.
- **Specula (N2).** 6 × 6 m, no cover except the dish on its south edge. Sees Hortus (33 m), Trabs (37 m), Armamentarium (42 m); cannot see Horreum. Any splash hit throws the occupant down to Corona or N0 (no fall damage in Velox, ADR 0003), not into the void.
- **Armamentarium (AR).** 12 × 16 m. RA in the north-west corner behind a cover block 100 × 400 × 200 at X −3950..−3850, Y 100..500, so it cannot be railed from the hub. A 120 cm parapet along the west edge (Y −400..400). Three ways in or out: Pons Fractus, P5 in, P2 out.
- **Horreum (HR).** 34 × 10 m, the largest outer deck and the safest place on the map. Two containers: K1 400 × 200 × 160 at X −600..−200, Y −3300..−3100 with a 100 cm crate beside it (step up; also the plasma-hop target), K2 400 × 200 × 200 at X 800..1200, Y −2700..−2500. Parapet 120 high on the south edge X −400..400.
- **Semita and Clivus Orientis.** A 4 m wide catwalk with void on both sides, then a 12 m ramp up to Trabs. YA sits on the elbow (3700, −3000), the far south-east corner: taking it means a 19 m walk in the open, in rail view of Trabs and Horreum.
- **Trabs (TR).** 36 m long, 6 m wide, 320 above the hub deck. Two parapets 120 high on the west edge (Y 800..1200 and Y −1200..−800; a crouched player, 100 cm, is hidden), one pylon 200 × 200 × 300 at (3500, 1500) near the RG. The node is 10 m wide so the P1 landing has room. Every splash hit here threatens a ring-off.
- **Clivus and Hortus (CL, HO).** A 15 degree ramp falling 320 over 12 m into a 16 × 10 m deck. Mega at the far north edge (0, 3650), back to the void. Two grow racks 400 × 100 × 200 at X −600..−200, Y 3300..3400 and X 300..700, Y 3550..3650 (green emissive strips: the landmark). Hortus is looked down on from the N0 north band and from Trabs: the Mega is never free.
- **Scopulus (SC).** A rock with a flattened top at +160. Reached only by P2 or by a rocket jump from the jetty; left by P3 or by the drop-jump to the jetty. The rock outcrop in the north-east corner is the P2 backstop.

## 4. Movement reference numbers (from `UVeloxMovementSettings` defaults, `Source/Velox/Movement/VeloxMovementSettings.h`)

`MaxGroundSpeed 800 cm/s`, `JumpVelocity 675`, `GravityZ −2000`, `StepHeight 45`, `WalkableFloorAngle 45.5`. Derived: jump apex 114 cm, air time 0.675 s; flat jump reach 540 cm at 800 cm/s and 756 cm at 1 120 cm/s (the `StrafeRun` acceptance factor 1.4); reach with a 320 drop 797 cm, with a 480 drop 887 cm (both at 800 cm/s). Knockback 12.5 cm/s per damage point, capped at 200 damage (`UVeloxCombatSettings`): a 100-damage rocket is 1 250 cm/s. Highest knockback take-off seen 1 875 cm/s, apex about 880 cm (`docs/tasks/m4-bots/07f-test-map-killz.md`); DM1's two `ROCKET_JUMP` links climb 320 cm. Plasma splash 15 damage gives about 190 cm/s, so a plasma hop peaks near 187 cm (computed, not measured).

## 5. Movement routes

### 5.1 Jump pads (six; all `AimAtTarget`, feet-level coordinates; the generator adds `PAD_LAUNCH_Z`)

Computed for `GravityZ −2000` with the apex as the pad target (`AVeloxJumpPad` semantics). `MaxLaunchSpeed` is 4 000 cm/s.

| Pad | Launch (x, y, z) | Apex (x, y, z) | Landing (x, y, z) | Range | Flight | Launch speed | Horizontal speed | Run-out guard |
|---|---|---|---|---|---|---|---|---|
| **P1** Nucleus east → Trabs node | (1300, 0, 0) | (2660, 0, 520) | (3500, 0, 320) | 22 m | 1.17 s | 2 370 | 1 880 | backstop at X 4150 (650 cm) |
| **P2** Armamentarium → Scopulus | (−4000, 600, 0) | (−3255, 1880, 700) | (−2600, 3000, 160) | 28 m | 1.57 s | 2 440 | 1 770 | rock outcrop behind the landing |
| **P3** Scopulus → Nucleus north-west | (−2000, 2800, 160) | (−1680, 2160, 560) | (−1300, 1400, 0) | 16 m | 1.38 s | 1 700 | 1 130 | 800 × 800 corner, then the Corona wall |
| **P4** Hortus → Specula ("Ascensus") | (−500, 3200, −320) | (−150, 940, 840) | (0, 0, 640) | 32 m, +960 | 1.52 s | 3 030 | 2 130 | dish at Y −250; clears the spindle lip by about 110 cm |
| **P5** Horreum → Armamentarium ("Arcus Longus") | (−1000, −2900, 0) | (−2300, −1700, 640) | (−3600, −500, 0) | 35 m | 1.60 s | 2 730 | 2 210 | 800 cm of deck along the arc |
| **P6** Trabs north tip → Hortus | (3700, 1800, 320) | (2545, 2340, 620) | (500, 3300, −320) | 35 m, −640 | 1.52 s | 2 570 | 2 330 | arc points along the 16 m deck toward the Mega |

Rules: every landing zone is a clear >= 400 × 400 cm floor; along the arc direction there is either >= 700 cm of deck or a solid backstop, because a pawn lands with 1 100–2 300 cm/s of horizontal speed and Velox keeps it (Q3 pads). The run-out distances are estimates until the blockout measures them (acceptance A3). No pad arc passes over another deck below 250 cm clearance. All six are one-way; no pad returns to where another came from, so the pads give the map a direction.

### 5.2 Strafe lanes and bunny-hop lines

| Line | Path | Length × width | Notes |
|---|---|---|---|
| SL1 Cargo straight | Horreum west edge → Semita elbow along Y −3000 | 53 m × 10 m, then 4 m | longest lane; no step; ends at the YA |
| SL2 Truss | Trabs Y −1600..2000 | 36 m × 6 m | about 4.5 hops at 1 120 cm/s; entered at speed from Clivus Orientis |
| SL3 Hub circuit | the four N0 bands | 4 × 32 m, 8 m wide | closed bhop circuit, four 90 degree turns, about 96 m per lap |
| SL4 West axis | Armamentarium west edge → Corona west wall along Y 0 | 36 m × 4 m on the bridge | the 400 cm gap is crossed in stride (one hop) |
| SL5 North descent | N0 north band → Clivus → Hortus | 8 m + 12 m ramp + 10 m | downhill hops and ramp slide gain speed; the deck ends 10 m after the ramp foot |

Run-up: 8–16 m of flat deck precede SL1, SL2 (via the ramp), SL4 and SL5; three hops reach about 1 100 cm/s.

### 5.3 Gap jumps and drops

| Jump | From → to | Gap / height | Needs | Bots |
|---|---|---|---|---|
| G1 Pons Fractus | X −2600 ↔ X −2200, Y ±200 | 400 flat | >= 593 cm/s; reach 540 at run speed | `JUMP` link both ways, `required_speed 650` |
| G2 Scopulus → jetty | (−1800, 3200, 160) → (−1200..−800, 3000..3400, −320) | 600, drop 480 | >= 541 cm/s; reach 887 at run speed | `JUMP` link one way, `required_speed 600` |
| G3 Corona launches | Corona edge on an axis → Clivus / Pons Australis / PF east stub / P1 | 800, drop 320 | >= 800 cm/s, on the axis | none (human line) |
| G4 Saltus | N0 north edge beside Clivus → Hortus | 1 200, drop 320 | >= 1 205 cm/s | none (expert line) |
| D1 Corona → N0 | any side, walk off | drop 320 | walk speed | 8 `DROP` links, two per side, clear of the ramps |
| D2 Specula → Corona | north, east, west | drop 320 | walk speed | 3 `DROP` links |

### 5.4 Rocket and plasma jumps

| Id | From → to | Rise needed | Horizontal | Versus capability | Bots |
|---|---|---|---|---|---|
| RJ1 | Corona north or west band → Specula | 320 | 150–450 | same class as DM1's two links (320); apex cap about 880 | 2 `ROCKET_JUMP` links: (0, 650, 320) → (0, 150, 640) and (−650, 0, 320) → (−150, 0, 640) |
| RJ2 | N0 anywhere → Corona | 320 | 100–400 | as RJ1 | none needed (ramps exist) |
| RJ3 | Hortus jetty west end → Scopulus | 480 | 600 gap + 100 | needs take-off vz >= 1 500 (apex 562, reach 830 at 800 cm/s); comfortable at 1 675 (reach 1 046) | none until measured (A4) |
| RJ4 | Hortus south edge → N0 north band beside Clivus | 320 | 1 200 gap | needs vz about 1 675 and >= 830 cm/s forward | none (expert line) |
| PJ1 | Horreum floor → container K1 roof | 160 | — | plasma hop about 187 (computed); also reachable by the 100 cm crate | none; no route depends on it |

Clearance above every take-off is unlimited (no ceiling), so ADR 0006 section 3's 900 cm rule holds trivially.

### 5.5 Ramp slides

Clivus (12 m down into Hortus), Clivus Orientis (12 m down onto Semita, then a 90 degree turn onto SL1), RN and RS (down onto the hub bands). All use the kit's 15 degree ramp; `RampSlideMinNormalSpeed` / `RampExitMinAngle` apply unchanged.

## 6. Items

Definitions are the existing `/Game/Items/DA_Item_<Type>_<Name>` assets (`Tools/editor/create_item_content.py`). Respawn: health 35 s, armour 25 s, ammo 40 s, powerups 120 s (first appearance 30–60 s), weapons by game mode (5 s). MG is the spawn weapon. Mode gating uses the existing `DisabledInModes` container.

| Item | Location (x, y, z) | Area | Modes |
|---|---|---|---|
| Red Armor | (−4200, 300, 0) | Armamentarium, behind the cover block | both |
| Mega Health | (0, 3650, −320) | Hortus north edge | both |
| Yellow Armor (south) | (3700, −3000, 0) | Semita elbow | both |
| Yellow Armor (north) | (−2400, 3200, 160) | Scopulus | FFA only |
| 25 Health | (−2400, 3200, 160) | Scopulus | Duel only |
| Quad Damage | (0, 0, 640) | Specula | FFA only |
| Armor Shard × 3 | (−150, 100, 640), (150, 100, 640), (0, −100, 640) | Specula | Duel only |
| Rocket Launcher | (0, −3000, 0) | Horreum centre | both |
| Railgun | (3700, 1700, 320) | Trabs north tip | both |
| Lightning Gun | (−1200, 300, 0) | N0 west band, at the Pons Fractus mouth | both |
| Plasma Gun | (400, 1400, 0) | N0 north band, at the head of Clivus | both |
| Shotgun | (−3300, 0, 0) | Armamentarium, at the bridge mouth | both |
| Rockets | (1200, −2600, 0) | Horreum east | both |
| Slugs | (1300, 400, 0) | N0 east band, beside P1 | both |
| Lightning | (1300, −400, 0) | N0 east band | both |
| Cells | (−550, 3600, −320) | Hortus west (the PG user has to go down for ammo) | both |
| Shells | (−4200, −600, 0) | Armamentarium south-west | both |
| Bullets | (0, 550, 320) | Corona north band | both |
| 50 Health | (0, −550, 320) | Corona south band | both |
| 25 Health × 4 | (1300, −1000, 0), (−700, 1400, 0), (3700, 900, 320), (−1200, −3200, 0) | N0 south-east, N0 north-west, Trabs, Horreum west | both |
| 5 Health × 6 | (0, −1800 / −2000 / −2200, 0); (2400 / 2800 / 3200, −3000, 0) | Pons Australis; Semita | both |
| Armor Shard × 6 | three up RN (−300 / 0 / 300, 1000), three up RS (300 / 0 / −300, −1000) | hub ramps | both |

37 pickup actors are placed; after mode gating 33 remain in FFA and 35 in Duel.

**Timing and control logic.**

- *Duel:* three majors on three different decks: RA (west, 25 s), Mega (north, 35 s), YA (south-east, 25 s). The **armour orbit** RA → P2 → Scopulus → G2 → Mega takes about 6 s and is the strongest run on the map, but it contains a 1.57 s arc, a 12 m rock with no cover and a deck that is looked down on from two sides. The return Mega → Clivus → N0 → Pons Fractus → RA is about 54 m (6.5 s running, 4.5 s strafing), so RA and Mega cannot both be held on a 25/35 s clock without flying P2 every cycle. The counter-position is the **east side**: RL (Horreum), YA (Semita), RG (Trabs) with a 20–40 m view of P2, Scopulus, Hortus and both hub ramps. The stack holder must cross; the rail holder must leave the truss for slugs (hub east band) and for health.
- *FFA:* Quad on Specula needs P4 (launched from the Mega deck, 1.52 s in full view) or a rocket jump from Corona. Scopulus carries the second YA. With 4–6 players the hub is the brawl and the outer decks are short visits.
- No Haste, Regen or Battle Suit: Haste on long pads and narrow decks would need its own tuning pass.

## 7. Spawns

Seven `PlayerStart`s, each facing its deck centre, each with a weapon within 25 m of travel (DM1's 8 m rule does not fit decks this far apart), none within 10 m of another, none on a pad landing:

| Id | Location (x, y, z) | Yaw | Nearest weapon |
|---|---|---|---|
| S1 | (−1350, −1350, 0) | 45 | LG, 17 m |
| S2 | (1350, 1350, 0) | 225 | PG, 10 m |
| S3 | (1350, −1350, 0) | 135 | RL via Pons Australis, 24 m |
| S4 | (−3400, 650, 0) | 225 | SG, 7 m |
| S5 | (−1200, −3100, 0) | 0 | RL, 12 m |
| S6 | (1700, −3100, 0) | 180 | RL, 17 m |
| S7 | (3700, −700, 320) | 90 | RG, 24 m |

ADR 0006's rule "no spawn in line of sight of Mega or RA" cannot hold literally on an open map. Replacement rule for Orbita: no spawn within 12 m of a major item or on its deck (S4 is the stated exception: 9 m from the RA but on the far side of its cover block), and every hub spawn stands behind a corner pylon as seen from the Trabs node. No spawn on Hortus, Scopulus, Corona or Specula.

## 8. Loops and control flow

1. **Hub circuit** (walk only): N0 bands around Corona, with RN/RS as an over-the-top shortcut. 96 m lap; LG, PG, slugs, lightning, health. Whoever is out of control lives here.
2. **West orbit:** Horreum → P5 → Armamentarium (RA, SG) → Pons Fractus → N0 → Pons Australis → Horreum (RL). About 12 s; one pad, one gap.
3. **North orbit (the armour orbit):** Armamentarium → P2 → Scopulus → G2 → Hortus (Mega, cells) → Clivus (PG at its head) → N0 west band → Pons Fractus → Armamentarium. About 13 s; the Duel control run.
4. **East orbit:** N0 → P1 → Trabs (RG) → P6 → Hortus (Mega) → Clivus → N0; or Trabs → Clivus Orientis → Semita (YA) → Horreum (RL) → Pons Australis → N0. The rail player's two exits: one fast and blind, one slow and armoured.
5. **Crown line (FFA):** Hortus → P4 → Specula (Quad) → drop to Corona → any Corona launch (G3).

Flow: pads run counter-clockwise on the west and north (P5, P2, P3) and feed the hub; the east side is entered by P1 and left by P6 or on foot. The only gap-free walk between hub and outer decks is Pons Australis, so Horreum is the natural regroup point and the hub south band the natural ambush.

## 9. Sightlines, chokes, camping

- **Rail lanes (intended):** Trabs node ↔ N0 east band 16–26 m; Trabs tip ↔ Hortus 40 m, looking 640 down; Trabs ↔ Specula 37 m; Armamentarium ↔ N0 west band 16–24 m; Armamentarium ↔ Scopulus 26 m; Horreum ↔ Trabs south 20–40 m; Specula ↔ Armamentarium 42 m. Longest open line about 67 m (Armamentarium north-east corner to Trabs past the hub's north band): a 140 cm character is then about 1.2 degrees tall, roughly 19 px at 1080p. Acceptable only because the background is black.
- **Blocked on purpose:** everything through the hub centre (Corona 320, spindle 640, dish 940); RA from the hub (cover block); Specula from the south (dish).
- **Pad arcs are rail windows:** 1.2–1.6 s on a known curve. Air control lets the flier jink by a metre or two; a hit mid-arc usually means a miss of the landing and a void death. This is the map's signature duel and its main frustration risk.
- **Chokes:** Pons Australis (8 × 4 m), Pons Fractus (16 × 4 m with the gap), Clivus (12 × 4 m), Semita (19 × 4 m). Each is a ring-off trap for rockets and LG; each has a pad alternative except Semita (alternative: P1).
- **Camping risks and answers:** *Trabs north tip with RG* — 6 m wide, any splash pushes toward an edge, slugs are not on the truss, two approaches (P1 lands 17 m away; the ramp arrives at the far end behind parapets). *Specula* — no cover from three sides, 6 × 6 m, RJ access from two sides. *Horreum behind containers* — safe but holds no major; the YA walk is exposed. *Armamentarium behind the RA block* — P5 lands behind the block. If soaks or playtests show the Trabs tip dominating, the first fix is to move the RG to the node (3700, 0).

## 10. Void and hazards: rules

1. **Kill plane.** `WorldSettings.KillZ = −1400` (`veloxmap.set_kill_z`). A pawn below it dies through the existing `AVeloxCharacter::FellOutOfWorld` path: `EnvironmentKillDamage` (1 000), tag `Damage.Environment`, no armour, no knockback. Fall time from Z 0 is 1.18 s, from Hortus 1.04 s. No separate kill-volume actor is needed; lateral bounds are not needed because gravity brings everything to the plane.
2. **Credit to the last attacker (new server code, small).** Today the environment kill has no instigator. Needed: the health component records `LastEnemyDamageInstigator` and `LastEnemyDamageServerTime` on every damage application from another player; `FellOutOfWorld` passes that instigator into the damage spec when the hit is younger than `VoidKillCreditSeconds` (new field in `UVeloxCombatSettings`, proposed 5.0 s). Result: frag for the attacker, obituary "A knocked B into the void"; otherwise the existing environment death (suicide scoring as today). Self-knockback (own rocket) never credits anyone. This touches ADR 0003/0004 scoring and needs a short ADR amendment before implementation.
3. **Things that fall.** Projectiles end by lifetime; any dropped or knocked actor below `KillZ` is destroyed. Corpses of void deaths are not left on the map.
4. **No railings by rule.** The only solid edge protection is the listed parapets and backstops. Every deck edge carries an emissive edge strip (section 12) so the boundary reads against black.
5. **No other hazards:** no lava, no crushers, no moving platforms, no low-gravity zones, no teleporters.

## 11. Bots and navigation

ADR 0006 section 3 rejected a void map for DM1 because the bots do not reason about falling, and section 7 makes `left_arena 0` and `world_kill 0` hard invariants on any map. Orbita therefore needs bot work and an ADR amendment **before** its blockout can pass a soak. Required, in order:

1. **Nav.** One `NavMeshBoundsVolume` 9000 × 7600 × 1800 centred (−100, 200, 200). Recast erodes by `AgentRadius 45`, so path following already keeps bots 45 cm from every edge. Decks are solid 200 cm slabs (D41 holds: no thin walkable surface over a floor). Nav-lint gate `clusters = 0` as on DM1.
2. **Links.** 6 `JUMP_PAD` (ends at `ComputeLandingPoint`), 3 `JUMP` (G1 both ways, G2 one way), 11 `DROP` (D1, D2), 2 `ROCKET_JUMP` (RJ1). `veloxmap.drop_links` must **not** be called for outer edges: on this map a `DROP` link whose end does not project onto nav is a death link. New lint: every link end projects to nav within the D41 extent.
3. **Edge-aware combat movement (new, `dev-hard`).** The combat overlay (ADR 0005: lateral strafe amplitude 0.7, projectile dodge, retreat) is the only bot motion that ignores the path. Rule: before applying it, predict the position 0.25 s ahead from velocity plus the overlay wish; if that point does not project onto nav, drop the lateral component (or flip its sign if the other side projects). Bots never back-pedal toward an edge they cannot see nav behind.
4. **Airborne recovery (extend D17).** A bot airborne from knockback steers with full air control toward the nearest nav point at or below its current height, not toward its path segment.
5. **Pad discipline.** While on a pad arc a bot gives no movement input until `until: ground` (the arcs are deterministic only then); combat aim stays active.
6. **Soak invariants (amendment needed).** On maps tagged as void maps, `world_kill` is split: `void_kill_credited` (gameplay, unbounded) and `void_suicide` (no enemy hit inside the credit window). `void_suicide` and `left_arena` run report-only for three sets, then get bands by the E3 method. All other map-independent invariants stay hard, including item coverage (Quad and Scopulus must be taken by bots).
7. **Deferred:** bots choosing knockback weapons because the enemy is near an edge; bots railing pad arcs with lead on the known curve (the aim model's normal lead applies).

## 12. Readability, palette, lighting

- **Background:** sky is black with a sparse starfield; stars are points with V <= 0.6 covering under 0.5 % of the sky, no nebulae, no coloured clouds. The planet fills the lower south-west quarter of the cubemap (visible below the decks, where falling players are watched): day side V <= 0.40, S <= 0.30, dusty teal-grey, no hue from the skin set at skin brightness.
- **Surfaces:** kit rule unchanged (V <= 0.5, S <= 0.25). Deck tops light grey-white hull plate (V 0.45), sides and undersides dark (V 0.15), so a deck reads as a bright slab with a dark rim. Identity colours appear only as small emissive fixtures and trim: Nucleus white, Armamentarium red, Hortus green, Horreum amber, Trabs cyan, Scopulus none (bare rock, V 0.3).
- **Edges:** a 10 cm emissive amber strip along every void edge (emissive is exempt from the palette rule like `MI_Kit_Light`). Pad rings pulse in the existing pad green.
- **Skins:** bright skins on sunlit pale decks are the lowest-contrast case; the deck V cap of 0.45 against skin V >= 0.60 with S >= 0.70 keeps the margin. Against the sky the contrast is maximal.
- **Lighting (all static, baked; no Stationary lights):** one static directional sun, high from the north-east (pitch −50), white, hard shadows from the spindle, dish, containers and pylons; one weak static directional fill from below-south-west (planetshine, blue-grey, about 8 % of the sun) so undersides and shadowed players are not pure black; a static `SkyLight` with `TC_Sky_Orbita` at low intensity; about 26 static point lights (6 pads, 12 deck identity fixtures, 8 item beacons for RA, Mega, Quad, RG, RL, both YA and the LG). 2 bounces are enough (no enclosed rooms). 6 sphere reflection captures at 128 px, one per deck.
- **Volumetric lightmap** (characters and projectiles): importance volume X −4600..4400, Y −3600..4000, Z −1400..1200 at the 200 cm cell, so players on pad arcs and players falling stay lit.
- **Skybox:** one unlit sky sphere with `TC_Sky_Orbita` (512 px faces; stars and planet painted into the cubemap by script, so the planet costs no extra draw call). Far backdrop: about 10 unlit, lightmap-free instances of two silhouette meshes (ring arc, solar wing) beyond 150 m, never reachable.
- **Bake:** path B on the Mac (ADR 0006 E0.1). No walls or ceilings: lightmapped area is about a third of DM1's.

## 13. Kit

Reused from the DM1 kit (`docs/art/kit-spec.md`) with a map-specific set of 8 material instances `MI_Orb_*` of `M_Kit_Master`: `Floor_400`, `Ramp_15`, `Bridge_400`, `Ledge_Edge`, `Railing_Stub`, `Pad_Ring`, `Pedestal`, `Light_Fixture`, `Pillar` (pylons), `Wall_400` (Corona and spindle faces, scaled in Z), `Wall_Half`, `Skirting_400` — 12 pieces.

New pieces (11; total 23 of the 25 cap), all <= 600 triangles, one material slot, `NoCollision`, collision on clip boxes as on DM1:

| Piece | Size (cm) | Purpose |
|---|---|---|
| `SM_Kit_Deck_800` | 800 × 800 × 20 | large deck tile: a quarter of the instances of `Floor_400` (the instance cap in section 14 depends on it) |
| `SM_Kit_Edge_Glow_800` | 800 × 10 × 10 | emissive void-edge strip |
| `SM_Kit_Parapet_400` | 400 × 20 × 120 | solid low cover |
| `SM_Kit_Truss_800` | 800 × 100 × 180 | girder under bridges, Semita and Trabs (visual only, hangs below deck level) |
| `SM_Kit_Keel` | 400 × 400 × 600 | tapered under-structure hanging below each deck |
| `SM_Kit_Container_400` | 400 × 200 × 200 | cargo container (K1 scaled to 160 high), crate at scale 0.5 |
| `SM_Kit_Dish` | 600 × 50 × 300 | Specula antenna dish (landmark, backstop) |
| `SM_Kit_Rock_A`, `SM_Kit_Rock_B` | about 1200 × 1200 × 500, 400 × 400 × 300 | Scopulus body and outcrop |
| `SM_Kit_Grow_Rack` | 400 × 100 × 200 | Hortus landmark, green emissive strip |
| `SM_Kit_Backdrop_Wing` | 2400 × 800 × 40 | solar wing: Trabs dressing and far silhouette (the far ring arc is this piece instanced and rotated) |

Also new: `TC_Sky_Orbita` (generated by script like `TC_Sky_DM1`), `MI_Orb_*`, one audio cue for a void fall. Generator: `Tools/editor/maps/orbita_layout.py` plus a map script on `veloxmap.py`; preferably `create_dm1_map.py` is generalised to take a layout module rather than copied.

## 14. Performance estimate versus budget (ADR 0006 section 8)

There is no occlusion on this map: from Specula everything is in view, so the totals are the worst case.

| Figure | Estimate | Budget |
|---|---|---|
| Kit instances (all visible) | about 290; **design cap 300** | — |
| Distinct kit meshes / materials | 23 / 8 | 25 / 8 |
| Draw calls, worst view | 290 kit without any batching, expected 80–150 with dynamic instancing; plus about 40 pickups, 6 pads, 6 players with weapons, projectiles, sky 1: **about 200 expected, about 360 upper bound** | <= 600 |
| Triangles in view | kit about 15 k, rocks and backdrop about 4 k, 6 players 36 k, weapons and pickups about 12 k: **about 70 k** | <= 300 k |
| Lightmaps | about 70 deck tiles at 128, the rest at 32–64: **about 12 MB** | <= 48 MB |
| Components with lightmap res > 64 | about 90 | <= 120 |
| Translucency | none (edge strips and pad rings are opaque emissive) | LG beam and pickup glow only |

How `audit_map.py` counts draw calls per pass must be confirmed before the cap of 300 instances is final.

## 15. Acceptance ideas for the blockout

- **A1** `FT_Orbita_Reachability`: `FindPath` (Full filter) from every `PlayerStart` to every pickup and between all majors; every `JUMP_PAD` link end within 60 cm of `ComputeLandingPoint`; every link end projects onto nav.
- **A2** Nav lint `clusters = 0`; nav build <= 2 s.
- **A3** `Content/Data/MoveScripts/OrbitaRun.json`: all six pads twice, once with no input and once with forward held for the whole flight; the pawn must come to rest inside the landing deck's check box both times; strays <= 2.5 cm in the no-input case. Records the real run-out distances and replaces the estimates in 5.1.
- **A4** Gap and jump scripts: G1 both ways at run speed, G2, the RJ1 link; RJ3 measured report-only.
- **A5** Void-kill functional test: rocket a dummy off Trabs → frag credited; walk off → suicide; hit, wait longer than the window, walk off → suicide; own rocket → suicide.
- **A6** Two clients on a dedicated server with 100 ms and 2 % loss: P4, P5 and P6 (the fastest a pawn ever moves on a Velox map) show no visible correction; correction count from the movement HUD recorded.
- **A7** Soaks report-only: BotDuel × 3, FFA × 10 with 4 bots; item coverage complete; `void_suicide` per bot per 10 min reported; link traversal success >= 90 % per link type.
- **A8** `audit_map.py`: instance cap, lightmap MB, palette assertion on `MI_Orb_*`, luminance statistics of `TC_Sky_Orbita`.

## 16. Risks and open questions

Risks, most serious first:

1. **Bots on a void map.** Sections 10 and 11 need new bot code (edge-aware overlay, airborne recovery) and a change to two hard soak invariants. Until that exists the map cannot pass the project's own gates. Estimated three `dev-hard` tasks plus one ADR amendment.
2. **Pad run-out and mid-arc air control.** Landing speeds of 1 100–2 300 cm/s on decks 6–16 m deep are computed, not measured. If run-out is longer than estimated, landing zones grow or more backstops appear, which changes the look and the sightlines.
3. **Prediction at pad speeds.** 3 000 cm/s launches are well above anything DM1 exercises; a correction during a 35 m arc ends in the void.
4. **Frustration.** Deaths without a fight (overshooting Corona, being hit mid-arc) are part of the genre and still the main reason people leave such maps. Orbita should not be the default public server map.
5. **Spawn exposure.** No walls means spawns are visible from somewhere; the pylon rule reduces it, it does not remove it. There is no spawn protection in Velox.
6. **Draw-call accounting** on a map with zero occlusion (section 14).

Open questions for the human:

- **Q1 Gravity.** Recommended: normal gravity everywhere. Low gravity would change jump height, strafe timing, pad arcs and every bot tuning value on one map only. Confirm.
- **Q2 Void-kill credit.** Is a 5 s window right, and does an uncredited void death cost a point (suicide, as environment deaths do today)?
- **Q3 Railings.** None by design. Should the Duel version get more parapets on Trabs and Semita, or is the exposure the point?
- **Q4 Mode priority.** Duel first with FFA as a variant (as written), or FFA first with a larger hub?
- **Q5 Second armour in Duel.** Currently one RA and one YA plus Mega; Scopulus holds only 25 Health in Duel. Move the YA north onto Scopulus instead of Semita?
