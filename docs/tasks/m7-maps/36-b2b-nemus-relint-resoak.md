# 36 (B2b) — Nemus: last lowclear poly, item coverage, re-soak

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** H11 (JUMP gaps), H9 (BotWatch park point), H12 (link_span lint in validator) — all merged
**Wave:** blockout follow-up (lead-added 2026-11-14)

## Goal
Finish B2 (docs/tasks/m7-maps/18-b2-nemus-build.md): (b) one lowclear poly at Hump3_Dn (722,-2508) still fails navlint; (e) item coverage 28–32/41 (untaken: 3 west-log shards behind the terrace JUMP link, 4 small healths on the east aqueduct deck, Ammo_Shells, Health_Medium (2400,2350)) and nav_build > 2 s under load. With H11 the log-gap JUMP links now succeed (40/40); with H9 BotRun has no BotWatch world kills. Fix the lowclear poly (layout), move or re-route the never-taken items to positions on normal routes (concept intent kept), regenerate, re-run navlint, the Nemus fts row, NemusRun and the three soak sets (FFA ×10 at 3 and 6 bots, BotRun ×3).

## Scope
- `Tools/editor/maps/nemus_layout.py`, `Content/Maps/Nemus/L_Nemus_Blockout.umap`, `Tools/editor/maps/golden/nemus.json`, evidence `docs/adr/evidence/0008-nemus-blockout/`, the B2 task file notes, this file

## Acceptance criteria
- [ ] navlint PASS (clusters 0, lowclear 0), nav build ≤ 2.0 s in navlint
- [ ] FFA ×10 at 3 and 6 bots: item coverage ≥ 39/41, all map-independent invariants PASS, JUMP ≥ 90 %; BotRun ×3 behaviour PASS (cost windows: host-load rule)
- [ ] NemusRun PASS; FtMapReachability Nemus PASS; DM1 golden OK; B2 (b) and (e) ticked if passing

## Notes / outcome
(Filled by the agent.)
