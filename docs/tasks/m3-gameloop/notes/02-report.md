# M3-02 powerups — agent report (2026-10-29)
build.sh 58 tests in the worktree (+ `Velox.Items.PowerupStacking`), 0 failures (`20261029-211642-3842947`). Headless `velox.items.PowerupTest quit` on L_CombatTest PASS (MG 7 → with Quad 21; Regen 50→65→80→95 per second). Stacking: 30 s, +30 at 20 s remaining → 50 s, same handle, no cue restart. RocketJumpRun 1/1 C=0 (regression check, loaded machine).
- `Items/VeloxPowerupEffects.*`: abstract `UVeloxGE_Powerup` (SetByCaller.Duration, no stacking, tag `Velox.Powerup.<Name>`, looping cue `GameplayCue.Powerup.<Name>`), Regen via `UVeloxRegenExecution` every 1 s; `VeloxPowerups::GrantOrExtend` (apply, else `ModifyActiveEffectStartTime(Handle, Total − remaining)`, fallback remove + re-apply); cheats `velox.items.PowerupTest`, `velox.items.GivePowerup <Name> [s]`.
- Quad at fire time: hitscan `DamageScale` via `FVeloxDamageMath::ScaleDamage` (truncation); projectiles `bQuad` initial-only, scales direct + splash base before falloff (Q3), move-timed self splash uses the same flag on both ends.
- Battle Suit in `FVeloxDamageMath::Apply`: knockback from raw damage, radius → 0/0, else ×0.5 before the self halving. `FVeloxDamageInput` gains `bBattleSuit`, `bRadius`.
- Regen `RegenStep` world-free; decay skipped under Regen (armor still decays); no ticks while dead/frozen. `GE_SpawnInit` removes `Velox.Powerup` effects.
- MatchFrozen: tag by name via `VeloxEffects::GetMatchFrozenTag()`; frozen = dead in the damage execution; fire `CanActivateAbility` refuses. `AVeloxProjectile::Reset()` destroys and drops pending self splash.
- Tint: `UVeloxCombatFeedbackComponent` sets `PowerupTint` (Quad > BS > Regen > Haste), colours `UVeloxCombatSettings::PowerupTints`; invisible until `M_PlayerBody` gets the `PowerupTint` parameter.
- Tunables: `QuadDamageFactor 3`, `BattleSuitDirectDamageScale 0.5`, `RegenHealthPerTick 15`, `RegenOverTick 5`, `RegenSoftCapScale 1.1`, `HasteFireRateScale 1.3`.
- `PowerupEffect` narrowed to `TSubclassOf<UVeloxGE_Powerup>`; `GrantPowerup` → `GrantOrExtend`; content script sets `powerup_effect`.
- Engine facts: `ModifyActiveEffectStartTime` AbilitySystemComponent.h:830/.cpp:1791, container GameplayEffect.cpp:5674, CheckDuration :5474; CapturedSourceTags GameplayEffect.h:1232.
## Open
- ADR 0004 §10 row "quad self rocket 1250×3" is wrong: knockback cap 200 dmg → 2500 cm/s (Q3); architect to fix the ADR.
- After 05 merged: `GetMatchFrozenTag()` → `VeloxMatchTags::State_MatchFrozen`; add MatchFrozen to fire `ActivationBlockedTags`, drop the CanActivate check; enable the frozen part of `PowerupStacking`.
- `M_PlayerBody` needs a `PowerupTint` vector parameter (bootstrap script).
- Owner's fake projectile / predicted self splash use the local Quad tag (edges: ≤ RTT/2 at pickup, ≤ RTT at expiry → one correction on a quad rocket jump); task 03 replaces with the move-time domain.
- Hand-offs 03: `Launch.bQuad = IsPowerupActive(QuadEnd, FireMoveTimeStamp)`; `ServerRefreshPowerupMoveTimes` after `GrantOrExtend` (and in `GivePowerup`). 07: remaining via `VeloxPowerups::GetRemainingSeconds`; show remaining, not a ratio; tint via feedback component. 04: Quad row of FT_ItemPickup can reuse the calls.
