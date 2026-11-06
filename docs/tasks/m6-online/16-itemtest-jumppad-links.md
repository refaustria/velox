# 16 — L_ItemTest JumpPad links 0/4: explain and fix the miss rate

**Milestone:** M6 Online (bot/map follow-up from ADR 0007 F2)
**Agent:** dev
**Status:** todo
**Depends on:** ADR 0007 F2 ruling 1, ADR 0006 E0.3 (b) ("a pad has no reason to miss"), ADR 0005 (link outcomes)

## Goal
The bots' JumpPad link rate on `L_ItemTest` is ≥ 95 % because every miss on `VeloxNavLink_0` / `VeloxNavLink_4` is explained from the landing offset and the pads or link endpoints are corrected — not the 90 % threshold or the runner.

## Context
Gate run 2026-11-06: all JumpPad misses are `reason=missed` on those two links in dedicated (23/25), listen (35/39) and packaged (22/25); pooled 186/200 = 93.0 %. Evidence: `docs/adr/evidence/0007-listen-gate/gate-20261106/`.

## Scope — files/modules the agent may touch
- `L_ItemTest` generation (the map library / generator script that places the pads and nav links), its nav-link endpoints, pad launch data if per-pad
- `infra/ci/` only for a diagnostic print (landing point vs link end) if missing
- Never `Source/Velox/Movement/` or the lag-compensation path; no threshold changes

## Acceptance criteria
- [ ] A table per miss: link, launch point, predicted vs actual landing, offset to the link end, cause.
- [ ] Fix applied; navlint and the map's FT still pass.
- [ ] BotDuel ×3 + BotRun ×3 on L_ItemTest (dedicated, idle host): JumpPad ≥ 95 % observed, every set PASS.
- [ ] Editor build + all automation tests green.

## Notes / outcome
