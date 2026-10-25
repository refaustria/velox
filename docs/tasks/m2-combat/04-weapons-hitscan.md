# 04 — Weapon definitions, inventory, fire/switch abilities, hitscan weapons

**Milestone:** M2 Combat
**Agent:** dev-hard
**Status:** todo
**Depends on:** 01, 01b, 03 (03b may run in parallel; 04 does not depend on its fixes)

## Goal
Implement ADR 0003 implementation-notes task **`04-weapons-hitscan`** (`docs/adr/0003-combat-gas-and-lag-compensation.md`). The ADR sections are the specification; this file pins scope and acceptance. ADR 0002 amendments A17 (recorded knockback channel), A24 (echoes), A32/A33 (events over moves vs RPCs) are binding.

## Scope — files/modules the agent may touch
- `Source/Velox/Weapons/` (new: VeloxWeaponDefinition, VeloxWeaponTable, VeloxWeaponInventoryComponent, VeloxGameplayAbility_Fire*, _Switch, VeloxFireRateBucket, target data), `Source/Velox/Combat/VeloxAbilitySystemComponent.*` (route InputTag.Fire to current weapon), `Core/VeloxGameplayTags.*`, `Tools/editor/create_bootstrap_content.py` (6 DA_Weapon_*), tests

## Acceptance criteria
- [ ] MG, SG (pellet pattern), LG (beam, tick damage), RG (instant rail) damage a dummy headless; fire-rate token bucket rejects 0 legitimate shots and all over-rate shots; `ClientConfirmHit` hit markers.
- [ ] `Velox.Combat.FireRateBucket` green; shots travel over GAS target-data RPCs, never over moves; architect review.
- [ ] `infra/ci/build.sh` green (all `Velox.*` tests); M1 soak unaffected where the ADR says so.

## Review notes from task 01 (architect, 2026-10-25; ADR 0003 amendments B1–B9 are binding)
- **Prerequisite:** follow-up `01b` (armor constant B2, input call site B6) must be merged first; the acceptance numbers below assume ⌈0.66 d⌉ (100 vs 100 armor → 66/34).
- **B6:** ability input is processed in `AVeloxPlayerController::PlayerTick` after `Super` (after `UpdateRotation`), not in `PostProcessInput`. Fire abilities take the origin from `Pawn->GetPawnViewLocation()` and the direction from `Controller->GetControlRotation().Vector()`; never from the camera manager. `ViewServerTime` and the hint stamp are sampled there too.
- **B5:** delete `FVeloxAmmoTypeInfo::SpawnAmount`; `VeloxEffects::MakeSpawnInitSpec` sums `UVeloxCombatSettings::SpawnLoadout[].Ammo` per `Def->AmmoTag`; `ResetToLoadout` grants from the same list. Update `Velox.Combat.Ammo` and the bootstrap script accordingly.
- Fire filter: implement `UVeloxAbilitySystemComponent::CanRouteInputToSpec` (hook exists) — for `InputTag.Fire` only the spec whose `SourceObject` is the inventory's current `UVeloxWeaponDefinition`. `ProcessAbilityInput` gates on `Velox.State.Dead` (01b); fire abilities also carry `ActivationBlockedTags = {Velox.State.Dead, Velox.State.WeaponSwitching}`.
- Damage specs go through `VeloxEffects::MakeDamageSpec(SourceASC, FVeloxDamageSpecParams)` (`Combat/VeloxGameplayEffects.h`) — `Damage = Def.Damage`, `PelletHits = n`, `Direction = Dir`, `KnockbackScale`, `ShotSeq`, `DamageTag`; nothing else builds `GE_Damage`.
- The LG beam is a WhileActive cue; it must be removed on death/unpossess (task 06 calls `RemoveAllGameplayCues` before clearing the avatar, B8) and on weapon switch.

## Review notes from task 03 (architect, 2026-10-25; ADR 0003 amendments B10–B17 are binding; review `docs/reviews/m2-03-lag-compensation-review.md`)
Exact call sequence for `UVeloxGameplayAbility_FireHitscan` (public API: `Net/VeloxLagCompensationSubsystem.h`, `AVeloxPlayerController::BuildShotTiming`).

