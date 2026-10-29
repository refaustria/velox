# 06b — Death review fixes: dummies respawn in place, invulnerable soak targets, kill labels

**Milestone:** M2 Combat
**Agent:** dev
**Status:** todo
**Depends on:** 06, 07 (review `docs/reviews/m2-05-06-projectiles-death-review.md` §2 defects 2, 8, 10, 13; ADR 0003 amendments B27, B28, B29). Must land before 08. Independent of 05b and 04c.

## Goal
08's `FireRun` fires 10 rails at a 200-hp strafing dummy; today the dummy dies at the second shot and 1.7 s later respawns as the default pawn at a farthest-half player start, strafing there. After this task a dummy respawns as its dummy class at its spawner's transform and restarts its strafe, and a spawner can mark its dummy invulnerable (permanent spawn protection: no damage, hits still confirmed), so soak targets stay where the script aims. Also: readable kill labels for AI PlayerStates, the corpse floor drop no longer rests on other pawns, and a client corpse stops simulating movement as soon as it learns it is a corpse.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxDummyController.h/.cpp` — `FTransform RespawnTransform`, `TSubclassOf<AVeloxCharacter> RespawnPawnClass`, `bool HasFixedRespawn() const`; `OnPossess` uses `RespawnTransform` (when set) as the strafe origin and facing
- `Source/Velox/Bots/VeloxDummySpawner.h/.cpp` — sets the two fields on spawn; `bInvulnerable` (`UPROPERTY`, and `velox.combat.SpawnDummy … invulnerable` argument) → after possession apply `UVeloxGE_SpawnProtection` with infinite duration (`VeloxEffects::MakeDurationSpec` with a duration ≤ 0 meaning infinite, or a dedicated infinite spec helper — keep `GE_SpawnInit`'s removal of `SpawnProtected` from clearing it: apply after `OnPawnPossessed`, re-apply on every possession)
- `Source/Velox/GameModes/VeloxGameMode.h/.cpp` — `RestartPlayer` override: a controller with a fixed respawn is restarted with `RestartPlayerAtTransform(Controller, RespawnTransform)`; `GetDefaultPawnClassForController_Implementation` returns `RespawnPawnClass` for it; `PlayerDied`/`AssignPlayerColor` log lines use the label helper
- `Source/Velox/GameModes/VeloxPlayerState.h/.cpp` — `static FString VeloxPlayerLabel(const APlayerState*)` (name if non-empty, else the object name; `"-"` for null)
- `Source/Velox/Character/VeloxCharacter.cpp` — `CompleteDeath` floor drop: sweep ignoring pawns (collision response override for the one `AddActorWorldOffset`, or temporarily set the capsule's Pawn response to Ignore); `OnRep_IsCorpse` on clients: `GetCharacterMovement()->DisableMovement()` in `ApplyCorpseState` for every role (the server already does it)
- `Source/Velox/Tests/VeloxDeathTests.cpp` — new test `Velox.GameMode.DummyRespawn`

## Relevant ADRs / references
- `docs/adr/0003-combat-gas-and-lag-compensation.md` — §8, §9, B27, B28, B29
- `docs/reviews/m2-05-06-projectiles-death-review.md` — §2 defects 2, 8, 10, 13; §1 rulings 5 and 9
- `docs/tasks/m2-combat/08-fire-nettest.md` — what `FireRun` needs from the dummy

## Acceptance criteria
- [ ] `Velox.GameMode.DummyRespawn` (private game world with an `AVeloxGameMode`): a spawner-created dummy killed with 1000 environment damage respawns within `MinRespawnDelay + 0.1 s` as the spawner's `DummyClass` at the spawner transform (≤ 1 cm), controlled by the same `AVeloxDummyController`, strafing (its `StrafeOrigin` equals the spawner location); an invulnerable dummy takes no health loss from 1000 damage, `IsRegistered` stays true, and a hitscan shot at it still yields `Hits.Num() > 0` (`FVeloxServerShotInfo`).
- [ ] `Velox.GameMode.DeathFlow`, `SpawnSelection`, `SpawnProtection`, `PlayerColors` unchanged and green; human respawn path untouched (`ChoosePlayerStart` still used for player controllers and for AI controllers without a fixed respawn).
- [ ] `VeloxKill`/`VeloxColor` lines show a non-empty label for AI PlayerStates (object name) and the player name for humans.
- [ ] Corpse drop: a pawn killed in the air above another pawn ends on the floor, not on the pawn (assert Z in the test by placing a second pawn under it).
- [ ] `infra/ci/build.sh` → `[ci] CI OK`; headless `velox.combat.SpawnDummy` + `velox.combat.KillTest env` on `L_CombatTest`: the dummy respawns at its spawner (log `VeloxRespawn … (fixed)` with the spawner's location).

## Verification steps
1. `infra/ci/build.sh` → `[ci] CI OK`.
2. Headless listen run on `L_CombatTest`: `velox.combat.SpawnDummy Lane 0 0 0 DummyStrafe`, `velox.combat.KillTest env`, wait 2 s → `VeloxRespawn` line names the fixed transform; `velox.combat.SpawnDummy Lane2 … invulnerable`, `velox.combat.KillTest rail` → health unchanged, `VeloxShot … hit=1`.

## Report back
Standard dev format: result, what changed, verification output tail, open issues.

## Notes / outcome
(Filled by the agent or the lead after completion.)
