# M4-04b report (dev-hard, 2026-10-30)

Result: done. Merged at 82e732e.

- Post-merge baseline (05+06): build.sh OK, 95 tests, fts 4/4 PASS.
- D4: ReactionJitter (0.3) is a preset field. Eight fight/maneuver tunables are in UVeloxBotSettings + DefaultGame.ini.
- Integration: FillEnemyInput uses GetCombatState; Fight choices become real Fight goals; visible/lost target triggers re-evaluation; NotePathQuery counts every FindPathSync, RecordBotTick takes a count.
- Corner stuck: nav AgentRadius 37.5 -> 45. Gaps < 90 cm lost their mesh, so off-mesh recovery is now needed in practice.
- Off-mesh recovery: after 1 s off the mesh on the ground, ring search 150/300/600/1000 cm x 12 directions, straight walk, 4 s cap.
- infra/ci/fts.sh: 5 FTs (adds FtOffMesh). 3x `OK 5/5`.
- Final build: 95 tests green; -DisableAdaptiveUnity build OK.
- FFA Medium 3x120 s: stuck 0/0/0, off-mesh 0/1/0/1, all recovered. Duel Hard 3x180 s: no unrecovered stranding, Mega taken 4/3/3.
- Budget: FFA mean 49-65 us, max_path 255-569 us, 11-15 paths/s, over=0.

## Open (for architect review)

1. Fight goals dominate FFA: FightBaseScore 20 x FightItemBias 1.5 = 30 beats almost every item (1.5-25). Pickups/run dropped from 69-107 to 14-21. Needs a D4 balance ruling.
2. Ramp-top strip: the exit runs under the ramp slab; a bunny-hopping bot hits the slab and oscillates without a stuck strike. Needs a low-ceiling hop guard in the follower.
3. FtOffMesh is a fifth FT (task asked for 4/4).
4. One FtItemRouting run took 542 frames / 40 queries (probably fighting the local player).
