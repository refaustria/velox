# 01b — GAS foundation fixes from the architect review

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** done (2026-10-25)
**Depends on:** 01 (review `docs/reviews/m2-01-gas-foundation-review.md`, ADR 0003 Amendments B1–B9)

## Goal
Apply defects 1–8 of the review (§4 of the review file is the authoritative list with file:line and fixes): Q3 armor rule as exact integer ceiling `Save = min(Armor, Damage, (Damage×66 + 99)/100)` with `ArmorProtectionPercent = 66` (B2) and the corrected test table; `Apply` only early-outs for `Damage < 0`, zero-point hits deal 1 damage without knockback (B3); ability input processed in a `PlayerTick` override after `Super` (B6), fire abilities will sample `GetPawnViewLocation()` + `GetControlRotation()`; authority guard on the meta commit; grant-once honours a changed PawnData and keeps handles in a `UPROPERTY` (B9); settings preloaded in `PostInitializeComponents` (B1); dead/input-blocked gate on `ProcessAbilityInput` (`Velox.State.Dead`); comment fix (B7).

## Scope — files/modules the agent may touch
- `Source/Velox/Combat/VeloxDamageMath.h`, `VeloxCombatSettings.*`, `VeloxHealthSet.*`, `VeloxAbilitySystemComponent.*`, `VeloxGameplayEffects.*`
- `Source/Velox/GameModes/VeloxPlayerState.*`, `VeloxPlayerController.*`
- `Source/Velox/Tests/VeloxCombatTests.cpp`, `Tools/editor/create_bootstrap_content.py` (`armor_protection` value)

## Acceptance criteria
- [ ] `velox.combat.TestDamage 100 100` → `health 125->91 armor 100->34`; `TestDamage 50 100` → 33 armor / 17 health; multiples of 50 match Q3 exactly.
- [ ] Review defects 1–8 closed; `infra/ci/build.sh` green (31 tests, updated assertions); M1 soak 1 round unchanged.

## Report back
Standard dev-hard format; list each defect with the change.
