# Culmen — map concept (theme 2 "Futuristic")

**Display name:** Culmen (Latin: rooftop, summit) · **Slug:** `culmen` · **Working map id:** `DM2F` (`L_Culmen_Blockout` / `L_Culmen`)
**Status:** concept, docs only (M7 "two more maps", `docs/maps/BRIEF.md` theme 2). Nothing here is built or measured; every number is derived from the movement asset defaults, the pad formula and the DM1 rules, and is marked where it must be verified on a blockout.

## Pitch (20 seconds)

1. Night-time rooftops of an arcology: two tower roofs, a central holo spire, a maglev trench and an elevated viaduct, all open to a skyline.
2. FFA for 4–8. Three height layers (0 / +320 / +640 cm), 76 × 46 m, no void: falling off anything costs height, never a life.
3. Speed is the theme: a 70 m maglev strafe lane that ends on a pad at both ends, a 60 m viaduct lane with Haste in the middle, four 20° ski-jump ramps.
4. Eleven pads form chains: roof to spire to roof in two hops, Red Armor to Mega in about 5.5 s without touching the street.
5. Dark graphite city, thin violet and ice-white neon above head height; the six bright skins stay the most saturated things on screen.

---

## 1. Theme, mood and why it fits Velox

A clean megastructure at night. The play space is the top of an arcology: graphite decks, smoked-glass bridges and parapets, flush maglev rails inlaid in the floors, a hologram spire in the middle and large holo signs high above the towers. Beyond the parapets a low-poly skyline card ring and a dusk-to-night sky. The mood is cold, quiet and fast, a place built for vehicles that players borrow for strafe runs.

Why it fits: the non-negotiable order is movement, then frame rate and readability. A rooftop megastructure justifies exactly what Velox movement wants (very long straight lanes, big height steps bridged by pads, wide ramps) with almost no geometry: the kit pieces are boxes, slabs and strips, so the triangle count is trivial and all lighting is baked. A dark, desaturated city is the best possible backdrop for skins at V 1.0 / S ≥ 0.76, provided the neon is rationed (section 11). It is deliberately the opposite of DM1 Praesidium (enclosed, two atriums, three pads): open sky, long lines, pad chains.

It is **not** a space map and has **no lethal fall**. Theme 3 owns the void; Culmen keeps ADR 0006's "no out-of-arena ever" so the hard soak invariants (`left_arena 0`, `world_kill 0`) hold unchanged and the bots need no edge reasoning.

## 2. Mode and player count

- **FFA, 4–8 players** (sweet spot 6). `DA_GameMode_FFA` unchanged.
- **Not a Duel map**: the 60–76 m lanes and two powerups make 1v1 a chase. Duel stays loadable (powerups gated off as on DM1), but no Duel bands are planned.
- CTF is out of scope, but the layout is point-symmetric (180° about the origin) on purpose, so a later CTF variant with flags on the two roofs is possible without a redesign.

## 3. Size and height layers

- Footprint **7600 × 4600 cm** (X −3800..3800, Y −2400..2200), 100 cm grid, 400 cm module. DM1 is 64 × 44 m for 4–6; Culmen is about 25 % larger for 8.
- `FLOOR_Z = 0` (ADR 0006 E0.3 e), `KILL_Z = −1000` as a safety net only, perimeter clip to `CLIP_H = 1800`. Highest pad apex (feet) is 1098 cm, highest rocket-jump apex from a roof is ≤ 640 + 880 = 1520 cm, both below the clip roof. There is no ceiling anywhere except under the gate lintels (320 cm) and under the glass bridges (280 cm clear, D41-conform: ≥ 160 cm clear and ≥ 170 cm wide).

| Layer | Z (floor top) | What is on it |
|---|---|---|
| L0 "street" | 0 | Maglev Trench, Forum, South Concourse |
| L1 "decks" | +320 | West/East Terrace, two End Terraces, Spire Deck, four glass bridges, Viaduct, maglev car roof |
| L2 "roofs" | +640 | West Roof (Red Armor), East Roof (Mega) |

## 4. Top-down layout

One character = 100 × 100 cm. X grows east (right), Y grows north (up). Row labels are the Y of the row's top edge; the bottom edge of the map is Y −2400.

```
                  -3000     -2000     -1000     0         1000      2000      3000
                  |         |         |         |         |         |         |
  2200    ================================cccccccccccc================================
          ================================cccccccccccc================================
          ============S===================cc^cccGccccc=====^==========================
          ================================cccccccccccc================================
  1800    ====================Y=======================================================
          =^======================================================================^===
          ============================================================================
          ============================================================================
  1400    rrr/////////tttttt####....########################....####tttttttttttttttttt
          rrr/////////tttttt####....########################....####tttttttttttttttttt
          rrr/////////tttttt........................................tttttttttttttttttt
          rrr/////////tttttt.....................................xxxttttttttttttSttttt
  1000    rrr/////////tttttt.....................................xxxtttttttttttttttttt
          rrr/////////tttttt....................Q................xxxtttttttttttttttttt
          rrrrrrrrrrrrtttttt.....................................xxxttttttrrrrrrrrrrrr
          rrrrrrrrrrrrtttttt.....................................xxxttttttrrrrrrrrrrrr
   600    rrrrrrrrrr^rtttttt..............ssssssssssss...........xxxttttttrrrrrrrrrrrr
          rrrrrrrrrrrrtttttt..............ssssssssssss...........xxxttttttrrrrrrrrrrrr
          rrrrrrrrrrrrtttttt..............sssssssss^ss...........xxxttttttrrrrrrrMrrrr
          rrrrrrrrrrrrtttttt..............ssssssssssss...........xxxttttttrrrrrrrrrrrr
   200    rrrrrrrrrrrrtttttt..............ssssssssssss..............ttttttrrrrrrrrrrrr
          rrrrrrrrrrrrttttttbbbbbbbbbbbbbbsssss@@sssssbbbbbbbbbbbbbbttttttrrrrrrrrrrrr
          rrrrrrrrrrrrttttttbbbbbbbbbbbbbbsssss@@sssssbbbbbbbbbbbbbbttttttrrrrrrrrrrrr
          rrrrrrrrrrrrtttttt..............ssssssssssss..............ttttttrrrrrrrrrrrr
  -200    rrrrrrrrrrrrttttttxxx...........ssssssssssss..............ttttttrrrrrrrrrrrr
          rrrrrArrrrrrttttttxxx...........ss^sssssssss..............ttttttrrrrrrrrrrrr
          rrrrrrrrrrrrttttttxxx...........ssssssssssss..............ttttttrrrrrrrrrrrr
          rrrrrrrrrrrrttttttxxx...........ssssssssssss..............ttttttr^rrrrrrrrrr
  -600    rrrrrrrrrrrrttttttxxx.....................................ttttttrrrrrrrrrrrr
          rrrrrrrrrrrrttttttxxx.....................................ttttttrrrrrrrrrrrr
          ttttttttttttttttttxxx.....................................tttttt/////////rrr
          ttttttttttttttttttxxx.................R...................tttttt/////////rrr
 -1000    ttttttttttttttttttxxx.....................................tttttt/////////rrr
          ttttttPttttttttttt........................................tttttt/////////rrr
          tttttttttttttttttt##################....##################tttttt/////////rrr
          tttttttttttttttttt##################....##################tttttt/////////rrr
 -1400    -------------bbb----------kk----------------------------kk--bbb-------------
          -------------bbb----------kk----------------------------kk--bbb-------------
          ----^--------bbb----------------------^---------------------bbb-P-----------
          -------------bbb--kk----------------------------kk----------bbb-----------^-
 -1800    -------------bbb--kk----------------------------kk----------bbb-------------
          --------vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv--------
          --------vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv--------
          --------vvvvvvvvvvvvLvvvvvvvvvvvvvvvvvHvvvvvvvvvvvvvvvvvYvvvvvvvvvvv--------
 -2200    --------vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv--------
          --------vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv--------
```

