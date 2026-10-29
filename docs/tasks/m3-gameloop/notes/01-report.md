# M3-01 items foundation — agent report (2026-10-29)

build.sh 57 tests (53 + `Velox.Items.PickupRules`, `PickupTiming`, `Content`, `Pickup`), 0 failures (worktree CI `20261029-204915-3774652`). Headless `velox.items.PickupTest Health.Medium quit`: refused at 100, taken at 50 → 75, respawn at t=35.5, standing retake, `result=PASS`. Scratch net check 100 ms one-way / 2 %: 5/5 `VeloxPickupPredict` → `PredictConfirmed` in 217–250 ms, server grants correct, 0 corrections (script not in nettest; ItemRun is task 04).

## Design
- `Items/VeloxPickupRules.h` world-free: `CanGrab`/`Gain` (Q3), `FVeloxPickupTimer`, `RespawnSecondsFor`, `StaysAfterTake`, `InitialDelayFor`; mode rules `FVeloxItemModeRules` (ItemRespawnScale, WeaponRespawnSeconds, bWeaponStay) via `UVeloxItemRegistrySubsystem::SetModeRules` (FFA defaults; task 05 sets them).
- `AVeloxPickup`: always relevant, `DORM_DormantAll`, 10 Hz, `FlushNetDormancy` on change; replicates `RespawnServerTime`, `LastTakenBy`, `TakeCount`, `Definition` (initial-only). Origin = floor point; trigger 75×75×90 cm, Pawn only. Grants on the PlayerState ASC: `GE_Heal`, `GE_ArmorGain`, `MakeAmmoGainSpec`, `AddWeapon(Def, gain)`. Standing retake on respawn; refused pawns re-checked per frame while inside (tick only when needed). `Reset()` reschedules the initial delay.
- Prediction: `TryPredictTake` hides the item, weapon bit via `Inventory->PredictOwned(idx, now + PickupPredictionTimeout)`; outcomes Confirmed / Lost / Refused (timeout) via one OnRep handler.
- Inventory: effective mask (replicated + unexpired predicted bits); autoswitch in `OnRep_OwnedMask(Old)` via world-free `SelectAutoSwitchTarget` (exactly one new bit, old mask ≠ 0, setting on, current MG/Gauntlet) pressing the slot through the ASC input path; listen host calls `RunAutoSwitch`.
- Cheats: `velox.items.Spawn <Name> [cm]`, `SpawnAt <Name> x y z`, `List`, `PickupTest [Name] [quit]`.
- Content: 23 `Content/Items/DA_Item_<Type>_<Name>` from `Tools/editor/create_item_content.py` (ADR says 21, its table lists 23).

## Departures / open
- `PowerupEffect` is `TSubclassOf<UGameplayEffect>`; powerups refuse with `reason=no_powerup_effect` until task 02. `GrantPowerup` implements the remove/re-apply fallback.
- MatchFrozen check slot in `AVeloxPickup::IsPawnEligible` for task 05.
- Late joiner gets one `OnPickupTaken` for an earlier take of a placed item.
- Weapon-stay on: owner of the weapon refused (no infinite ammo); unused in M3.
- A switch to a predicted weapon within ~RTT/2 of the pickup can be server-rejected.
- Q3: standing on a weapon pickup gives +1 ammo per 5 s respawn.

## Hand-offs
- 02: narrow `PowerupEffect`, set `powerup_effect` in the content script, `GrantPowerup` → `ModifyActiveEffectStartTime`.
- 03: call `ServerRefreshPowerupMoveTimes` from `GrantPowerup`.
- 04: asset names `DA_Item_<Type>_<Name>`, origin on the floor; client log `VeloxPickupPredict`, `VeloxPickupPredictConfirmed/Lost/Refused`; server `VeloxPickup item= by= gain= respawn_in= t= take=`; `velox.items.SpawnAt` from server ExecCmds.
- 05: `Registry->SetModeRules(...)` from the mode definition; MatchFrozen in `IsPawnEligible`.
- 07: read via `UVeloxItemRegistrySubsystem` (`GetMajorPickups`, `GetTimeUntilRespawn`, `GetLastTakenBy`, `OnPickupTaken`/`OnPickupRespawned`).
- 09: replace `AVeloxPlayerController::bAutoSwitchOnPickup` with the user setting.
