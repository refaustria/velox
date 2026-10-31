# 02 — Weapon key during an own switch retargets the switch

**Milestone:** M4 follow-ups (human decision q20, 2026-10-30)
**Agent:** dev-hard
**Status:** todo
**Depends on:** ADR 0003 (weapon switch / GAS), STATUS question 20

## Goal
Q3 behaviour: pressing another weapon key while the own weapon switch is still in progress retargets the switch to the newly requested weapon (instead of being ignored or queued after the first switch completes). Must stay client-predicted and server-authoritative with no visible correction, and bots (which use the same input path) must keep working.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/` — switch ability / inventory switch state
- `Source/Velox/Combat/` only where the switch ability lives there
- `Source/Velox/Tests/` — new automation rows for retarget (predicted + authoritative), including: retarget to the weapon being switched away from, retarget to the same target (no-op), retarget to an unowned weapon (ignored), retarget during the lower/raise phases
- `docs/adr/0003-*.md` — amendment describing the rule
(GAS structure changes beyond the switch ability: report back for an architect pass.)

## Acceptance criteria
- [ ] Retarget works in both lower and raise phases; total switch time follows a stated rule (written in the ADR amendment).
- [ ] No mispredictions: `infra/ci/nettest.sh --scripts FireRun,FireRunControl --repeat 2` PASS, plus a new or extended nettest script/phase that does rapid key retargets and reports 0 switch corrections.
- [ ] `infra/ci/build.sh` CI OK, `infra/ci/fts.sh` 7/7, `infra/ci/nettest.sh --scripts BotDuel --repeat 1` PASS.

## Verification steps
1. `infra/ci/build.sh`
2. `infra/ci/fts.sh`
3. the nettest runs above

## Report back
Standard format. An architect design review follows (GAS/prediction).

## Notes / outcome
