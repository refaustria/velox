# 01 — Nav foundation: runtime nav mesh, Velox nav links, areas, filters, bot subsystem

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** todo
**Depends on:** ADR 0005 §3, §4, §12 (subsystem registries only)

## Goal
The server builds a Recast nav mesh at map load for the greybox maps (no editor bake), `AVeloxNavLink` actors describe jump pads, drops, gap jumps and rocket jumps with per-type costs, query filters express which link types a bot may use, and `UVeloxBotSubsystem` exists with the nav-ready flag and the link registry. After this task a path query from a PlayerStart to the Mega platform of `L_ItemTest` crosses the pad link. No bot moves yet.

## Scope — files/modules the agent may touch
- `Config/DefaultEngine.ini` — the `NavigationSystemV1` and `RecastNavMesh` sections of ADR 0005 §3 (verify key names against the 5.8 `BaseEngine.ini`; the per-resolution `NavMeshResolutionParams` is the live struct)
- `Source/Velox/Bots/VeloxNavLink.{h,cpp}` (new), `Bots/VeloxNavAreas.{h,cpp}` (new), `Bots/VeloxNavFilters.{h,cpp}` (new), `Bots/VeloxBotSubsystem.{h,cpp}` (new: nav-ready, link registry, `FindLinkByStart`, master seed, `velox.bots.Seed`; bot and projectile registries as empty stubs for 04/05), `Bots/VeloxBotLog.{h,cpp}` (new, `LogVeloxBots`), `Bots/VeloxBotCheats.cpp` (new: `velox.bots.DumpNav`, `velox.bots.DumpPadLandings`)
- `Source/Velox/Movement/VeloxJumpPad.{h,cpp}` — `ComputeLandingPoint(const UWorld*)`, `Trigger->SetCanEverAffectNavigation(false)`; `Movement/VeloxMoveSim.{h,cpp}` — `BallisticPoint(Origin, V0, GravityZ, T)`
- `Source/Velox/Items/VeloxPickup.cpp` — `Trigger->SetCanEverAffectNavigation(false)` (one line)
- `Tools/editor/create_item_test_map.py` — links (pad ×2, drops at every platform edge, one `RocketJump`), the third platform (350 cm, 400 × 400, no pad, `Ammo.Rockets`, label), updated actor-count asserts; `Tools/editor/create_movement_test_map.py` — pad links (A/B/C), drop links at the pad platforms, gap links (500 → `Jump`, 700 → `JumpFast`, none for 900), updated asserts; `Tools/editor/README.md` — link conventions
- `Content/Maps/L_ItemTest.umap`, `Content/Maps/L_MovementTest.umap` (regenerated)
- `Source/Velox/Tests/VeloxJumpPadTest.cpp` — `BallisticPoint` rows
(Anything not listed is out of scope. If the task needs more, report back instead of expanding.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §3 (nav config, runtime Dynamic generation, relevance of triggers), §4 (link class, areas, costs, filters, placement, landing point), §12 (subsystem), §14 (`DumpNav`)
- `docs/adr/0002-movement-component-design.md` §4 (jump pads, A21 landing caveat), §7 (`L_MovementTest` layout)
- `docs/adr/0004-game-loop-items-modes-ui.md` §8 (`L_ItemTest` layout)
- `Tools/editor/README.md` (Python API lessons: struct kwargs, rotators, collision profiles)

## Acceptance criteria
- [ ] `Velox L_ItemTest -server -nullrhi` logs the nav build finished (`VeloxNav built tiles=<n> seconds=<t>`, t < 5 s on this VM) with no nav-relevant trigger exported (pads/pickups walkable).
- [ ] `AVeloxNavLink` with `EVeloxNavLinkType`, properties per ADR §4, writes `PointLinks[0]` (snap radius 60, snap height 100, direction per link), area class per type, registers with `UVeloxBotSubsystem`; `bEnabled=false` switches the area to `UNavArea_Null` and updates the octree.
- [ ] Nav areas `UVeloxNavArea_JumpPad/_Drop/_Jump/_JumpFast/_RocketJump` with the ADR §4 costs; filters `UVeloxNavFilter_Walk/_Hop/_Full` with the stated exclusions.
- [ ] `velox.bots.DumpNav x y z x y z [Walk|Hop|Full]` prints the path points with `link=<type>` on link segments (`FNavMeshNodeFlags::IsNavLink()` + `FindLinkByStart`): on `L_ItemTest` PlayerStart_1 → Mega item resolves a `JumpPad` segment; PlayerStart_1 → the rockets platform item is a full path only with `Full` and partial/none with `Hop`/`Walk`; on `L_MovementTest` the 500 cm gap is crossed with `Hop` and `Walk`, the 700 cm gap only with `Hop`/`Full`, the 900 cm gap never.
- [ ] `AVeloxJumpPad::ComputeLandingPoint` returns a point on the landing platform for pad A of `L_MovementTest` (within 100 cm of the authored marker) and the two pads of `L_ItemTest`; `velox.bots.DumpPadLandings` prints them; `VeloxMoveSim::BallisticPoint` unit-tested (apex at `t = Vz/|g|`, symmetric landing).
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1–M3 `nettest.sh` default list unaffected (maps regenerated: run `StrafeRun,PadRun,ItemRun` once).

## Verification steps
1. `UnrealEditor-Cmd Velox.uproject -run=pythonscript -script=<abs>/Tools/editor/create_movement_test_map.py -nullrhi -unattended …` then the item map script; both assert counts.
2. `UnrealEditor-Cmd Velox.uproject L_ItemTest -server -nullrhi -log -ExecCmds="velox.bots.DumpNav -2700 -600 100 -2300 1200 370, velox.bots.DumpPadLandings" …` → log lines as above.
3. `infra/ci/build.sh`; `infra/ci/nettest.sh --port <N> --scripts StrafeRun,PadRun,ItemRun`.

## Report back
Standard dev-hard format with a design summary for `architect` review (nav config values chosen, build time, how link segments are matched, any engine fact that differed from the ADR).

## Notes / outcome
(Filled by the agent or the lead after completion.)
