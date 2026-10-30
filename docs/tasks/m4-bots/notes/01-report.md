# M4-01 nav foundation — agent report (2026-10-30)
build.sh 80 tests (worktree base before M4-02), 0 failures (`20261030-014520-111701`); extra editor build with `-DisableAdaptiveUnity` OK. StrafeRun/PadRun/ItemRun 3/3 on rerun.
- Nav: agent "VeloxPawn" r 37.5 h 140 step 45 slope 45.5, cell 19/10, tile 1000, `RuntimeGeneration=Dynamic`, no client nav. Build L_ItemTest 29 tiles ~1 s, L_MovementTest 117 tiles ~1.5 s; no navmesh actor saved.
- `AVeloxNavLink : ANavLinkProxy` (PointLinks[0] from properties, written in `PostRegisterAllComponents` before Super; snap 60/100; disabled → `UNavArea_Null`); 5 `UVeloxNavArea_*` (config costs), filters Walk/Hop/Full. Path-point link match `FindLinkByStart` / `UVeloxBotSubsystem::FindNearestEnd` (0.0 cm measured).
- `UVeloxBotSubsystem` (authority): nav-ready, `CallWhenNavReady`, link registry, `velox.bots.Seed`, empty bot/projectile registries (typed AController until 04). Cheats `velox.bots.DumpNav`, `velox.bots.DumpPadLandings`. `AVeloxJumpPad::ComputeLandingPoint`; `FVeloxMoveSim::BallisticPoint` (+ test `Velox.Movement.BallisticPoint`).
- Maps: L_ItemTest bounds volume, 15 links (2 pad, 12 drop, 1 rocket jump), third platform (1800,−1000) 350 cm with `Ammo.Rockets` (moved from the grid); L_MovementTest bounds volume, 12 links (pads A/B/C, drops, gap 500 Jump both ways, 700 JumpFast one way, none for 900).
- Evidence: Mega via `links=JumpPad`; rockets platform Full `ok links=RocketJump`, Walk/Hop partial; gap 700 Walk partial, Hop/Full JumpFast; pad landings 19–25 cm from markers.
## Departures (architect)
1. `bWholeWorldNavigable` is commented out in 5.8 (`NavigationSystem.h` l.367–371); Python can spawn `NavMeshBoundsVolume` → one volume per map script. **L_CombatTest has none** → task 05 needs it (assigned to 04).
2. `!SupportedAgents=ClearArray` (not `-`).
3. PointLinks in `PostRegisterAllComponents`.
4. `Ammo.Rockets` moved to the platform (keeps 23 distinct items).
5. Per-bot seed `MasterSeed ^ BotIndex ^ LifeCount` collides → `HashCombine` in 04; `FindNearestEnd` unit row → 04; pad C lands back on the pad, link ends on the ledge (bot steers).
