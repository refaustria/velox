# Task M5-03b - test-map lint baseline (report-only), 2026-10-31

Run on `m5-map-characters` + task 03b, `infra/ci/navlint.sh /Game/Maps/<Map> --report-only` (Grid 50; then `--grid 25`), after the one
regeneration of the three maps (`Layout:` tags, `L_ItemTest` north seals, `cover` / `platform` fills). **This is the baseline**: a later
regeneration of a test map must not add a cluster of any kind except `edge` beyond it (ADR 0006 E0.2 (e)). In the summary line
`walkin` / `isolated` now exclude `edge` (`clusters = walkin + isolated + edge`).

## Summary lines before (notes/03-report.md) and after

| Map | before | after |
|---|---|---|
| `L_ItemTest` | clusters=11 (9/2) lowclear=0 | clusters=11 (walkin=9 isolated=2 edge=0) lowclear=0 |
| `L_CombatTest` | clusters=3 (0/3) lowclear=6 | clusters=2 (walkin=0 isolated=2 edge=0) lowclear=0 |
| `L_MovementTest` | clusters=19 (15/4) lowclear=13 | clusters=19 (walkin=10 isolated=4 edge=5) lowclear=2 |

Counts lines of the generators: movement StaticMeshActors=115 (expected 115), 12 nav links; combat StaticMeshActors=21 (expected 21); item
navlinks 15 (2 JumpPad, 12 Drop, 1 RocketJump), `platform=17` (was 15), pickups 23. Every actor label appears as `layout=` in the lint lines below.

## Classification (E = expected, F = finding, A = artefact)

### L_ItemTest (lowclear 0)
- 0, 1: D17 room floor ring / wall tops - E. 3, 4: D17 room slots (the deliberate off-mesh bed of `FtOffMesh`) - E. 6-9: central cross arms - E.
- The Mega / RA north slots (before: clusters 5, 6) are **gone** (sealed). The corridor wall top (100 cm wide, Z 250) is now three `walkin` clusters (2, 5, 10) instead of one:
  the fill boxes put covered platform tops (Z 300) beside the wall top, so the dZ tolerance links them; before, only the YA ramp-top did. E (D17 width class, wall tops). Total wall-top area fell from 50 to 40.25 m2; no new surface.
### L_CombatTest (lowclear 0)
- 0, 1: cover tops of `Cover_07` (400 x 150, Z 250) and `Cover_08` (150 x 300, Z 200) - E. The third isolated top of 03 (`Cover_02`) now meshes at Grid 50 (the fill changed the erosion around it); at Grid 25 it is reported `walkin` (2, 3, 4: `Cover_02/03/06` tops) - no judgement, Grid 25 is informational.
### L_MovementTest (lowclear 2)
- 0, 2, 3, 12: corridor wall tops, 60 cm stairs (must block) - E. 8, 13, 17, 18: now `kind=edge` (as predicted); 11 (`Stairs_60cm_00`) became `edge` too - A.
- 4, 5: floor under the 10 / 45 deg ramps - F, report-only by ruling (E0.2 (d)). 6, 7, 9, 10, 14, 15, 16: 100 cm inter-lane slots - F, report-only.
- 1 and lowclear 0, 1: `RampPlatform_45deg` - see the 45 deg finding.

### 45 deg platform (<= 30 min)
Not a one-line fix; recorded and stopped. Experiment: `fill_nav=False` for `RampPlatform_45deg` only (the fill is not the cause): the missing west 250 cm (cluster 1, 15 m2) and the two Z 1075 islands (X 1226, Y 2916 / 3144, 56 cm under the platform top) stay identical; the only change is the Z 10 islands inside the box return (lowclear 4), i.e. the fill is what removed those. So the `platform` fill resolved 11 of 13 lowclear polys; the remaining 2 are Recast geometry at the ramp / platform junction (the 20 cm slab tip ends 14 cm inside the platform, 45 deg is 0.5 deg under `AgentMaxSlope`). Untested hypotheses: shortening the slab by its thickness, or `AgentMaxSlope` margin. Both change the lane geometry; left to the lead. The movement-map gate (lowclear 0) therefore stays red on these 2 polys; they are inside a solid box (inside=1) - no bot plays the ramp lanes.

