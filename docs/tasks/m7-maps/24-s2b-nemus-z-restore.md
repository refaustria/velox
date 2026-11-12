# 24 (S2b) — Nemus back on the concept heights (pure Python)

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** done (pure Python; engine gate deferred)
**Depends on:** S2 (13), P6 (23)

## Goal
S2 lowered every Nemus Z by 320 (deviations D1/D2) because spawns and pads could only stand at `FLOOR_Z`. P6 added `Pad.z` / `Spawn.z`. Restore the concept heights of `docs/maps/nemus.md` (pit 0, tiers 80/160/240, forest floor 320, ruin ring 640, Corona 960, clip ceiling 2000) so ADR 0006's lowest-floor-at-0 holds again, put pad P1 back on the Orchestra floor per the concept, and undo any other S2 deviation that only existed for lack of `z`.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/nemus_layout.py`
- this file and the notes of `docs/tasks/m7-maps/13-s2-nemus-spec.md`

## Relevant ADRs / references
- `docs/adr/0006` (E0.3 e), `docs/adr/0008-m7-five-maps.md`, `docs/maps/nemus.md`, `docs/tasks/m7-maps/23-p6-generator-gaps.md`

## Acceptance criteria
- [x] `create_map.py --map nemus --validate-only` and `--selftest` green
- [x] `--mode art --validate-only`: 0 protrusions
- [x] `create_map.py --map dm1 --golden check` OK
- [x] Pad landings vs the concept table in the notes
- [x] Concept section 17.1 LOS still holds

## Verification steps
1. `python3 -I Tools/editor/create_map.py --map nemus --validate-only`, `--selftest`, `--mode art --validate-only`, `--map dm1 --golden check`.

## Report back
Standard format.

## Notes / outcome
Datum: `PIT, T1, T2, T3, F, U, C = 0, 80, 160, 240, 320, 640, 960`, `SKY`/`CLIP_H` 2000, `FLOOR_Z` 0 (the pit). Every Spawn (11) and pad P2..P5 carries `z=F`; P1 is `Pad("P1", -300, 0, (-1950, 0, 1050), (-2922, 0, 640), PIT)` with the concept target. Pad link starts use the pad's own z. Pad apex targets are the concept's (P2/P3 820, P4 1160, P5 800). Tree wall is `F..F+1200`, hump health on `F + HUMP`, nav bounds, Lightmass volume and intermission camera moved up by 320. Nothing else changed: all heights relative to each other are as in S2.

Deviations undone: D1 (datum) and D2 (P1 on the rim). Kept: D3 (tree 30 cm west, Oct gap rule), D4 (Quad on the pit floor, no altar; LOS), D5 (spawn moves, LOS), D6, D7 (box shapes / bridge kinds), D8 (drop count), D9, D10. None of those depended on the missing z.

**Pad landings (simulated `pad_arc` vs concept table).**

| Pad | Concept landing | Simulated | Delta |
|---|---|---|---|
| P1 (pit) | (-2922, 0, 640) | (-2925, 0, 637.7) | 3 cm horizontal |
| P2 | (-2899, 1823, 640) | (-2899, 1823, 639.9) | 0 |
| P3 | (-2899, -1823, 640) | (-2899, -1823, 639.9) | 0 |
| P4 | (3110, 369, 960) | (3110, 366, 957.4) | 3 cm |
| P5 | (1800, -1789, 640) | (1800, -1788, 639.2) | 1 cm |

**LOS (section 17.1).** spawns -> Quad / RA / Mega 0 % visible, Corona -> RA 0 %, unchanged; longest sightline 61.2 m; RJ clearance 1360 / 1360 / 1680. Art plan 600 kit instances, 0 protrusions; DM1 golden OK; validate-only and selftest green. Nothing run in the engine.
