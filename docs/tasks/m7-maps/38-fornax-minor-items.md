# 38 — Fornax minor items onto routes + coverage re-soak

**Milestone:** M7 Maps (ADR 0008)
**Agent:** dev
**Status:** open
**Depends on:** H15 (merged)
**Wave:** blockout follow-up (lead-added 2026-11-14)

## Goal
Fornax is done except item coverage in FFA ×10 at 3 bots: 27/30 (B4), 28/30 (B4b), 26/30 (H15). Only small minor items are missed (Health_Small, some Armor_Shard on the north stairs / Tap ramp), different ones per set. Move the never/rarely taken minor items onto positions on normal routes (concept intent kept, as B2b did for Nemus), regenerate, re-run navlint, the Fornax fts row, FornaxRun and one FFA ×10 at 3 bots + BotDuel ×3.

## Scope
- `Tools/editor/maps/fornax_layout.py`, `Content/Maps/Fornax/L_Fornax_Blockout.umap`, `Tools/editor/maps/golden/fornax.json`, evidence `docs/adr/evidence/0008-fornax-blockout/`, the B4 task file notes, this file

## Acceptance criteria
- [ ] FFA ×10 at 3 bots: coverage 30/30 (or 29/30 with the one miss named and on a route), all invariants PASS, hazard gate PASS, all JUMP links ≥ 90 %
- [ ] BotDuel ×3 PASS; navlint, fts row, FornaxRun green; DM1 golden OK; B4 (e) ticked if passing

## Notes / outcome
(Filled by the agent.)