**Client (autonomous proxy), inside `ActivateAbility` after `CommitAbility`, within the activation's `FScopedPredictionWindow`:**
1. `Origin = Pawn->GetPawnViewLocation()`, `Dir = Controller->GetControlRotation().Vector()` (B6; never the camera manager).
2. `FVeloxShotTiming Timing = VeloxPC->BuildShotTiming(Origin, Dir, Def->RangeCm)` — **once per shot with the centre ray**, also for the shotgun (B14). On a listen host / for bots the same call returns `ViewServerTime = Now` and no hint; the ability code does not branch on it.
3. Cosmetic resolution only: per pellet `FVeloxHitMath::RayPose` against `UVeloxHitboxComponent::GetRenderedPose()` of simulated proxies plus a world `LineTraceSingleByChannel(Velox_TraceChannel_Weapon)`; pellet directions from `FRandomStream(ShotSeq ^ PlayerId)` around `Dir`. Never call the subsystem on a client (it may not exist, and is empty when it does — B10).
4. `FGameplayAbilityTargetData_VeloxShot { FVector_NetQuantize100 Origin; FVector_NetQuantizeNormal Dir; float ViewServerTime; uint8 ShotSeq; bool bHasHint; TWeakObjectPtr<AActor> HintTarget; float HintTargetStamp; }` — `NetSerialize` writes `HintTarget` through `Map->SerializeObject` (one bit `bHasHint` first), `ViewServerTime` as float (1 ms resolution within a match), stamp as float (must round-trip exactly: the server compares stamps as float, B12). Copy from `Timing`: `ViewServerTime`, `HintTarget`, `HintTargetStamp` (`bHasHint = HintTarget.IsValid()`).
5. `CallServerSetReplicatedTargetData(Handle, ActivationInfo, TargetDataHandle, FGameplayTag(), ScopedPredictionKey)`; then predicted `GE_AmmoCost`, `ExecuteGameplayCueWithParams(FireCueTag)` locally, `NextFireTime = Now + FireInterval`, `EndAbility`. **No predicted hit marker.**

**Server (`AbilityTargetDataSetDelegate` → `OnTargetDataReady`), in this order:**
1. `ConsumeClientReplicatedTargetData`. Reject (log `VeloxShotRejected seq= reason=`, `EndAbility` without firing) when: the target data type is wrong; `Origin.ContainsNaN()`; `Dir` not finite or `IsNearlyZero()` (normalise otherwise); `!FMath::IsFinite(ViewServerTime)` or `!FMath::IsFinite(HintTargetStamp)`; the `FVeloxFireRateBucket` of this weapon instance is empty (checked **first**, it is the cheapest). Do not touch the subsystem for a rejected shot.
2. Build the request: `FVeloxShotRequest Req; Req.Shooter = Avatar; Req.Origin = Origin; Req.RangeCm = Def->RangeCm; Req.bPenetratePawns = Def->bPenetratesPawns; Req.MaxPenetrations = Def->MaxPenetrations; Req.ShotSeq = ShotSeq;` pellets: `Req.Dirs` from the **server's own** `FRandomStream(ShotSeq ^ PlayerId)` around `Dir` (`PelletCount`, `SpreadAngleDeg`; one entry = `Dir` for MG/LG/RG). `Req.ApplySettings(PlayerState->GetCombatSettings())` (B1: tolerances from the shooter's settings asset), then `Req.ViewServerTime / HintTarget / HintTargetStamp` from the target data (or `Req.ApplyTiming(FVeloxShotTiming{…})`). Leave `bValidateOrigin = true`.
3. `UVeloxLagCompensationSubsystem::Get(World)->TraceShot(Req, Result)`. Null subsystem (should not happen on a server) → log and `EndAbility`.
4. For each `Result.Hits[i]` (nearest first): victim ASC via `UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Hit.Pawn)`; `VeloxEffects::MakeDamageSpec(SourceASC, { Damage = Def->Damage, PelletHits = Hit.PelletHits, DamageTag = Def->DamageTag, Direction = Dir, HitLocation = Hit.Location, KnockbackScale = Def->KnockbackScale, ShotSeq })` → `ApplyGameplayEffectSpecToTarget`. A victim killed by hit *i* is already unregistered (B8/B17) but `Result` was built before — apply the remaining hits anyway (a rail through two pawns kills both).
5. `GE_AmmoCost` on the own ASC; `ExecuteGameplayCueWithParams(FireCueTag)` with the activation prediction key; impact cue: one `ImpactCueTag` per *ray end* for single-ray weapons (`Result.Rays[0].End`, `ImpactNormal`), for the shotgun one cue at the centre ray's end only (clients predicted their own pellet impacts); `ClientConfirmHit(ShotSeq, TotalDamage clamped to 255, Flags{Kill})` when `Result.Hits.Num() > 0`; `EndAbility`.
6. Server-side local activation (listen host, bots in M4): skip the target-data RPC and run steps 2–5 directly with `BuildShotTiming`'s server result (`ViewServerTime = Now`, no hint).

Other constraints from the review: `Result.RewindMs`/`HintMode` are for the `VeloxShot` log the subsystem already writes — do not log a second shot line; 03b adds `view_ms`. The LG is 20 shots/s per player through this exact path (ADR §5); do not add per-shot allocations in the ability (keep `Req.Dirs` on the stack via its inline allocator; the shotgun's 11 entries spill once per shot, acceptable).

## Report back
Standard format for the agent type; dev-hard includes a design summary for `architect` review.