Legend — areas: `=` Maglev Trench (L0) · `c` maglev car roof (L1) · `.` Forum (L0) · `-` South Concourse (L0) · `t` terraces (L1) · `r` roofs (L2) · `/` roof ramps 20° (L1↔L2) · `x` Clivus ramps 20° (L0↔L1) · `b` glass bridges (L1, walkable underneath) · `s` Spire Deck (L1) · `@` spire core (solid, to Z 1000) · `v` Viaduct (L1) · `#` Porticus walls (ridge 480, not walkable) · `k` kiosks (cover, not walkable).
Items: `A` Red Armor · `M` Mega · `Q` Quad · `H` Haste · `Y` Yellow Armor · `R` RL · `G` RG · `L` LG · `P` PG · `S` SG · `^` jump pad.

**Solid masses (the layout table's boxes; everything else is air):**

| Id | X range | Y range | Top Z | Note |
|---|---|---|---|---|
| `TowerW` / `TowerE` | −3800..−2600 / 2600..3800 | −800..800 | 640 | the two roofs |
| `TerraceW` / `TerraceE` | −2600..−2000 / 2000..2600 | −1400..1400 | 320 | solid podium, 6 m deep, 28 m long |
| `EndTerrSW` / `EndTerrNE` | −3800..−2600 / 2600..3800 | −1400..−800 / 800..1400 | 320 | PG-W / SG-E, rocket-jump spots |
| `RoofRampNW` / `RoofRampSE` | −3480..−2600 / 2600..3480 | 800..1400 / −1400..−800 | 320→640 | 20°, 880 run, 600 wide, sealed fill below |
| `RampHeadNW` / `RampHeadSE` | −3800..−3480 / 3480..3800 | 800..1400 / −1400..−800 | 640 | 320 × 600 landing, flush with the roof |
| `Spire` | −600..600 | −600..600 | 320 | deck; core ±100 to Z 1000 |
| `ClivusSW` / `ClivusNE` | −2000..−1700 / 1700..2000 | −1100..−220 / 220..1100 | 0→320 | 20°, 300 wide, head landing to Y ∓150 joins terrace and bridge |
| `PorticusN` | −2000..2000 | 1200..1400 | 360 + 50° ridge to 480 | gates 400 wide × 320 high at X −1600..−1200 and 1200..1600 |
| `PorticusS` | −2000..2000 | −1400..−1200 | 360 + ridge to 480 | one gate 400 × 320 at X −200..200 |
| `Viaduct` | −3000..3000 | −2400..−1900 | 320 | solid plinth, free north edge and free ends |
| `Car` | −600..600 | 1800..2200 | 320 | parked maglev car, RG on its roof |
| `Kiosk1..4` | −2000..−1800, −1200..−1000, 1000..1200, 1800..2000 | alternating −1900..−1700 / −1600..−1400 | 200 + ridge | slalom cover in the concourse |
| Glass bridges | `BrW` −2000..−600, `BrE` 600..2000 (Y −150..150); `BrSW` −2500..−2200, `BrSE` 2200..2500 (Y −1900..−1400) | | 320 (deck 40 thick) | parapet stubs 110 on the long sides of `BrW`/`BrE` only |

## 5. Areas

**Maglev Trench (L0, X −3800..3800, Y 1400..2200, 76 × 8 m).** The signature lane. The south half (Y 1400..1800) is a clear 400 cm wide strip with no step from X −3640 to X 3400; the rails are flush floor inlays with no collision of their own (clip-first). The parked car fills the north half for 12 m at the middle and is the only cover. South wall: tower faces, terrace faces (free L1 edges above, drops into the trench) and the Porticus with two gates. North: glass parapet and the skyline. West end: pad P1 to the Red Armor roof. East end: pad P11 to the SG terrace. Sightline: the full 76 m, deliberately; see section 10.

**Forum (L0, X −2000..2000, Y −1200..1200 minus the Spire).** A ring plaza, 11–14 m wide west and east of the Spire, 6 m wide north and south of it. Three street exits (two north gates, one south gate), two Clivus ramps up to the terraces. Overlooked by everything: both terraces, both glass bridges, the Spire Deck and, from 640, both roofs. Quad sits in the 6 m canyon north of the Spire, RL in the canyon south of it.

**Spire Deck (L1, 12 × 12 m, core 2 × 2 m).** The pad hub: two arrivals (from the roofs), two departures (to the roofs), two glass bridges. The core blocks the 40 m east–west line through the deck and carries the main hologram (starts at Z 700, visual only). No items on the deck except a 25 HP: it is a crossing, not a home.

**West / East Terrace (L1, 6 × 28 m each).** Balconies over the Forum with free inner edges (drop 320) and free north and south ends (drops into Trench and Concourse). Each connects to: its Clivus ramp, its Spire bridge, its roof ramp or End Terrace, the Viaduct bridge. Parapet stubs only at the bridge heads, otherwise open edges for drops.

**End Terraces (L1, 12 × 6 m).** SW under the West Roof (PG-W), NE under the East Roof (SG-E). The roof wall above them is exactly 320 cm: the bot-capable rocket-jump spots to the roofs. The south edge of `EndTerrSW` faces the Viaduct across a 500 cm gap (section 8).

**Roof ramps (20°, 6 m wide, 8.8 m long).** The only walkable way to L2. Going down they are ski jumps: off the ramp, across 6 m of terrace, off the free edge into the Forum or the Trench.

**West Roof (L2, 12 × 16 m + ramp head): Red Armor. East Roof: Mega.** Each roof has one arrival from the Spire, one arrival from the street pad (onto the ramp head), one departure pad, a 120 cm glass parapet on the inner (Forum) edge with 400 cm gaps on the two pad lines, and one solid holo-billboard wall across the middle (West: X −3600..−2800 at Y 0, 20 thick, 300 high; East mirrored) that splits the roof into an arrival half and an item half. No health on the roofs.

**Viaduct (L1, 60 × 5 m).** The second lane, elevated: LG west, Haste centre, Yellow Armor east. Free north edge over the Concourse along its whole length, free ends dropping into the two pockets. South side: parapet and skyline.

**South Concourse (L0, X −3800..3800, Y −1900..−1400, plus two pockets X ±3000..±3800, Y −2400..−1400).** A 5 m street under the Viaduct edge with four kiosks alternating north/south: a slalom, the bunny-hop line of the map. West pocket: 50 HP and pad P10 to the PG terrace. East pocket: PG-E and pad P2 to the Mega roof.

## 6. Items

Names are the existing definitions (`/Game/Items/DA_Item_<Type>_<Name>`, `Tools/editor/create_item_content.py`). Respawn: health 35 s (Mega included), armor 25 s, ammo 40 s, weapons 5 s (`DA_GameMode_FFA.weapon_respawn_seconds`), powerups 120 s with the first appearance 30–60 s after the start. Z is the floor the pickup stands on.

| Item (definition) | Id | Position (x, y, floor z) | Area | Mode |
|---|---|---|---|---|
| `Armor_Red` | RA | (−3300, −350, 640) | West Roof, south half | all |
| `Health_Mega` | MH | (3300, 350, 640) | East Roof, north half | all |
| `Armor_Yellow` | YA-N | (−1800, 1800, 0) | Trench, west of the car | all |
| `Armor_Yellow` | YA-S | (1800, −2150, 320) | Viaduct east | all |
| `Powerup_Quad` | Quad | (0, 900, 0) | Forum north canyon | FFA only (`DisabledInModes: Velox.Mode.Duel`, as on DM1) |
| `Powerup_Haste` | Haste | (0, −2150, 320) | Viaduct centre | FFA only (needs the same tag on the Haste definition) |
| `Weapon_RocketLauncher` | RL | (0, −900, 0) | Forum south canyon | all |
| `Weapon_Railgun` | RG | (0, 2000, 320) | car roof | all |
| `Weapon_LightningGun` | LG | (−1800, −2150, 320) | Viaduct west | all |
| `Weapon_PlasmaGun` | PG-W | (−3200, −1100, 320) | End Terrace SW | all |
| `Weapon_PlasmaGun` | PG-E | (2600, −1650, 0) | Concourse east | all |
| `Weapon_Shotgun` | SG-E | (3200, 1100, 320) | End Terrace NE | all |
| `Weapon_Shotgun` | SG-W | (−2600, 2000, 0) | Trench west | all |
| `Ammo_Rockets` | | (900, −900, 0) | Forum south-east | 9 m from RL |
| `Ammo_Slugs` | | (−1000, 1600, 0) | Trench lane | 10.8 m from RG |
| `Ammo_Lightning` | | (−900, −2150, 320) | Viaduct | 9 m from LG |
| `Ammo_Cells` ×2 | | (−2300, −1000, 320), (1700, −1750, 0) | Terrace W, Concourse | 9 m from each PG |
| `Ammo_Shells` ×2 | | (2300, 1000, 320), (−3400, 2000, 0) | Terrace E, Trench west | 8–9 m from each SG |
| `Ammo_Bullets` | | (1500, 900, 0) | Forum north-east | |
| `Health_Large` (50) ×2 | | (−3400, −2100, 0), (3600, 2000, 0) | west pocket, Trench east end | the two "far corners" |
| `Health_Medium` (25) ×7 | | (−1300, 400, 0), (1300, −400, 0), (−3000, 1600, 0), (−2800, −1650, 0), (−2300, −600, 320), (2300, 600, 320), (0, −400, 320) | Forum W/E, Trench, Concourse, both terraces, Spire Deck | |
| `Health_Small` (5) ×8 | | X 1400, 1800, 2200, 2600 at (Y 1600, Z 0); X 300, 500, 700, 900 at (Y −1650, Z 0) | Trench lane line, Concourse line | pickup lines that reward keeping speed |
| `Armor_Shard` ×8 | | four up each roof ramp centre line: X −2800, −3000, −3200, −3400 at Y 1100; X 2800..3400 at Y −1100 (Z on the ramp surface) | roof ramps | the "climb" trail to RA / Mega |

Totals: 1 RA, 1 Mega, 2 YA, 8 shards, 2 × 50, 7 × 25, 8 × 5, 7 weapon pickups (MG is the spawn weapon), 8 ammo, 2 powerups = **46 pickups** (DM1: 35). No Regen, no Battle Suit.

**Timing rhythm and control logic.**
- **RA (25 s) west roof, Mega (35 s) east roof**, 66 m apart as the crow flies, 5.5 s apart by the pad chain (section 8; loop A in section 9). One player can hold both only by committing to the roof line, which is the most exposed line on the map (two 1.2–1.6 s ballistic flights, visible from everywhere, and the RG is 20 m away on the car roof). In FFA that is the intended deal: the best stack is available, and everybody can see who is taking it.
- **YA-N and YA-S are in the two lanes**, on the street side of the map, each 25–30 m from the nearest roof item. A player who avoids the roofs gets 50 armor every 25 s per lane and the lane weapons; the "low loop" (section 9, loop B) is a complete alternative economy.
- **Quad (Forum north) and Haste (Viaduct centre)** spawn on the same 120 s clock but 30 m apart with the Porticus between them: one player cannot take both within the first seconds unless he times the Forum south gate route (≈ 4 s strafing). Quad is at the bottom of the bowl (every deck looks down on it); Haste is on the lane where it matters most (Haste scales speed by 1.3: the Viaduct is 60 m of it). The announcer cues for powerups already exist.
- **RG on the car roof** is low, central and exposed from both terraces' north ends. It is not on a roof on purpose: the rail positions (roofs, section 10) do not come with the rail.
- **RL at the Forum bottom**, the nearest weapon to four spawns' routes; rockets are also the key to the five rocket-jump shortcuts.

## 7. Spawn points

Ten `PlayerStart`s. Rules as DM1 (validated by the generator): ≥ 10 m apart, a weapon ≤ 8 m away (horizontal), no line of sight to RA or Mega (both lie on roofs 320–640 cm above every spawn; the roof edge blocks the line), yaw into the open space.

| Id | Position | Yaw faces | Nearest weapon |
|---|---|---|---|
| S1 | (−2300, 1600, 0) | east, down the lane | SG-W 5.0 m |
| S2 | (−600, 1600, 0) | east | RG 7.2 m |
| S3 | (600, 1600, 0) | west | RG 7.2 m |
| S4 | (2900, 1800, 0) | west | SG-E 7.6 m |
| S5 | (700, −1050, 0) | north-west, to the RL | RL 7.2 m |
| S6 | (−1400, −1650, 0) | east | LG 6.4 m |
| S7 | (3000, −1750, 0) | west | PG-E 4.1 m |
| S8 | (−3300, −1250, 320) | east | PG-W 1.8 m |
| S9 | (−2500, −2150, 320) | east, down the Viaduct | LG 7.0 m |
| S10 | (2300, −1000, 320) | north, along the terrace | PG-E 7.1 m |

Seven of ten spawns are on L0 or in the lanes: a fresh spawn starts low and fast, with a weapon, and has to climb to the stack.

## 8. Movement routes

**Movement facts used** (`UVeloxMovementSettings` defaults = `DA_MovementSettings_Default`, to be re-read from the asset by the blockout task): `MaxGroundSpeed` 800 cm/s, `JumpVelocity` 675, `GravityZ` −2000 → jump apex 114 cm, air time 0.675 s on the level, 0.996 s with a 320 drop, 1.206 s with a 640 drop; `StepHeight` 45; `WalkableFloorAngle` 45.5°. One hop covers 540 cm at 800 cm/s, 756 cm at 1120 cm/s (the 1.4× the `StrafeRun` soak demands within its 51 m corridor). Rocket-jump apex ≤ 880 cm above take-off in theory (1875 cm/s, task 07f); bots have proven `ROCKET_JUMP` links at 320–350 cm (`L_ItemTest`, DM1). Bot gap links: `JUMP` ≤ 500, `JUMP_FAST` ≤ 700, none beyond 900 cm.

### Strafe lanes and hop lines

| Id | Line | Clear length × width | Run-up and exits |
|---|---|---|---|
| L-N "Via Magnetica" | Trench south strip, Y 1400..1800, Z 0 | **7040 × 400 cm** (pad P1 at X −3640 to pad P11 at X 3400), no step, no door | Entered from either north gate at about a third of its length, from spawns S1–S4, or by dropping off a terrace north end. Running west ends on P1 (to the RA roof), running east ends on P11 (to the SG terrace and the Mega rocket jump): the lane converts speed into height at both ends. |
| L-S "Via Alta" | Viaduct, Y −2400..−1900, Z 320 | **6000 × 500 cm**, three pickups on the centre line | Entered from either glass bridge (X ±2350), pad P9 at the centre, the 500 cm gap from End Terrace SW, or a rocket jump from the Concourse. Running off either end is a 320 drop with full speed into a pocket; the east pocket has P2 (Mega roof) 6 m ahead. |
| L-C | Concourse slalom, Y −1900..−1400, Z 0 | 7600 long, 300 cm beside each kiosk, 1000–3000 cm between kiosks | Bunny-hop line: four direction changes at kiosk pitch; the 5 HP row sits on the racing line between P9 and Kiosk3. |
| L-T | Terrace W / E, Z 320 | 2800 × 600 cm each | Run-up for the Forum-side drops and for the 500 cm Viaduct gap from the terrace south end. |
| L-B | Bridge line: Terrace W → `BrW` → Spire Deck → `BrE` → Terrace E, Z 320 | 4600 cm, 300 wide on the bridges; must sidestep the core | The risky fast crossing at L1: no cover, visible from both roofs. |

### Jump pads

All pads are `AimAtTarget` with `override_velocity` (as `veloxmap.jump_pad`), launch origin = trigger centre 70 cm above the floor, validation gravity −2000, `PAD_MAX_LAUNCH_SPEED` 4000. Values below are computed with `VeloxMoveSim::JumpPadLaunchVelocity`'s formulas (upward pads: apex at the target; the two downward pads: minimum-speed arc to the target). "Apex" is the feet height above Z 0. Every landing zone is ≥ 300 × 300 cm of plain floor. P2, P4, P6, P11 are the point mirrors (x → −x, y → −y) of P1, P3, P5, P10.

| Pad | Pad (x, y, floor z) | Target point (x, y, z) | Landing (x, y, floor z) | Apex | Launch speed | Flight | Tightest clearance (feet) |
|---|---|---|---|---|---|---|---|
| P1 Trench west → West Roof | (−3640, 1700, 0) | (−3640, 1245, 970) | (−3640, 1000, 640) ramp head | 900 | 1957 | 1.46 s | +94 over the ramp-head edge (Y 1400, 640) |
| P2 Concourse east → East Roof | (3640, −1700, 0) | (3640, −1245, 970) | (3640, −1000, 640) | 900 | 1957 | 1.46 s | as P1 |
| P3 Spire → West Roof | (−350, −350, 320) | (−1924, −412, 910) | (−2900, −450, 640) | 840 | 2617 | 1.17 s | +115 over the roof edge (X −2600, 640); needs the 400 cm parapet gap at Y −650..−250 |
| P4 Spire → East Roof | (350, 350, 320) | (1924, 412, 910) | (2900, 450, 640) | 840 | 2617 | 1.17 s | as P3 |
| P5 West Roof → Spire | (−2750, 550, 640) | (−350, 350, 390) | (−350, 350, 320) | 1098 | 2054 | 1.56 s | +122 over the roof edge; parapet gap at Y 350..750 |
| P6 East Roof → Spire | (2750, −550, 640) | (350, −350, 390) | (350, −350, 320) | 1098 | 2054 | 1.56 s | as P5 |
| P7 Trench → car roof | (1100, 2000, 0) | (596, 2000, 490) | (350, 2000, 320) | 420 | 1512 | 0.96 s | +97 over the car edge |
| P8 car roof → Terrace W | (−350, 2000, 320) | (−1325, 1550, 770) | (−2300, 1100, 320) | 700 | 2134 | 1.23 s | +103 over the Porticus ridge (480) |
| P9 Concourse → Viaduct | (0, −1650, 0) | (0, −1986, 490) | (0, −2150, 320) at Haste | 420 | 1396 | 0.96 s | +72 over the Viaduct edge |
| P10 west pocket → End Terrace SW | (−3400, −1650, 0) | (−3266, −1247, 490) | (−3200, −1050, 320) at PG-W | 420 | 1453 | 0.96 s | +39 over the terrace edge: raise the apex 40 cm in the layout if the capsule clips |
| P11 Trench east → End Terrace NE | (3400, 1650, 0) | (3266, 1247, 490) | (3200, 1050, 320) at SG-E | 420 | 1453 | 0.96 s | as P10 |

**Pad chains** (3–7 m of floor between a landing and the next pad; no landing is inside a pad trigger, so every hop is one clean `JUMP_PAD` link for the bots):
- **Roof to roof:** P5 → Spire NW → 7 m east → P4 → East Roof, and P6 → Spire SE → 7 m west → P3 → West Roof. RA → Mega: 10.5 m run, 1.56 s flight, 7 m, 1.17 s flight, 4 m ≈ **5.4 s at ground speed**.
- **Street to roof to roof:** L-N west → P1 → West Roof → P5 → Spire → P4 → East Roof (three pads from Z 0 to the Mega).
- **Trench to terraces:** P7 → car roof (RG) → 7 m west → P8 → Terrace W, landing at the foot of the NW roof ramp.

### Rocket and plasma jumps

| Id | From → to | Rise needed | vs Velox | Bot link |
|---|---|---|---|---|
| RJ1 | End Terrace SW (−3200, −1250, 320) → West Roof (−3200, −600, 640), run-up 450 cm | 320 | proven class (DM1: 320, `L_ItemTest`: 350) | `ROCKET_JUMP` |
| RJ2 | End Terrace NE → East Roof (mirror) | 320 | proven class | `ROCKET_JUMP` |
| RJ3 | Trench floor (−1050, 1600, 0) → car roof west end | 320 | proven class | `ROCKET_JUMP` |
| RJ4 | Forum floor, north or south canyon → Spire Deck | 320 | proven class; Quad to the pad hub in one jump | `ROCKET_JUMP` (one, from the Quad side) |
| RJ5 | Concourse → Viaduct, anywhere | 320 | proven class | none (P9 and the bridges cover it) |
| RJ6 | Forum / Trench / Concourse floor → a roof directly | 640 | 240 cm under the 880 theoretical apex; human skill jump, **must be measured on the blockout** | none |
| PJ | PG-W sits directly under the 320 cm West Roof wall | 320 | plasma climb / plasma jump height is not documented anywhere in the repo: **no route depends on it**; open question 4 | none |

All rocket-jump surfaces are open to the sky (≥ 900 cm clearance by construction).

### Gap jumps, drops and ramp slides

| Id | What | Numbers | Bot link |
|---|---|---|---|
| G1 | Terrace W/E south end or End Terrace SW ↔ Viaduct, across the Concourse | 500 cm gap, level (Z 320 both sides); a plain jump covers 540 at 800 cm/s | `JUMP_FAST`, both ways (500 is the `JUMP` limit with no margin) |
| G2 | Roof inner edge → glass bridge `BrW`/`BrE` deck (skipping the terrace) | 600 cm horizontal, 320 drop: a 0.996 s jump at 800 cm/s covers 797 | `DROP` to the terrace only |
| G3 | Viaduct north edge → kiosk row / Concourse; terrace edges → Forum, Trench | 320 drops everywhere, 640 from the roofs' outer corners to L0 | `DROP` per free edge where the landing is ≥ 300 × 300 and ≥ 500 from a wall (E3 rule) |
| R1 | **Ski jump NW / SE**: down the 20° roof ramp (880 cm run, 320 drop), across the 600 cm terrace, off the free edge | From the terrace edge (X ∓2000, Z 320) with a jump: 996 cm at 1000 cm/s, 1295 cm at 1300 cm/s; lands in the Forum's 11–14 m side plaza, short of the Spire (1400 cm away): a clean, repeatable speed line into the bowl | `DROP` at the edge |
| R2 | **Clivus SW / NE**: down the 20° Forum ramp (880 run) heading for the south gate (SW) or the north-east gate (NE) | exit on the Forum floor 1300 cm before the gate with ramp speed | walkable |
| R3 | Ramp exit upward: a fast run **up** a roof ramp exits the 20° lip with upward velocity (`RampExitMinAngle` 5°) onto the ramp head or over it onto the roof | to be tuned on the blockout; gives the RA/Mega approach a skill ceiling | walkable |

## 9. Main loops and control flow

- **Loop A, "Culmina" (roof loop, L2/L1, ≈ 12 s per lap by pads):** West Roof (RA) → P5 → Spire Deck → P4 → East Roof (Mega) → P6 → Spire Deck → P3 → West Roof. The stack loop. It is entirely ballistic and overlooked; whoever runs it owns 100 armor + 100 health per cycle and is the map's target.
- **Loop B, "Viae" (lane loop, L0/L1, ≈ 150 m):** Trench west → L-N east (YA-N, slugs, 5 HP line) → P11 → End Terrace NE (SG-E) → Terrace E south → `BrSE` → Viaduct west (YA-S, Haste, LG) → G1 or `BrSW` → Terrace W north → drop into the Trench. Two Yellow Armors, three weapons, Haste; never touches a roof.
- **Loop C, "Forum" (bowl loop, L0/L1, ≈ 130 m):** RL → south gate → Concourse → P9 → Viaduct → `BrSW` → Terrace W → Clivus SW down → Forum → Quad canyon → north gate → Trench → P7 → RG → P8 → Terrace W. The weapons loop around the centre: RL, RG, Quad.
- **Loop D, "the climb" (street to stack):** any lane → P1 or P2 (the lane-end pads) → roof → Loop A. And back down by the ski jumps.

**How control flows.** There is no room to hold; there are three economies that overlap at four knots (the Spire Deck, the two lane-end pads, the car roof). The roof player is strong and visible. The lane player is fast and has the armor to contest. The bowl player has the RL, the RG and the Quad timing. In 6–8 player FFA the roofs change hands constantly because every roof arrival is announced by a pad sound and a 1.2–1.6 s arc; in 4-player games the lane loop is long enough that two players can live on it without meeting, which is the reason for the Quad/Haste pair pulling everybody back to the middle every two minutes.

## 10. Sightlines, chokes and camping risks

| Line / spot | Length | Assessment and counter |
|---|---|---|
| Trench lane end to end | 76 m | The longest rail lane on any Velox map, on purpose. Counters: the RG spawns in the middle of it, not at an end; both ends are pad triggers (nobody can stand there); the lane floor is visible from both terraces' north ends (L1 looks down on the "sniper"); the car gives the north half cover. If playtests show a rail gallery, add a second parked car on the south strip's north edge at X 2200..3000 (breaks the line, keeps the strip 400 clear). |
| Viaduct end to end | 60 m | Same class, but both ends are free edges over pockets with no cover and the Concourse can shoot up at the whole north edge. |
| Roof to roof | 52–66 m | RA and Mega are in each other's view. Each roof has the 120 cm inner parapet (crouch cover) and the billboard wall; neither roof has health or the RG. A roof camper needs to bring the rail from the car roof and is in view of the other roof, both terraces and the Spire Deck. |
| Roof down into the Forum | 15–35 m, steep | The classic overlook. The Forum's two canyons (Quad, RL) are hidden from the roofs by the Spire Deck slab only at their inner halves; expect the Quad pickup to be contested from above: intended. |
| North gate ↔ south gate diagonal | blocked | The single south gate at X ±200 and the north gates at X ±1400 are arranged so the Spire base (±600) cuts every Trench-to-Concourse line through the Forum. |
| `BrW` – Spire – `BrE` | blocked by the core | 2 × 2 m core at the deck centre; lines of 20 m either side remain. |
| Chokes | | The three Forum gates (400 wide, 200 deep) are the only L0 chokes; each has an L1 bypass within 10 m (terrace drop or bridge). The glass bridges are 300 wide and 5–14 m long: exposed, never the only way. No dead ends: both pockets and both Trench ends terminate in a pad. |
| Spawn safety | | No spawn sees a major item; the Forum has only one spawn (S5, behind the Spire's south-east corner). |

## 11. Readability and palette

The style guide rule stands: **the player is the most saturated thing in any frame** (environment V ≤ 0.50, S ≤ 0.25; skins V ≥ 0.94, S ≥ 0.76). A neon city breaks that rule by default, and the six skin hues (0°, 53°, 128°, 180°, 212°, 311°) cover most of the classic neon palette, cyan and magenta included. So the neon is rationed by four binding rules:

1. **Surfaces are graphite.** Decks `#2B2E33` (V 0.20, S 0.16), panels `#4A4F57` (V 0.34, S 0.15), smoked glass `#1D2126`, steel trim `#6B7078` (V 0.47, S 0.11). All inside the existing rule. Floors are darker than walls so skins read against both.
2. **Two accent hues only, both outside the skin set:** violet (H 265°, 47–53° from Blue and Magenta) and ice white (H 200°, S ≤ 0.15). No cyan, no magenta, no pure red/green/yellow emissive on any kit piece. Amber exists only in the skybox skyline.
3. **Neon is thin and high or thin and flat.** Emissive strips are ≤ 10 cm wide. They sit either ≥ 250 cm above a walkable floor (edge lines of towers, bridge undersides, gate lintels) or flush in the floor (maglev rails, pad rings, deck edge lines that mark every free edge). No emissive surface larger than 0.5 m² between 0 and 250 cm above any floor: nothing glowing ever stands behind a player's silhouette at player height.
4. **Holo signage is a landmark, not wallpaper.** Four signs in total, all above Z 700 or beyond the clip wall: a violet ring stack on the Spire core (centre), "I" above the West Roof billboard (violet), "II" above the East Roof billboard (ice white), and the maglev destination board over the Trench's east portal. West = violet, east = white, north = trench and skyline tower, south = viaduct: four-way orientation at a glance.

Pickups keep their definition colours; the Quad (blue) and Mega (blue) glows are the brightest things on the street, as intended by the style guide. "Glass" is **opaque** smoked material with a bright edge line, not a translucent one (ADR 0006 §8 allows no translucency besides the LG beam and pickup glows): bridges are readable as floors, and nobody shoots at a player seen through a bridge who cannot be hit through it.

A palette assertion extension is needed (section 13): emissive accents allowed only with hue in {265° ± 10°, any hue with S ≤ 0.15}.

## 12. Lighting plan

- **Static only** (ADR 0006 §5): static `SkyLight` with a new 128 px cubemap `TC_Sky_Culmen` (deep blue zenith, warm dusk band on the west horizon, skyline glow), one static directional "city glow / moon" light from the north-west at a low angle for long baked shadows that give the flat decks depth and tell west from east.
- **Static point and spot lights: about 80–90** (count is free at runtime; cost is bake time): one cool-white spot per 800 cm of each lane (≈ 18), one per pad (11), one per major item and weapon (≈ 15, slightly tinted with the pickup colour at S ≤ 0.3), under-bridge fills (8), gate lintels (6), Forum bowl fills (≈ 12), roof and terrace edge fills (≈ 16). Emissive strips do not light the scene (`bUseEmissiveForStaticLighting` off); a weak violet or white point light under selected strips fakes it.
- Target exposure: floors mid-dark, players lit mainly by the volumetric lightmap (200 cm cells) plus their own emissive floor (0.35); no pitch-black corners: minimum bake luminance on any walkable floor is an audit item (section 17).
- 8 sphere reflection captures at 128 px (Forum ×2, Trench ×2, Concourse ×2, one per roof).
- Skybox: an unlit low-poly skyline card ring (one mesh, ≈ 2 k triangles, outside the Lightmass importance volume) with sparse emissive windows in amber and white, one giant tower due north as the compass landmark, and the cubemap sky. No moving sky, no fog volumes; height fog is static exponential fog at low density if the Mac playtest says the skyline needs separation.
- Bake: path B (Mac) as for DM1. The map is open, so Lightmass has more sky visibility and fewer bounces to resolve than DM1; expect a similar or shorter Production bake for 25 % more area.

## 13. Kit: reuse and new pieces

The per-map budget is ≤ 25 distinct kit meshes × ≤ 8 materials (ADR 0006 §8). Culmen needs a second **theme set** of the kit: the same module, pivots and import path, new material instances, plus new pieces. `Tools/blender/kit.py` gets a `--theme culmen` switch writing its own manifest; the 25/8 assertions apply per theme.

**Reused from DM1 (12 meshes, re-skinned by material instance only):** `Floor_400`, `Wall_400`, `Wall_Trim`, `Wall_Half`, `Wall_Door400` (gate lintels), `Ledge_Edge`, `Railing_Stub` (bridge-head parapet stubs), `Pad_Ring`, `Pedestal`, `Light_Fixture`, `Skirting_400`, `Pillar` (Spire core, stacked). Not used: ceilings, beams, `Door300`, `Stairs_5x20`, `Ramp_15`, `Bridge_400`.

**New pieces (10 meshes → 22 in total):**

| Mesh | Size (cm) | Purpose | Tris (budget ≤ 600) |
|---|---|---|---|
| `SM_Kit_Floor_800` | 800 × 800 × 20 | big deck slab for the open areas, lightmap 128 (see section 14) | 12 |
| `SM_Kit_Ramp_20` | 400 × 400 × 145.6 | the four 20° ramps (2.2 pieces per run; the generator scales X on the last one, as it does for boxes) | 8 |
| `SM_Kit_GlassBridge_400` | 400 × 300 × 40 | opaque smoked deck, edge line in UV for the emissive strip | ≈ 40 |
| `SM_Kit_Parapet_Glass_400` | 400 × 10 × 120 | perimeter and roof parapets (clip is the wall behind it / a 120 cm clip box) | ≈ 24 |
| `SM_Kit_Rail_Inlay_400` | 400 × 40 × 2 | maglev rail, flush floor decal mesh, no collision | 12 |
| `SM_Kit_Neon_Strip_400` | 400 × 10 × 5 | the emissive line (violet or ice by material instance) | 12 |
| `SM_Kit_Roof_Ridge_400` | 400 × 200 × 119 | 50° gable cap for the Porticus and kiosks: not walkable, so no isolated nav and no clip above it | 8 |
| `SM_Kit_Maglev_Car` | 1200 × 400 × 320 | the parked car (one mesh; its clip is the `Car` box) | ≤ 400 |
| `SM_Kit_Holo_Sign` | 800 × 10 × 400 | emissive sign plane on a thin frame; four instances, glyph by material parameter | ≈ 30 |
| `SM_Kit_Skyline_Ring` | ≈ 40000 diameter | skybox city cards, unlit | ≈ 2000 (exempt from the 600 rule; listed as a sky asset) |

**Materials (8 `MI_Culmen_*` of `M_Kit_Master`):** `Deck`, `Panel`, `PanelDark`, `Steel`, `Glass` (opaque), `NeonViolet` (emissive), `NeonIce` (emissive, also the light fixture), `Holo` (emissive, glyph atlas). Textures: reuse `T_Kit_Plate`, `T_Kit_Steel`, `T_Kit_Trim`; new `T_Culmen_Panel` (512) and `T_Culmen_Glyphs` (512) → 5 textures.

**Tooling extensions (to be listed in the blockout/art tasks, not designed here):** kit themes in `kit.py` and `create_kit_materials.py`; the palette assertion with the accent-hue window; a `Ramp(deg=20)` check in the layout validator (currently 15° only is used); a layout rule "no emissive kit instance between 0 and 250 cm above a floor unless flush" in `audit_map.py`.

## 14. Budgets (estimate, to be replaced by `audit_map.py` numbers)

| Budget (ADR 0006 §8) | Limit | Estimate | Basis |
|---|---|---|---|
| Distinct kit meshes × materials | ≤ 25 × ≤ 8 | 22 × 8 | section 13 |
| Triangles in view | ≤ 300 k | ≈ 110 k worst case | ≈ 1500 kit instances × ≈ 30 tris = 45 k; skyline 2 k; car 0.4 k; 8 characters × 6 k = 48 k; 8 weapons × 1.5 k = 12 k; pickups ≈ 5 k |
| Draw calls | ≤ 600 | **≈ 400–560, the tight one** | Open map: from a roof nearly everything is in the frustum and occlusion culling helps little. 22 meshes × ≈ 6 lightmap atlases ≈ 130 instanced batches, × 2 (depth prepass + base pass with forward shading) ≈ 260; 46 pickups × 2 ≈ 90 (fewer when taken); 8 characters and weapons × 2 ≈ 32; projectiles, beams, HUD ≈ 40–80; sky and skyline 4. DM1's rooms hide most of the map; Culmen does not. |
| Lightmaps | ≤ 48 MB | ≈ 30 MB | decks: ≈ 3500 m² ≈ 55 × `Floor_800` + ≈ 60 × `Floor_400` at 128 → 115 × 85 KB ≈ 10 MB; ≈ 900 wall/side pieces at 64 (21 KB) ≈ 19 MB; trims at 32 ≈ 1 MB |
| Components with lightmap res > 64 | ≤ 120 | ≈ 115 + 10 ramp pieces → **at the limit** | This is why `Floor_800` exists (6.2 cm lightmap texels on the open decks instead of 3.1). If the audit exceeds 120, the Trench and Concourse floors drop to res 64 on `Floor_800`. |
| Textures | ≤ 256 MB | < 10 MB | 5 × 512² + cubemap |
| Translucent surfaces | none besides LG beam and pickup glow | none | opaque glass, opaque holo signs (emissive, not additive) |

If the draw-call audit on the Mac exceeds 600 from the worst camera (West Roof ramp head looking east): first merge the static perimeter (parapets, tower outer faces, skyline) into three merged meshes per side at art-pass time (they are never near a player), then reduce the neon strip instance count by using 800 cm strips.

## 15. Bots and navigation

- **No hazard handling is needed**: there is no void, no lava and no kill volume inside the arena. Every edge is a drop of 320 or 640 cm onto walkable floor. This is the main reason the design should carry over DM1's bot behaviour without new code.
- **Nav:** one `NavMeshBoundsVolume` over the footprint from Z −100 to 1100. `fill_nav_kinds` as DM1 (`floor`, `wall`, `platform`, `step`): towers, terraces, the Viaduct plinth, the car and the Spire base are solid, so no nav is generated inside them. Ramps are sealed with stepped fill boxes (as `A_Ramp_Fill_*`). The glass bridges are the only walk-under pieces (280 cm clear, 300 wide: their own nav above, Forum/Concourse nav below).
- **Not walkable by construction:** Porticus and kiosk tops (50° ridge > `WalkableFloorAngle`), the billboard walls and parapets (clip boxes up to `CLIP_H` above the billboards; the 120 cm parapets are 10 cm thick with a clip box of the same height, so a player can jump over them and a bot never paths onto them), the Spire core. Target: `navlint` `clusters=0`, `lowclear polys=0`, no `NavLintIgnore`.
- **Links (estimate ≈ 45):** 11 `JUMP_PAD` (start = trigger centre on the floor, end = the landing column in the pad table); 4 `ROCKET_JUMP` (RJ1–RJ4, start 450 cm from the wall, end 100 cm onto the target); 4 `JUMP_FAST` both ways for G1 (terrace W/E south ends and two on End Terrace SW ↔ Viaduct; `required_speed` from the 500 cm gap); ≈ 26 `DROP` (terrace inner edges ×3 each side, terrace ends ×4, Spire Deck ×4 minus the bridge heads, Viaduct north edge ×5 and ends ×2, car roof ×3, roof edges onto terraces and End Terraces ×6; none where the landing is a pad trigger, a ramp, or within 500 cm of a wall, per E3).
- **Known bot risks:** (1) the roof pads fly 24 m: `DumpPadLandings` must agree within 60 cm, and the bot follower's link timeout (`TimeoutSeconds`, D28) must cover a 1.56 s flight plus run-in; (2) bots strafe-jumping the 70 m lane will arrive at P1/P11 at well above ground speed: with `override_velocity` the pad result is speed-independent, but the trigger must be entered on the floor (use the lowered trigger extent from task m6-16 on P1, P2, P11); (3) link success ≥ 90 % per type is a hard invariant and Culmen has 4× the pad links of DM1: expect one or two pads to need target tuning in the bot-iteration step; (4) the 20° ramps are new to the bots (15° everywhere so far) although well under the 45.5° limit.
- Item coverage invariant (every pickup taken once over FFA ×10): the two 50 HP in the far corners and the roof-ramp shards are the likely stragglers; both sit on a pad approach on purpose.

## 16. Hazards and rules

- **None lethal.** No void, no lava, no crushers, no moving geometry. `KillZ −1000` and the perimeter clip exist only as safety nets; `left_arena` and `world_kill` must stay 0.
- **Falls:** 320 and 640 cm drops are part of the routes. Culmen assumes the current combat settings for fall damage unchanged (whatever `DA_CombatSettings_Default` does on DM1's 640 cm tower drop applies here); if falls from 640 hurt, the roofs get that as a small price, which is acceptable.
- **Deferred on purpose:** a moving maglev train through the Trench (telegraphed, lethal) is the obvious set piece. It needs a replicated mover, a server-side kill volume with frag credit, lag-compensation and bot awareness: new code in three domains. Not in this concept; see open question 3.

## 17. Acceptance ideas (blockout FT and soaks)

1. `python3 -I Tools/editor/create_culmen_map.py --validate-only`: all DM1 table rules plus: the eleven simulated pad arcs land within 60 cm of the table above and clear every solid by ≥ 60 cm before and after the apex; ramps are 20° ± 0.5°; every free edge has a `DROP` link or a stated exemption; spawn rules; ammo 6–12 m from its weapon; Quad and Haste disabled in Duel.
2. `navlint.sh /Game/Maps/Culmen/L_Culmen_Blockout` → `clusters=0`, `lowclear polys=0`; nav build ≤ 2 s (the map is 25 % larger than DM1: watch this number).
3. `FT_Culmen_Reachability`: `FindPath` (Full) from every spawn to every pickup and between RA, Mega, YA-N, YA-S, RG, Quad, Haste; `DumpPadLandings` within 60 cm for all 11 pads.
4. Move script `CulmenRun.json` in the default soak list, judged like `PadRun`/`DM1Run` (reaches each marker, Σ strays ≤ 2.5 cm, 100 ms / 2 % loss): (a) L-N west to east from S1, strafe-jumping, with `analyze_run.py --min-peak-factor 1.5` over the 70 m (to be calibrated; 1.4 is proven in 51 m); (b) the roof chain P5 → P4 → P6 → P3; (c) ski jump NW: down the ramp, off the terrace edge, landing marker in the Forum; (d) G1 gap both ways.
5. Bot soaks report-only first (BotRun ×3, FFA ×10 with 8 bots), then bands in `infra/ci/bands/L_Culmen.sh` by architect ruling: hard invariants as on any map (`left_arena 0`, `world_kill 0`, `no_nav_point 0`, `giveup 0`, stuck strikes ≤ 5, dwell ≤ 5 s, link success ≥ 90 % per type, every pickup taken).
6. Layout metrics recorded in the task notes: RA → Mega path time by pads (expected 5–6 s) and by foot via Loop B, trench lane clear length, the three sightline lengths of section 10 measured by line traces.
7. Art pass: `audit_map.py --diff-collision L_Culmen_Blockout L_Culmen`; budgets of section 14; the new audit rule for emissive placement; a palette screenshot check on the Mac playtest with all six skins standing in front of each of the four holo signs and on a maglev rail (the checklist item that decides whether the neon rule works).
8. Human playtest (Mac): correction jitter on the 24 m pad flights with two clients, readability of opaque "glass", whether the roofs are fun or a rail gallery.

## 18. Risks and open questions

**Risks, ranked.**
1. **Draw calls on an open map** (section 14). DM1's budget was met by rooms hiding each other; Culmen shows everything from the roofs. The estimate is under 600 but without margin, and it can only be measured on the Mac. Mitigations are listed; the fallback of last resort is a solid billboard wall along each roof's inner edge, which also changes the gameplay.
2. **Rail dominance on 60–76 m lanes.** Intended as the map's character, countered by RG placement and exposure, but only a playtest with humans decides. The second-car fix is a layout table change, not a redesign.
3. **Neon vs skins.** The four rules are strict enough on paper; the real test is cyan and magenta skins against violet strips at distance. If violet reads as "magenta-ish" on the Mac's display, the fallback accent is ice white only.
4. **Pad count and bot link success.** Eleven pads and ≈ 45 links against DM1's 3 and 16; the ≥ 90 % invariant will take iteration.
5. **20° ramps** are new: ramp-slide tuning (`RampSlideMinNormalSpeed`, `RampExitMinAngle`) was validated on 15°. If the ski jump feels wrong, the fallback is 15° ramps with a 1194 cm run (roof ramps then start 200 cm into the terraces).
6. **Size.** 76 × 46 m with 4 players may feel empty; the powerup pair and seven of ten spawns on the street are the countermeasures. Recommended minimum is 4 with bots filling to 6.

**Open questions for the human.**
1. Two powerups (Quad + Haste) in FFA, or Quad only as on DM1? Haste on the Viaduct is a large part of the "speed" identity; recommended: both.
2. Is opaque "glass" acceptable, or do you want one real translucent glass material as an exception to ADR 0006 §8 (costs a translucency pass and makes players visible through bridges they cannot be shot through)? Recommended: opaque.
3. The moving maglev train as a telegraphed lethal hazard: wanted for a later iteration (new code, own ADR), or never?
4. Plasma jump / plasma climb: is it meant to be a supported technique in Velox? Its height has never been measured; if yes, PG-W under the West Roof wall becomes a designed route and needs a measurement task first.
5. Accent colour: violet + ice white as proposed, or ice white only (safest for the skins, less "neon")?
6. Should the layout stay point-symmetric with a view to a CTF variant in M7, or may the blockout iteration break the symmetry freely for FFA flow?
