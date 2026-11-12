# 25 — Fornax spawns back to the concept (S4b)

**Milestone:** M7 Maps
**Agent:** dev
**Status:** done
**Depends on:** 15-s4 (Fornax layout), 23-p6 (Spawn z, Link both_ways)

## Goal
S4 moved spawns S1 and S5 from L1 to L0 and shifted S2 / S4 because `Spawn` had no z. P6 added `Spawn(..., z=)`. Put the spawns back on the positions of `docs/maps/fornax.md` section 6 where the generator rules allow, and use `Link(both_ways=)` where it is behaviour-identical.

## Scope — files/modules the agent may touch
- `Tools/editor/maps/fornax_layout.py` — SPAWNS (and link pairs if both_ways is identical)
- this file

## Relevant ADRs / references
- `docs/maps/fornax.md` section 6, `docs/tasks/m7-maps/15-s4-fornax-spec.md`, `23-p6-generator-gaps.md`

## Acceptance criteria
- [ ] `python3 -I Tools/editor/create_map.py --map fornax --validate-only` and `--selftest` green
- [ ] DM1 golden OK; hazard rules green
- [ ] Spawn table versus concept in the notes

## Verification steps
1. The two create_map.py commands above; the DM1 golden check of the selftest.

## Report back
Standard dev report.

## Notes / outcome
Spawn table (concept docs/maps/fornax.md section 6 versus now):

| Id | Concept | S4 (before) | Now | Note |
|---|---|---|---|---|
| S1 | -1700,1200,320 | -800,-1100,0 | S1_TapN 1000,-500,0 | concept impossible: every Flue point sees the Mega down the open Flue (50 cm grid scan); no West Landing point meets weapon + spacing rules. Old Slag Run spot is 4.3 m from the concept S2, so S1 moved to the Tap Room north strip (PG 6.4 m, 10+ m from the others, no major in sight) |
| S2 | -650,-700,0 | 150,-750,0 | -650,-700,0 | concept restored (RL 3.8 m) |
| S3 | 500,500,0 | same | same | unchanged |
| S4 | 450,-1350,0 | 1000,-1300,0 | 450,-1350,0 | concept restored (PG 3.8 m) |
| S5 | 2100,-450,320 yaw 90 | 1750,-600,0 | 2100,-450, z 320, yaw 90 | concept restored via Spawn.z |

Link both_ways: not used. The lava hops are asymmetric pairs (start 60 before the take-off edge, end 100 onto the landing, so the reverse hop starts and ends at different points); one both_ways link would change the endpoints, so it is not behaviour-identical. Link counts unchanged.

Verification: `create_map.py --map fornax --validate-only` (layout valid, 5 spawns, 20 links, spawns -> majors 0 % visible), `--map fornax --selftest` and `--map dm1 --selftest` OK.
