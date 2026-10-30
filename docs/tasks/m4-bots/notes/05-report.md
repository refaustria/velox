# M4-05 bot combat — agent report (2026-10-30)
build.sh 94 tests (+3), 0 failures, no warnings (`Saved/CI/20261030-032700-249024`, worktree); `-DisableAdaptiveUnity` OK. FT_BotNavigation PASS seeds 1–5 (RocketJump ok first attempt, rockets in 4.4–4.5 s, apex 660 cm, self knockback 1050 cm/s, 14 health + 28 armor; abort path `reason=ineligible`). FT_BotStrafeCorridor PASS. L_CombatTest Hard 2 bots 60 s: 7 frags, 352 `VeloxShot` all `rewind_ms=0.0`, 41 % hits. Medium: 0 self kills (also L_ItemTest 100 s). Budget mean 19–78 µs/bot; non-path max mostly < 200 µs idle; path ticks ≤ ~950 µs; fire ticks 0.3–5 ms.
- `VeloxBotCombat` world-free rules (`EvaluateFire/ShouldFire`, `InFov`, sticky `SelectTarget`, `CombatWish`, `ProjectileThreat`, `RollChance`, `VeloxBotRocketJump::ArriveWish`) + glue `Perceive/Overlay/Act`; `UVeloxBotCombatSettings`; cvar `velox.bots.Peaceful`.
- Perception staggered by BotIndex, candidates = lag-comp registered pawns, FOV/range, centre then head trace on the Weapon channel, memory, PlayerId keys. Hearing via `UVeloxHealthComponent::OnDamaged`.
- Aim from `FVeloxAimModel` (pitch incl.), lead for projectiles, splash 50 cm low, idle aim path/last-seen/heard; randomness from the controller stream. Maneuver overrides on top; fire rule suppressed while the rocket jump wants the launcher.
- Weapon `Pick` every 0.25 s via slot tags; fire rule ADR §8 + `DirectHitRadiusCm` 60 (departure). Overlay: strafe flips with nav probe, range keeping, leash 2500, dodge jumps, projectile dodge (Hard+).
- F1 fixed: `AVeloxBotController::EvaluateRocketJump(const AVeloxNavLink&, bool&)` called in `TickManeuver`. New ballistic air braking in Launched/Airborne. D12: Fight with visible enemy requests no path; chase last-seen (`ChaseMoveToleranceCm` 300).
- Budget `STATGROUP_VeloxBots`, `VeloxBotBudget bots= mean_us= max_us= max_path_us= max_fire_us= paths=/s traces=/s navrays=/s over=` every 5 s; max excludes path and fire ticks.
- Projectiles register with the subsystem in `BeginPlay` (authority, non-fake).
## Open (architect / 07)
- `DirectHitRadiusCm` 60 (Q3 BotCheckAttack) — without it Hard bots wasted MG ammo (1 frag vs 7).
- Fire ticks excluded from the peak (shared player fire path + logging).
- Hearing and projectile dodge not triggered in a world run → 07 soak should grep.
- Medium no-self-kill trivial on L_CombatTest (MG only); L_ItemTest covers splash.
- `velox.combat.GiveAllWeapons` on a dedicated server does not reach bots.
- 06: use `GetCombatState()`; count path queries via `RecordBotTick`; merge points controller header + `Tick`.
