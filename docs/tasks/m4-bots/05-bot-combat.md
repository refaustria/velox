# 05 — Bot combat: perception, aim, weapon choice, fire, dodge, rocket-jump link, budget stats

**Milestone:** M4 Bots
**Agent:** dev-hard
**Status:** done (merged; notes/05-report.md)
**Depends on:** 04; ADR 0005 §4 (RocketJump), §6, §8, §9, §10, §13. Parallel with 06 (file ownership in the README).

## Goal
Bots see enemies (FOV + world-only LOS), remember and "hear" attackers, aim with the reaction/error/tracking model, choose weapons by range and ammo, fire under the ADR fire rule without splashing themselves, strafe and dodge under fire, dodge incoming rockets on Hard+, execute the rocket-jump link maneuver, and report their per-tick cost. Bot shots resolve at `Now` with zero rewind.

## Scope — files/modules the agent may touch
- `Source/Velox/Bots/VeloxBotCombat.{h,cpp}` (new: perception update, enemy memory, `ShouldFire`, combat overlay, projectile dodge — thin glue over the 02 cores), `Bots/VeloxBotController.cpp` — `UpdateCombat(Dt)` and the Act phase only (05 owns these functions; 06 owns `UpdateGoal`), `Bots/VeloxBotPathFollowingComponent.cpp` — the `RocketJump` maneuver branch only, `Bots/VeloxBotSubsystem.{h,cpp}` — projectile registry, `STATGROUP_VeloxBots`, `VeloxBotBudget` log, `BudgetWarnMicroseconds`
- `Source/Velox/Weapons/VeloxProjectile.cpp` — register/unregister with the bot subsystem on authority (`BeginPlay`/`EndPlay`), nothing else
- `Source/Velox/Tests/VeloxBotTests.cpp` — the `Maneuver` rocket-jump rows and `ShouldFire` rows (own section); `Tests/VeloxBotFunctionalTest.cpp` — the rocket-jump case of `FT_BotNavigation`
- `Source/Velox/Combat/VeloxHealthComponent.*` — only if `OnDamaged` lacks what §6 needs (instigator is already in `FVeloxDamageEvent`; expected: no change)
(Anything not listed is out of scope. `Net/VeloxLagCompensationSubsystem` is read-only: the registered-pawn list is the enemy candidate list.)

## Relevant ADRs / references
- `docs/adr/0005-bots.md` §4 (rocket-jump sequence, eligibility, abort), §6, §8 (wiring, fire rule, lag-comp statement), §9, §10, §13 (budget and stats), §14 (acceptance numbers)
- `docs/adr/0003-combat-gas-and-lag-compensation.md` §5 (fire ability local path), §6 (`TraceShot` at `Now`, `Weapon` channel ignored by pawns), §7 (projectiles), B6, B37 (local rhythm rule), C2 (bots resolve self-splash from the tag)

## Acceptance criteria
- [ ] Perception at `PerceptionIntervalSeconds`, staggered by `BotIndex`; two traces per candidate on `Velox_TraceChannel_Weapon`; FOV and range per preset; damage from an unseen instigator turns the bot toward it after `ReactionSeconds`.
- [ ] Aim: `FVeloxAimModel::Update` output is the control rotation (pitch included); lead only with `ProjectileSpeed > 0`; idle aim follows the path direction.
- [ ] Weapon choice via `VeloxWeaponChoice::Pick`, switching through `InputTag.Weapon.Slot<N>`; fire via `InputTag.Fire` edges + `ProcessAbilityInput`; the fire rule incl. self-splash safety; `VeloxShot` log shows `rewind_ms=0` for bot shots.
- [ ] Combat overlay (strafe flip timing, dodge jumps, range keeping with a nav raycast leash) and projectile dodge (Hard+) per ADR §10.
- [ ] `RocketJump` maneuver: `FT_BotNavigation` rocket-jump case green (Hard bot with all weapons reaches the rockets platform, `type=RocketJump result=ok` ≤ 25 s); abort path logged when the RL is missing.
- [ ] Headless `L_CombatTest?listen` with two Hard bots (`?Bots=2?BotsFill=1?BotDifficulty=Hard`): ≥ 5 frags in 60 s; with two Medium bots no `Velox.Damage.Self` kill in 60 s.
- [ ] `VeloxBotBudget bots=2 mean_us= max_us=` every 5 s; mean ≤ 300 µs on this VM for the run above.
- [ ] `infra/ci/build.sh` green.

## Verification steps
1. `UnrealEditor-Cmd Velox.uproject "L_CombatTest?listen?Bots=2?BotsFill=1?BotDifficulty=Hard" -game -nullrhi -ExecCmds="velox.bots.Debug 1" -log …` for 60 s; count `VeloxKill` lines; grep `VeloxShot` for `rewind_ms=0`; collect `VeloxBotBudget`.
2. `infra/ci/build.sh`.

## Report back
Standard dev-hard format with a design summary for `architect` review (perception cost, how the fire ability's local path behaved for bots, rocket-jump numbers reached: apex height, self-damage).

## Notes / outcome
(Filled by the agent or the lead after completion.)