## Full report-only output (Grid 50)
```
=== L_ItemTest 
L_ItemTest walkable_m2=2727.50 covered_pct=92.57 clusters=11 (walkin=9 isolated=2 edge=0) lowclear=0 result=REPORT
  cluster i=0 m2=70.00 center=(4700,1150,5) extent=(2200,1500) actor=Room_Floor layout=Room_Floor cells=280 kind=isolated over=none over_layout=-
  cluster i=1 m2=62.00 center=(4700,1150,255) extent=(2000,1300) actor=Room_Wall_N layout=Room_Wall_N cells=248 kind=isolated over=none over_layout=-
  cluster i=2 m2=31.00 center=(300,1550,260) extent=(3100,100) actor=Corridor_Wall layout=Corridor_Wall cells=124 kind=walkin over=Corridor_WallCap_RampTop over_layout=Corridor_WallCap_RampTop
  cluster i=3 m2=12.00 center=(4700,650,5) extent=(1200,100) actor=Room_Floor layout=Room_Floor cells=48 kind=walkin over=none over_layout=-
  cluster i=4 m2=10.00 center=(4800,1650,5) extent=(1000,100) actor=Room_Floor layout=Room_Floor cells=40 kind=walkin over=none over_layout=-
  cluster i=5 m2=7.75 center=(-2100,1550,255) extent=(800,100) actor=Corridor_Wall layout=Corridor_Wall cells=31 kind=walkin over=none over_layout=-
  cluster i=6 m2=2.00 center=(-200,0,205) extent=(200,100) actor=Cross_X layout=Cross_X cells=8 kind=walkin over=none over_layout=-
  cluster i=7 m2=2.00 center=(0,-200,205) extent=(100,200) actor=Cross_Y layout=Cross_Y cells=8 kind=walkin over=none over_layout=-
  cluster i=8 m2=2.00 center=(0,200,205) extent=(100,200) actor=Cross_Y layout=Cross_Y cells=8 kind=walkin over=none over_layout=-
  cluster i=9 m2=2.00 center=(200,0,205) extent=(200,100) actor=Cross_X layout=Cross_X cells=8 kind=walkin over=none over_layout=-
  cluster i=10 m2=1.50 center=(2425,1550,255) extent=(150,100) actor=Corridor_Wall layout=Corridor_Wall cells=6 kind=walkin over=none over_layout=-
=== L_CombatTest 
L_CombatTest walkable_m2=1600.00 covered_pct=99.06 clusters=2 (walkin=0 isolated=2 edge=0) lowclear=0 result=REPORT
  cluster i=0 m2=8.00 center=(-1300,1500,255) extent=(400,200) actor=Cover_07 layout=Cover_07 cells=32 kind=isolated over=none over_layout=-
  cluster i=1 m2=6.00 center=(900,1600,205) extent=(200,300) actor=Cover_08 layout=Cover_08 cells=24 kind=isolated over=none over_layout=-
=== L_MovementTest 
L_MovementTest walkable_m2=9164.00 covered_pct=97.66 clusters=19 (walkin=10 isolated=4 edge=5) lowclear=2 result=REPORT
  cluster i=0 m2=126.00 center=(2550,0,255) extent=(5300,1200) actor=CorridorWall_L layout=CorridorWall_L cells=504 kind=isolated over=none over_layout=-
  cluster i=1 m2=15.00 center=(1175,3100,1124) extent=(250,600) actor=RampPlatform_45deg layout=RampPlatform_45deg cells=60 kind=walkin over=RampPlatform_45deg over_layout=RampPlatform_45deg
  cluster i=2 m2=14.00 center=(375,5900,245) extent=(350,400) actor=Stairs_60cm_02 layout=Stairs_60cm_02 cells=56 kind=isolated over=none over_layout=-
  cluster i=3 m2=14.00 center=(775,5900,545) extent=(350,400) actor=Stairs_60cm_07 layout=Stairs_60cm_07 cells=56 kind=isolated over=none over_layout=-
  cluster i=4 m2=9.25 center=(625,950,5) extent=(650,700) actor=Ground layout=Ground cells=37 kind=walkin over=Ramp_10deg over_layout=Ramp_10deg
  cluster i=5 m2=4.25 center=(125,3050,5) extent=(150,700) actor=Ground layout=Ground cells=17 kind=walkin over=Ramp_45deg over_layout=Ramp_45deg
  cluster i=6 m2=4.25 center=(1775,650,5) extent=(450,100) actor=Ground layout=Ground cells=17 kind=walkin over=none over_layout=-
  cluster i=7 m2=3.50 center=(1725,1350,5) extent=(350,100) actor=Ground layout=Ground cells=14 kind=walkin over=none over_layout=-
  cluster i=8 m2=3.00 center=(1975,1000,283) extent=(50,600) actor=RampPlatform_10deg layout=RampPlatform_10deg cells=12 kind=edge over=none over_layout=-
  cluster i=9 m2=2.75 center=(1650,2050,5) extent=(300,100) actor=Ground layout=Ground cells=11 kind=walkin over=none over_layout=-
  cluster i=10 m2=2.50 center=(1275,3450,5) extent=(250,100) actor=Ground layout=Ground cells=10 kind=walkin over=none over_layout=-
  cluster i=11 m2=2.00 center=(125,5900,65) extent=(50,400) actor=Stairs_60cm_00 layout=Stairs_60cm_00 cells=8 kind=edge over=none over_layout=-
  cluster i=12 m2=2.00 center=(1025,5900,725) extent=(50,400) actor=Stairs_60cm_11 layout=Stairs_60cm_11 cells=8 kind=isolated over=none over_layout=-
  cluster i=13 m2=2.00 center=(1075,-3500,645) extent=(50,400) actor=JumpPadC_Ledge layout=JumpPadC_Ledge cells=8 kind=edge over=none over_layout=-
  cluster i=14 m2=1.75 center=(400,1350,5) extent=(200,100) actor=Ground layout=Ground cells=7 kind=walkin over=none over_layout=-
  cluster i=15 m2=1.75 center=(1450,2750,5) extent=(200,100) actor=Ground layout=Ground cells=7 kind=walkin over=none over_layout=-
  cluster i=16 m2=1.50 center=(225,2050,5) extent=(150,100) actor=Ground layout=Ground cells=6 kind=walkin over=none over_layout=-
  cluster i=17 m2=1.00 center=(575,5200,245) extent=(50,200) actor=Stairs_40cm_05 layout=Stairs_40cm_05 cells=4 kind=edge over=none over_layout=-
  cluster i=18 m2=1.00 center=(725,5200,325) extent=(50,200) actor=Stairs_40cm_07 layout=Stairs_40cm_07 cells=4 kind=edge over=none over_layout=-
  lowclear_poly i=0 center=(1226,3144,1075) headroom=0 inside=1 actor=RampPlatform_45deg layout=RampPlatform_45deg
  lowclear_poly i=1 center=(1226,2916,1075) headroom=0 inside=1 actor=RampPlatform_45deg layout=RampPlatform_45deg
```

