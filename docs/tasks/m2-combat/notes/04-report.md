# Task 04 agent report (verbatim summary, 2026-10-25)

Commit 12a5221 on `m2-combat`. build.sh green, 39 tests (34 + 5 new), nettest full list 2/2 on `--port 7795`.

## Design as built
- One `UVeloxWeaponDefinition` per weapon in `/Game/Weapons/DA_Weapon_*`; `DA_WeaponTable` lists them in slot order, table index = net id.
  MG slot 2 0.1 s 7 dmg spread 1.399° range 20480 cm; SG slot 3 1.0 s 11×10 spread 4.884°; RL slot 5 0.8 s 100 direct, splash 100/300 cm, 2250 cm/s, 15 s; LG slot 6 0.05 s 8 dmg 1920 cm beam; RG slot 7 1.5 s 100 dmg penetrates ≤4 pawns; PG slot 8 0.1 s 20 direct, splash 15/50 cm, 5000 cm/s, 10 s.
- MG/SG/LG/RG use `UVeloxGameplayAbility_FireHitscan`. RL/PG have no fire ability yet (task 05).
- Settings: `WeaponTable`, `SpawnLoadout` (MG/100). B5 done: `SpawnAmount` deleted, `GE_SpawnInit` sums loadout ammo.
- `UVeloxWeaponInventoryComponent` on PlayerState: `uint16 OwnedMask` (owner only), replicated `uint8 CurrentIndex`. Owning grants the fire ability (SourceObject = definition, tag InputTag.Fire). One switch ability spec bound to Slot1..9/Next/Prev. `ResetToLoadout` in `AVeloxPlayerState::OnPawnPossessed` before `GE_SpawnInit`. Owner shows predicted switch until replicated index arrives (1 s timeout). `CanRouteInputToSpec` passes Fire only to the current weapon's spec.
- Client shot: CommitAbility → origin `GetPawnViewLocation`, dir from control rotation → `BuildShotTiming` once (centre ray) → target data {Origin NetQuantize100, Dir NetQuantizeNormal, float ViewServerTime, uint8 ShotSeq, hint bit, HintTarget via package map, float HintTargetStamp} → `CallServerSetReplicatedTargetData` → predicted `GE_AmmoCost` → cosmetic rays vs rendered proxies + world trace → local Fire/Impact cues (SG one per pellet) → `NextFireTime` → end without end RPC.
- Server shot: on `AbilityTargetDataSetDelegate`: bucket check first, then type/NaN/dir/finite-time checks; rejects log `VeloxShotRejected seq= reason=`. Rays from `FRandomStream(ShotSeq ^ PlayerId)`, `TraceShot`, one `MakeDamageSpec` per hit (PelletHits=n, Direction=Dir). Then `GE_AmmoCost`, cues under the activation prediction key, `ClientConfirmHit(seq, dmg capped 255, Kill)`. Listen host/bots resolve directly.
- `FVeloxFireRateBucket`: refill 1/FireInterval per s on server receive time, capacity `FireRateBurstAllowance`=3, full at first use, one per weapon ability instance.
- Cues native: `AVeloxCharacter::HandleGameplayCue` → `UVeloxCombatFeedbackComponent` (debug draws, beam state, hit-confirm state). `UVeloxGameplayCueManager` in DefaultGame.ini. LG beam: WhileActive cue started server-side without prediction key; inventory keeps it alive 2.5 intervals after last shot, stops on timeout/weapon change; death needs `RemoveAllGameplayCues` (task 06). Keep-alive is in the inventory because EndAbility clears ability timers.
- Switch: resolved on shooter's machine, sent as event data (EventMagnitude=index); server checks ownership. One predicted `GE_WeaponSwitching` = refire wait + 0.2 s drop + 0.25 s raise; index changes after wait+drop.
- ASC: event-data activation via new hook `PrepareInputActivation` in `VeloxGameplayAbility.h`; press+release in one frame still fires a WhileInputActive ability once.

## Departures from ADR / open items (for architect)
1. Remote shooter, server side: WeaponSwitching tag ignored, pending switch target accepted, `NextFireTime` not checked; only the bucket applies. Reason: server timers run late by latency+jitter, a shot right after raise would be rejected. **Architect to rule.**
2. MG spread 1.4° applied (Q3 Bullet_Fire); review said "one entry = Dir" for MG. Spread 0 gives the review's behaviour.
3. `NextFireTime` carries the remainder (Q3 weapon time) instead of Now+Interval.
4. Client ends each shot without end RPC (2 reliable RPCs/shot); `bRetriggerInstancedAbility` is the safety net.
5. One heap allocation per shot for target data (GAS handle API).
6. Weapon content comes from `create_bootstrap_content.py`, bucket test in `VeloxWeaponTests.cpp`; `docs/content-bootstrap.md` not updated.
7. `velox.combat.FireTest [AimDistanceCm]`: default aims at nearest pawn; 0 = straight along aim; >0 also moves the shooter without sweep. On a network client it drives the real input path.

## Hand-offs
- Task 05: projectile fire ability; set `FireAbility` on RL/PG; narrow `ProjectileClass` (currently `TSubclassOf<AActor>`).
- Task 06: call `RemoveAllGameplayCues` before clearing the avatar; end spawn protection on fire; HUD hit marker (state in feedback component). `ResetToLoadout` already runs on every possession.
- Task 08: add `VeloxShotFired aim_err_cm` line; shots ~1 s after joining logged `view_ms=250` (clamped) and `hint=fallback` — check interpolation estimate after time sync converges (03/03b code). Use `--port` for nettest when agents run in parallel.
