# 02 — Weapon key during an own switch retargets the switch

**Milestone:** M4 follow-ups (human decision q20, 2026-10-30)
**Agent:** dev-hard
**Status:** done (2026-10-31, dev-hard; architect review pending)
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
- [x] Retarget works in both lower and raise phases; total switch time follows a stated rule (written in the ADR amendment).
- [x] No mispredictions: `infra/ci/nettest.sh --scripts FireRun,FireRunControl --repeat 2` PASS, plus a new or extended nettest script/phase that does rapid key retargets and reports 0 switch corrections.
- [x] `infra/ci/build.sh` CI OK, `infra/ci/fts.sh` 7/7, `infra/ci/nettest.sh --scripts BotDuel --repeat 1` PASS.

## Verification steps
1. `infra/ci/build.sh`
2. `infra/ci/fts.sh`
3. the nettest runs above

## Report back
Standard format. An architect design review follows (GAS/prediction).

## Notes / outcome

**Outcome (2026-10-31, dev-hard).** Rule and mechanism in ADR 0003 **B42**. A weapon key during the own wait/drop retargets the pending switch (drop end and switch end unchanged; back to the lowered weapon re-raises it; same target no-op; unowned ignored); a key during the raise starts a new switch with wait = max(refire, remaining raise) (Q3 weaponTime). The inventory now owns the drop on the owning client too (PendingIndex + PendingDropTimer -> PredictCurrentIndex) and the switch ability ends at activation, so presses during the drop reach `PrepareInputActivation` without touching the ASC input routing. Retarget = same LocalPredicted activation with `EventMagnitude = index + MaxWeapons`; server: pending / late (drop ended <= 0.5 s ago) / fresh (`VeloxFireTiming::ClassifyRemoteRetarget`). Rollback via `ClientRejectSwitch` + the key's rejected delegate; `OnRep_CurrentIndex` ignores echoes of older predictions of the same chain; `VeloxWeaponSwitchCorrection` is logged on any contradiction.

Verification: `build.sh` CI OK (100 tests, new `Velox.Combat.SwitchRetargetRules/Local/Remote`, `Inventory` D4 rows updated); `fts.sh` 7/7; `nettest.sh --scripts FireRun,FireRunControl --repeat 2` PASS 2/2 twice (second run with the final FireRun.json: per round `retarget client=4 server=4 modes=pending:4 switch_corrections=0 raise_switches=2`, rejected=0, act_failed=0); `nettest.sh --scripts BotDuel --repeat 1` PASS. FireRun.json gained the retarget phase; nettest.sh judges it. Out-of-scope touches (needed for the acceptance phase): `Content/Data/MoveScripts/FireRun.json`, `infra/ci/nettest.sh`, one line in `Movement/VeloxScriptedInputComponent.cpp` (`SelectWeaponSlot`: during a drop only the pending target is a no-op).