## Report-only output at `--grid 25` (no judgement)
```
=== L_ItemTest --grid 25
L_ItemTest walkable_m2=2587.75 covered_pct=92.93 clusters=12 (walkin=10 isolated=2 edge=0) lowclear=0 result=REPORT
  cluster i=0 m2=62.00 center=(4700,1150,255) extent=(2000,1300) actor=Room_Wall_N layout=Room_Wall_N cells=992 kind=isolated over=none over_layout=-
  cluster i=1 m2=53.25 center=(4700,1150,5) extent=(2200,1500) actor=Room_Floor layout=Room_Floor cells=852 kind=isolated over=none over_layout=-
  cluster i=2 m2=31.56 center=(300,1550,260) extent=(3200,100) actor=Corridor_Wall layout=Corridor_Wall cells=505 kind=walkin over=Corridor_WallCap_RampTop over_layout=Corridor_WallCap_RampTop
  cluster i=3 m2=8.38 center=(-2000,1550,255) extent=(1000,100) actor=Corridor_Wall layout=Corridor_Wall cells=134 kind=walkin over=none over_layout=-
  cluster i=4 m2=6.12 center=(4688,650,5) extent=(1225,50) actor=Room_Floor layout=Room_Floor cells=98 kind=walkin over=none over_layout=-
  cluster i=5 m2=5.06 center=(4788,1650,5) extent=(1025,50) actor=Room_Floor layout=Room_Floor cells=81 kind=walkin over=none over_layout=-
  cluster i=6 m2=2.25 center=(-188,0,205) extent=(225,100) actor=Cross_X layout=Cross_X cells=36 kind=walkin over=none over_layout=-
  cluster i=7 m2=2.25 center=(0,-188,205) extent=(100,225) actor=Cross_Y layout=Cross_Y cells=36 kind=walkin over=none over_layout=-
  cluster i=8 m2=2.25 center=(0,188,205) extent=(100,225) actor=Cross_Y layout=Cross_Y cells=36 kind=walkin over=none over_layout=-
  cluster i=9 m2=2.25 center=(188,0,205) extent=(225,100) actor=Cross_X layout=Cross_X cells=36 kind=walkin over=none over_layout=-
  cluster i=10 m2=2.06 center=(1800,-1000,355) extent=(400,400) actor=Rockets_Platform layout=Rockets_Platform cells=33 kind=walkin over=none over_layout=-
  cluster i=11 m2=1.81 center=(2400,1550,255) extent=(200,100) actor=Corridor_Wall layout=Corridor_Wall cells=29 kind=walkin over=none over_layout=-
=== L_CombatTest --grid 25
L_CombatTest walkable_m2=1538.75 covered_pct=98.81 clusters=5 (walkin=3 isolated=2 edge=0) lowclear=0 result=REPORT
  cluster i=0 m2=6.00 center=(-1300,1500,255) extent=(400,150) actor=Cover_07 layout=Cover_07 cells=96 kind=isolated over=none over_layout=-
  cluster i=1 m2=4.50 center=(900,1600,205) extent=(150,300) actor=Cover_08 layout=Cover_08 cells=72 kind=isolated over=none over_layout=-
  cluster i=2 m2=2.38 center=(-400,1000,255) extent=(300,200) actor=Cover_02 layout=Cover_02 cells=38 kind=walkin over=none over_layout=-
  cluster i=3 m2=1.56 center=(500,300,205) extent=(200,400) actor=Cover_03 layout=Cover_03 cells=25 kind=walkin over=none over_layout=-
  cluster i=4 m2=1.12 center=(1300,-100,205) extent=(200,200) actor=Cover_06 layout=Cover_06 cells=18 kind=walkin over=none over_layout=-
=== L_MovementTest --grid 25
L_MovementTest walkable_m2=9004.75 covered_pct=96.74 clusters=38 (walkin=20 isolated=16 edge=2) lowclear=2 result=REPORT
  cluster i=0 m2=126.50 center=(2562,0,255) extent=(5325,1200) actor=CorridorWall_L layout=CorridorWall_L cells=2024 kind=isolated over=none over_layout=-
  cluster i=1 m2=33.06 center=(4988,1400,5) extent=(2025,11200) actor=Ground layout=Ground cells=529 kind=walkin over=none over_layout=-
  cluster i=2 m2=23.62 center=(-2662,9200,255) extent=(4525,450) actor=GapRunwayWall_900_L layout=GapRunwayWall_900_L cells=378 kind=isolated over=none over_layout=-
  cluster i=3 m2=13.62 center=(-1662,8400,255) extent=(2525,450) actor=GapRunwayWall_700_L layout=GapRunwayWall_700_L cells=218 kind=isolated over=none over_layout=-
  cluster i=4 m2=8.19 center=(1338,3100,1136) extent=(375,600) actor=RampPlatform_45deg layout=RampPlatform_45deg cells=131 kind=walkin over=none over_layout=-
  cluster i=5 m2=8.06 center=(638,962,5) extent=(675,675) actor=Ground layout=Ground cells=129 kind=walkin over=Ramp_10deg over_layout=Ramp_10deg
  cluster i=6 m2=7.25 center=(3775,8462,-95) extent=(50,2875) actor=GapPit layout=GapPit cells=116 kind=walkin over=none over_layout=-
  cluster i=7 m2=7.12 center=(-1012,7600,255) extent=(1225,450) actor=GapRunwayWall_500_L layout=GapRunwayWall_500_L cells=114 kind=isolated over=none over_layout=-
  cluster i=8 m2=3.88 center=(925,3100,1060) extent=(400,600) actor=Ramp_45deg layout=Ramp_45deg cells=62 kind=walkin over=none over_layout=-
  cluster i=9 m2=3.06 center=(900,-3500,645) extent=(400,400) actor=JumpPadC_Ledge layout=JumpPadC_Ledge cells=49 kind=walkin over=none over_layout=-
  cluster i=10 m2=3.00 center=(288,5900,185) extent=(75,400) actor=Stairs_60cm_02 layout=Stairs_60cm_02 cells=48 kind=isolated over=none over_layout=-
  cluster i=11 m2=3.00 center=(688,5900,485) extent=(75,400) actor=Stairs_60cm_07 layout=Stairs_60cm_07 cells=48 kind=isolated over=none over_layout=-
  cluster i=12 m2=3.00 center=(1012,5900,725) extent=(75,400) actor=Stairs_60cm_11 layout=Stairs_60cm_11 cells=48 kind=isolated over=none over_layout=-
  cluster i=13 m2=2.44 center=(388,1662,5) extent=(175,675) actor=Ground layout=Ground cells=39 kind=walkin over=Ramp_20deg over_layout=Ramp_20deg
  cluster i=14 m2=2.25 center=(125,3062,5) extent=(150,675) actor=Ground layout=Ground cells=36 kind=walkin over=Ramp_45deg over_layout=Ramp_45deg
  cluster i=15 m2=2.12 center=(1700,9375,5) extent=(800,50) actor=GapLand_900 layout=GapLand_900 cells=34 kind=walkin over=none over_layout=-
  cluster i=16 m2=2.12 center=(1762,650,5) extent=(425,50) actor=Ground layout=Ground cells=34 kind=walkin over=none over_layout=-
  cluster i=17 m2=2.06 center=(650,4875,155) extent=(800,50) actor=Stairs_20cm_02 layout=Stairs_20cm_02 cells=33 kind=walkin over=none over_layout=-
  cluster i=18 m2=2.00 center=(125,5900,65) extent=(50,400) actor=Stairs_60cm_00 layout=Stairs_60cm_00 cells=32 kind=isolated over=none over_layout=-
  cluster i=19 m2=2.00 center=(200,5900,125) extent=(50,400) actor=Stairs_60cm_01 layout=Stairs_60cm_01 cells=32 kind=isolated over=none over_layout=-
  cluster i=20 m2=2.00 center=(375,5900,245) extent=(50,400) actor=Stairs_60cm_03 layout=Stairs_60cm_03 cells=32 kind=isolated over=none over_layout=-
  cluster i=21 m2=2.00 center=(450,5900,305) extent=(50,400) actor=Stairs_60cm_04 layout=Stairs_60cm_04 cells=32 kind=isolated over=none over_layout=-
  cluster i=22 m2=2.00 center=(525,5900,365) extent=(50,400) actor=Stairs_60cm_05 layout=Stairs_60cm_05 cells=32 kind=isolated over=none over_layout=-
  cluster i=23 m2=2.00 center=(600,5900,425) extent=(50,400) actor=Stairs_60cm_06 layout=Stairs_60cm_06 cells=32 kind=isolated over=none over_layout=-
  cluster i=24 m2=2.00 center=(775,5900,545) extent=(50,400) actor=Stairs_60cm_08 layout=Stairs_60cm_08 cells=32 kind=isolated over=none over_layout=-
  cluster i=25 m2=2.00 center=(850,5900,605) extent=(50,400) actor=Stairs_60cm_09 layout=Stairs_60cm_09 cells=32 kind=isolated over=none over_layout=-
  cluster i=26 m2=2.00 center=(925,5900,665) extent=(50,400) actor=Stairs_60cm_10 layout=Stairs_60cm_10 cells=32 kind=isolated over=none over_layout=-
  cluster i=27 m2=1.88 center=(1738,1350,5) extent=(375,50) actor=Ground layout=Ground cells=30 kind=walkin over=none over_layout=-
  cluster i=28 m2=1.62 center=(1950,1000,283) extent=(50,600) actor=RampPlatform_10deg layout=RampPlatform_10deg cells=26 kind=walkin over=none over_layout=-
  cluster i=29 m2=1.44 center=(1275,3450,5) extent=(300,50) actor=Ground layout=Ground cells=23 kind=walkin over=none over_layout=-
  cluster i=30 m2=1.44 center=(1650,2050,5) extent=(300,50) actor=Ground layout=Ground cells=23 kind=walkin over=none over_layout=-
  cluster i=31 m2=1.38 center=(938,7600,5) extent=(75,400) actor=GapLand_500 layout=GapLand_500 cells=22 kind=walkin over=none over_layout=-
  cluster i=32 m2=1.25 center=(-375,6962,5) extent=(450,75) actor=Ground layout=Ground cells=20 kind=walkin over=none over_layout=-
  cluster i=33 m2=1.19 center=(1862,8400,5) extent=(75,400) actor=GapLand_700 layout=GapLand_700 cells=19 kind=walkin over=none over_layout=-
  cluster i=34 m2=1.12 center=(1475,-1500,325) extent=(50,400) actor=JumpPadA_Platform layout=JumpPadA_Platform cells=18 kind=walkin over=none over_layout=-
  cluster i=35 m2=1.00 center=(225,2062,5) extent=(200,75) actor=Ground layout=Ground cells=16 kind=walkin over=none over_layout=-
  cluster i=36 m2=1.00 center=(1225,3512,1231) extent=(400,25) actor=RampPlatform_50deg layout=RampPlatform_50deg cells=16 kind=edge over=none over_layout=-
  cluster i=37 m2=1.00 center=(1225,4088,1231) extent=(400,25) actor=RampPlatform_50deg layout=RampPlatform_50deg cells=16 kind=edge over=none over_layout=-
  lowclear_poly i=0 center=(1226,3144,1075) headroom=0 inside=1 actor=RampPlatform_45deg layout=RampPlatform_45deg
  lowclear_poly i=1 center=(1226,2916,1075) headroom=0 inside=1 actor=RampPlatform_45deg layout=RampPlatform_45deg
```
