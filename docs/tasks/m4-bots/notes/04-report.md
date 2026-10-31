# M4-04 bot controller + navigation — agent report (2026-10-30)
build.sh 91 tests, 0 failures (`Saved/CI/20261030-023508-178335`, worktree); `-DisableAdaptiveUnity` editor build OK; StrafeRun/PadRun/FireRun 3/3 C=0.
- `AVeloxBotController : AAIController` (`bWantsPlayerState`, TG_PrePhysics): Perceive (stub) → Decide (forced goal / virtual `UpdateGoal`, roam) → EnsureMove → Navigate (`StepFollow`, follower does not self-tick) → `UpdateCombat` (stub) → `Act` → `ApplyInput`. Input order: `UpdateControlRotation` (no Super) → `AddMovementInput` → `SetLocalMoveInput` (CPM snap) → `SetJumpInput` edges → crouch edges → slot press+release, fire edges → `ProcessAbilityInput`. No Velocity/Launch/RequestDirectMove/SetActorLocation writes.
- `UVeloxBotPathFollowingComponent`: link match in `SetMoveSegment` (0.0 cm); maneuver core from 02 mapped by switch; Pad/Drop/RocketJump steer straight, Jump/JumpFast strafe; impulse = `KnockbackGrace` rising; `VeloxBotLink ... result=` + `OnLinkFinished`.
- Subsystem `AddBot/KickBot/LoadDifficulty/FindBot`, registry `TWeakObjectPtr<AVeloxBotController>`, lowest free BotIndex, seed `HashCombine(MasterSeed, BotIndex, LifeCount)`. GameMode `StartBot`, `NumBots`, URL `?Bots=?BotDifficulty=` in `StartPlay`. PC `ServerAddBot/ServerKickBot` (refused in shipping, rate-limited). Cheats `AddBot`, `KickBot`, `velox.bots.List/Goal/Kill/Players/After/Freeze/Debug`. `AVeloxCharacter::SetJumpInput`. L_CombatTest nav bounds (24 tiles).
- Evidence: FT_BotNavigation PASS (Mega via JumpPad 2.0–11.8 s, RA via Drop+JumpPad 4.3 s, 4 links ok); FT_BotStrafeCorridor PASS (Hard peak 1631 cm/s, 4.32 s; Easy 5.80 s, 800 cm/s); AddBot/Kill/respawn/KickBot, `?Bots=2`, Duel cap, client RPC forwarding. Roam soak 5 bots 90 s: 1 stuck (recovered), 0 missed links. Cost mean 9–28 µs, spikes 116–732 µs (path queries, first tick).
## Departures (architect)
1. `UpdatePathSegment` overridden: pad launches before the centre (link segment starts within `LinkEntryRadiusCm` 100 or on impulse within 250 cm); partial paths finish at their last point.
2. Landing `ok` if a nav raycast from landing to link end stays on mesh (pad landings 150–200 cm from the item-centre link end).
3. Airborne link start waits for ground or impulse.
4. Weapon slot press+release same tick; `?Bots=` in `StartPlay`; forced goals survive respawn.
5. FTs are console commands (not in build.sh).
6. `velox.bots.Kill` added (KillTest hits dummies; dummies count as active players).
## Hand-offs
- 05: fill Perceive/UpdateCombat/aim in Act, keep maneuver view requests (`bFaceEnd`, `bForcePitch`, `bFire`, RL select); RocketJump plumbing wired but unexercised; add rocket-jump step to FtNavigation; path-query spikes ~650–730 µs vs §13 warning.
- 06: override `UpdateGoal`, use `SetGoal` (State protected); `OnPickupTaken` clears non-forced item goals; acceptance radius 30 cm; partial ends increment `FailedMoves`; roam uses the bot's stream.
